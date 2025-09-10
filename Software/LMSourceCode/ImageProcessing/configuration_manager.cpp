/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */

#include "configuration_manager.h"
#include "logging_tools.h"
#include <filesystem>
#include <fstream>
#include <regex>
#include <cstdlib>

// Helper function to merge property trees (user overrides defaults)
namespace {
    void merge_ptree(const boost::property_tree::ptree& from, boost::property_tree::ptree& to) {
        for (const auto& [key, value] : from) {
            if (value.empty()) {
                // Leaf node - copy value
                to.put(key, value.data());
            } else {
                // Non-leaf node - recursive merge
                auto child = to.get_child_optional(key);
                if (child) {
                    boost::property_tree::ptree merged = *child;
                    merge_ptree(value, merged);
                    to.put_child(key, merged);
                } else {
                    to.put_child(key, value);
                }
            }
        }
    }
    
    // Helper function to convert YAML to property tree
    void yaml_to_ptree(const YAML::Node& node, boost::property_tree::ptree& pt, const std::string& key = "") {
        if (node.IsScalar()) {
            pt.put(key, node.as<std::string>());
        } else if (node.IsSequence()) {
            for (size_t i = 0; i < node.size(); ++i) {
                yaml_to_ptree(node[i], pt, key + "." + std::to_string(i));
            }
        } else if (node.IsMap()) {
            for (const auto& it : node) {
                std::string child_key = key.empty() ? it.first.as<std::string>() : key + "." + it.first.as<std::string>();
                yaml_to_ptree(it.second, pt, child_key);
            }
        }
    }
    
    // Helper function to load YAML file to property tree
    void load_yaml_to_ptree(const std::string& filename, boost::property_tree::ptree& pt) {
        try {
            YAML::Node yaml_node = YAML::LoadFile(filename);
            yaml_to_ptree(yaml_node, pt);
        } catch (const YAML::Exception& e) {
            throw std::runtime_error("Failed to load YAML file: " + std::string(e.what()));
        }
    }
}

namespace golf_sim {

bool ConfigurationManager::Initialize(
    const std::string& json_config_file,
    const std::string& yaml_config_file,
    const std::map<std::string, std::string>& cli_overrides) {
    
    GS_LOG_TRACE_MSG(trace, "Initializing ConfigurationManager");
    
    json_config_file_ = json_config_file;
    
    // Load system defaults from golf_sim_config.json
    if (std::filesystem::exists(json_config_file)) {
        try {
            boost::property_tree::read_json(json_config_file, json_config_);
            GS_LOG_MSG(info, "Loaded system defaults from: " + json_config_file);
        } catch (const boost::property_tree::json_parser_error& e) {
            GS_LOG_MSG(error, "Failed to parse system config: " + std::string(e.what()));
            return false;
        }
    } else {
        GS_LOG_MSG(warning, "System configuration file not found: " + json_config_file);
    }
    
    // Load user settings from user_settings.json (new JSON-only approach)
    std::string user_settings_file = std::string(std::getenv("HOME") ? std::getenv("HOME") : "") + "/.pitrac/config/user_settings.json";
    
    if (std::filesystem::exists(user_settings_file)) {
        try {
            boost::property_tree::ptree user_settings;
            boost::property_tree::read_json(user_settings_file, user_settings);
            
            // Merge user settings into json_config (user overrides defaults)
            merge_ptree(user_settings, json_config_);
            
            GS_LOG_MSG(info, "Loaded user settings from: " + user_settings_file);
        } catch (const boost::property_tree::json_parser_error& e) {
            GS_LOG_MSG(error, "Failed to parse user settings: " + std::string(e.what()));
            // Continue with defaults if user settings are corrupt
        }
    } else {
        GS_LOG_MSG(debug, "No user settings found at: " + user_settings_file);
    }
    
    // DEPRECATED: Support legacy YAML for migration period only
    // This will be removed in future versions
    if (!yaml_config_file.empty() && yaml_config_file != "none") {
        std::string yaml_file = yaml_config_file;
        if (yaml_file.empty()) {
            // Check for legacy YAML in old locations
            const std::vector<std::string> yaml_locations = {
                std::string(std::getenv("HOME") ? std::getenv("HOME") : "") + "/.pitrac/config/pitrac.yaml",
                "/etc/pitrac/pitrac.yaml"
            };
            
            for (const auto& location : yaml_locations) {
                if (std::filesystem::exists(location)) {
                    GS_LOG_MSG(warning, "Found legacy YAML config at: " + location);
                    GS_LOG_MSG(warning, "Please run 'pitrac config migrate-to-json' to convert to new format");
                    break;
                }
            }
        }
    }
    
    // Apply CLI overrides
    for (const auto& [key, value] : cli_overrides) {
        SetOverride(key, value);
    }
    
    // Check if a preset is specified
    std::string preset = GetString("_preset", "");
    if (!preset.empty()) {
        ApplyPreset(preset);
    }
    
    return true;
}

std::string ConfigurationManager::GetString(const std::string& key, const std::string& default_value) const {
    return GetValue<std::string>(key, default_value);
}

int ConfigurationManager::GetInt(const std::string& key, int default_value) const {
    std::string str_val = GetString(key, std::to_string(default_value));
    try {
        return std::stoi(str_val);
    } catch (...) {
        return default_value;
    }
}

float ConfigurationManager::GetFloat(const std::string& key, float default_value) const {
    std::string str_val = GetString(key, std::to_string(default_value));
    try {
        return std::stof(str_val);
    } catch (...) {
        return default_value;
    }
}

bool ConfigurationManager::GetBool(const std::string& key, bool default_value) const {
    std::string str_val = GetString(key, default_value ? "true" : "false");
    
    // Handle various boolean representations
    if (str_val == "true" || str_val == "1" || str_val == "yes" || str_val == "on") {
        return true;
    }
    if (str_val == "false" || str_val == "0" || str_val == "no" || str_val == "off") {
        return false;
    }
    
    return default_value;
}

void ConfigurationManager::SetOverride(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(config_mutex_);
    SetInTree(cli_overrides_, key, value);
}

bool ConfigurationManager::HasKey(const std::string& key) const {
    if (GetFromTree<std::string>(cli_overrides_, key).has_value()) return true;
    if (GetFromTree<std::string>(yaml_config_, key).has_value()) return true;
    
    std::string json_path = MapToJsonPath(key);
    if (GetFromTree<std::string>(json_config_, json_path).has_value()) return true;
    
    return false;
}

std::string ConfigurationManager::GetJsonPath(const std::string& yaml_key) const {
    return MapToJsonPath(yaml_key);
}

bool ConfigurationManager::ApplyPreset(const std::string& preset_name) {
    try {
        auto preset = presets_.get_child_optional("presets." + preset_name);
        if (!preset) {
            GS_LOG_MSG(warning, "Preset not found: " + preset_name);
            return false;
        }
        
        GS_LOG_MSG(info, "Applying preset: " + preset_name);
        
        // Apply preset settings
        auto settings = preset->get_child_optional("settings");
        if (settings) {
            for (const auto& [key, value] : *settings) {
                std::string val = value.get_value<std::string>();
                SetInTree(yaml_config_, key, val);
            }
        }
        
        return true;
    } catch (const std::exception& e) {
        GS_LOG_MSG(error, "Failed to apply preset: " + std::string(e.what()));
        return false;
    }
}

bool ConfigurationManager::ValidateConfiguration() const {
    validation_errors_.clear();
    bool valid = true;
    
    // Validate all YAML settings against schema
    for (const auto& [key, value] : yaml_config_) {
        std::string str_val = value.get_value<std::string>();
        if (!ValidateValue(key, str_val)) {
            valid = false;
        }
    }
    
    // Validate CLI overrides
    for (const auto& [key, value] : cli_overrides_) {
        std::string str_val = value.get_value<std::string>();
        if (!ValidateValue(key, str_val)) {
            valid = false;
        }
    }
    
    return valid;
}

std::vector<std::string> ConfigurationManager::GetValidationErrors() const {
    return validation_errors_;
}

bool ConfigurationManager::ExportEffectiveConfig(const std::string& output_file, const std::string& format) const {
    try {
        boost::property_tree::ptree effective_config;
        
        // Build effective configuration by merging all tiers
        // Start with JSON defaults
        effective_config = json_config_;
        
        // Apply YAML overrides
        for (const auto& [key, value] : yaml_config_) {
            std::string json_path = MapToJsonPath(key);
            SetInTree(effective_config, json_path, value.get_value<std::string>());
        }
        
        // Apply CLI overrides
        for (const auto& [key, value] : cli_overrides_) {
            std::string json_path = MapToJsonPath(key);
            SetInTree(effective_config, json_path, value.get_value<std::string>());
        }
        
        // Export in requested format
        if (format == "json") {
            boost::property_tree::write_json(output_file, effective_config);
        } else if (format == "yaml") {
            // For now, export as JSON when yaml is requested
            // TODO: Implement proper YAML export using yaml-cpp
            GS_LOG_MSG(warning, "YAML export not yet implemented, exporting as JSON instead");
            boost::property_tree::write_json(output_file, effective_config);
        } else {
            GS_LOG_MSG(error, "Unknown export format: " + format);
            return false;
        }
        
        return true;
    } catch (const std::exception& e) {
        GS_LOG_MSG(error, "Failed to export configuration: " + std::string(e.what()));
        return false;
    }
}

std::string ConfigurationManager::GetValueSource(const std::string& key) const {
    if (GetFromTree<std::string>(cli_overrides_, key).has_value()) {
        return "cli";
    }
    if (GetFromTree<std::string>(yaml_config_, key).has_value()) {
        return "yaml";
    }
    
    std::string json_path = MapToJsonPath(key);
    if (GetFromTree<std::string>(json_config_, json_path).has_value()) {
        return "json";
    }
    
    return "not_found";
}

bool ConfigurationManager::Reload() {
    GS_LOG_MSG(info, "Reloading configuration");
    
    // Clear existing configuration
    json_config_.clear();
    yaml_config_.clear();
    // Keep CLI overrides
    
    // Reload files
    return Initialize(json_config_file_, yaml_config_file_, {});
}

bool ConfigurationManager::LoadMappings(const std::string& mappings_file) {
    try {
        load_yaml_to_ptree(mappings_file, mappings_);
        
        // Extract presets if present
        auto presets = mappings_.get_child_optional("presets");
        if (presets) {
            presets_ = *presets;
        }
        
        // Build reverse mapping cache
        BuildReverseMappingCache();
        
        GS_LOG_MSG(info, "Loaded parameter mappings from: " + mappings_file);
        return true;
    } catch (const std::exception& e) {
        GS_LOG_MSG(error, "Failed to load mappings: " + std::string(e.what()));
        return false;
    }
}

std::string ConfigurationManager::MapToJsonPath(const std::string& yaml_key) const {
    try {
        // Look up mapping
        auto mapping = mappings_.get_child_optional("mappings." + yaml_key);
        if (mapping) {
            auto json_path = mapping->get_optional<std::string>("json_path");
            if (json_path) {
                return *json_path;
            }
        }
    } catch (...) {
        // Ignore and return original key
    }
    
    // No mapping found, return original key
    return yaml_key;
}

std::string ConfigurationManager::MapToYamlKey(const std::string& json_path) const {
    // First check the cache
    auto it = json_to_yaml_map_.find(json_path);
    if (it != json_to_yaml_map_.end()) {
        return it->second;
    }
    
    // No mapping found, return original path
    return json_path;
}

void ConfigurationManager::BuildReverseMappingCache() {
    json_to_yaml_map_.clear();
    
    try {
        auto mappings = mappings_.get_child_optional("mappings");
        if (!mappings) {
            return;
        }
        
        // Iterate through all mappings and build reverse map
        for (const auto& [yaml_key, mapping_node] : *mappings) {
            auto json_path = mapping_node.get_optional<std::string>("json_path");
            if (json_path) {
                // Store the reverse mapping: JSON path -> YAML key
                json_to_yaml_map_[*json_path] = yaml_key;
                GS_LOG_TRACE_MSG(trace, "Reverse mapping: " + *json_path + " -> " + yaml_key);
            }
        }
        
        GS_LOG_MSG(debug, "Built reverse mapping cache with " + std::to_string(json_to_yaml_map_.size()) + " entries");
    } catch (const std::exception& e) {
        GS_LOG_MSG(warning, "Failed to build reverse mapping cache: " + std::string(e.what()));
    }
}

std::string ConfigurationManager::ConvertToJson(const std::string& yaml_key, const std::string& value) const {
    try {
        auto mapping = mappings_.get_child_optional("mappings." + yaml_key);
        if (mapping) {
            auto to_json = mapping->get_optional<std::string>("to_json");
            if (to_json) {
                // Simple conversion for boolean to "0"/"1"
                if (*to_json == "value ? \"1\" : \"0\"") {
                    return (value == "true" || value == "1") ? "1" : "0";
                }
            }
        }
    } catch (...) {
        // Ignore and return original value
    }
    
    return value;
}

std::string ConfigurationManager::ConvertFromJson(const std::string& yaml_key, const std::string& value) const {
    try {
        auto mapping = mappings_.get_child_optional("mappings." + yaml_key);
        if (mapping) {
            auto from_json = mapping->get_optional<std::string>("from_json");
            if (from_json) {
                // Simple conversion for "0"/"1" to boolean
                if (*from_json == "value == \"1\"") {
                    return value == "1" ? "true" : "false";
                }
            }
        }
    } catch (...) {
        // Ignore and return original value
    }
    
    return value;
}

std::string ConfigurationManager::ExpandPath(const std::string& path) const {
    if (path.empty() || path[0] != '~') {
        return path;
    }
    
    const char* home = std::getenv("HOME");
    if (!home) {
        return path;
    }
    
    return std::string(home) + path.substr(1);
}

bool ConfigurationManager::ValidateValue(const std::string& key, const std::string& value) const {
    try {
        auto mapping = mappings_.get_child_optional("mappings." + key);
        if (!mapping) {
            return true; // No validation rules, assume valid
        }
        
        auto validation = mapping->get_child_optional("validation");
        if (!validation) {
            return true; // No validation rules
        }
        
        // Check enum values
        auto enum_values = validation->get_child_optional("enum");
        if (enum_values) {
            bool found = false;
            for (const auto& [_, enum_val] : *enum_values) {
                if (enum_val.get_value<std::string>() == value) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                validation_errors_.push_back(key + ": value '" + value + "' not in allowed values");
                return false;
            }
        }
        
        // Check min/max for numeric values
        auto min = validation->get_optional<float>("min");
        auto max = validation->get_optional<float>("max");
        
        if (min || max) {
            try {
                float num_val = std::stof(value);
                
                if (min && num_val < *min) {
                    validation_errors_.push_back(key + ": value " + value + " below minimum " + std::to_string(*min));
                    return false;
                }
                
                if (max && num_val > *max) {
                    validation_errors_.push_back(key + ": value " + value + " above maximum " + std::to_string(*max));
                    return false;
                }
            } catch (...) {
                validation_errors_.push_back(key + ": value '" + value + "' is not numeric");
                return false;
            }
        }
        
        // Check pattern (regex)
        auto pattern = validation->get_optional<std::string>("pattern");
        if (pattern) {
            std::regex re(*pattern);
            if (!std::regex_match(value, re)) {
                validation_errors_.push_back(key + ": value '" + value + "' does not match pattern");
                return false;
            }
        }
        
        return true;
    } catch (...) {
        return true; // Ignore validation errors
    }
}

} // namespace golf_sim