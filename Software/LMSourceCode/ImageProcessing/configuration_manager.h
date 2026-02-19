/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */

#pragma once

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <yaml-cpp/yaml.h>
#include <opencv2/core.hpp>
#include <string>
#include <map>
#include <optional>
#include <mutex>

#include "logging_tools.h"

namespace golf_sim {

/**
 * ConfigurationManager implements a three-tier configuration system:
 * 1. Default values (built-in/JSON template)
 * 2. User overrides (YAML configuration)
 * 3. Runtime overrides (command-line arguments)
 * 
 * The system maintains backward compatibility with golf_sim_config.json
 * while providing user-friendly YAML configuration options.
 */
class ConfigurationManager {
public:
    static ConfigurationManager& GetInstance() {
        static ConfigurationManager instance;
        return instance;
    }

    // Delete copy constructor and assignment operator
    ConfigurationManager(const ConfigurationManager&) = delete;
    ConfigurationManager& operator=(const ConfigurationManager&) = delete;

    /**
     * Initialize the configuration system
     * @param json_config_file Path to golf_sim_config.json (defaults)
     * @param yaml_config_file Path to pitrac.yaml (user overrides)
     * @param cli_overrides Command-line overrides
     * @return true if initialization successful
     */
    bool Initialize(
        const std::string& json_config_file = ".pitrac/config/generated_golf_sim_config.json",
        const std::string& yaml_config_file = "",
        const std::map<std::string, std::string>& cli_overrides = {}
    );

    /**
     * Get a configuration value with fallback hierarchy
     * @param key Configuration key (e.g., "ball_detection.method")
     * @param default_value Default if not found
     * @return Configuration value
     */
    template<typename T>
    T GetValue(const std::string& key, const T& default_value) const;

    /**
     * Get a configuration value as string
     * @param key Configuration key
     * @param default_value Default if not found
     * @return Configuration value as string
     */
    std::string GetString(const std::string& key, const std::string& default_value = "") const;

    /**
     * Get a configuration value as integer
     * @param key Configuration key
     * @param default_value Default if not found
     * @return Configuration value as integer
     */
    int GetInt(const std::string& key, int default_value = 0) const;

    /**
     * Get a configuration value as float
     * @param key Configuration key
     * @param default_value Default if not found
     * @return Configuration value as float
     */
    float GetFloat(const std::string& key, float default_value = 0.0f) const;

    /**
     * Get a configuration value as boolean
     * @param key Configuration key
     * @param default_value Default if not found
     * @return Configuration value as boolean
     */
    bool GetBool(const std::string& key, bool default_value = false) const;

    /**
     * Set a runtime override value
     * @param key Configuration key
     * @param value Override value
     */
    void SetOverride(const std::string& key, const std::string& value);

    /**
     * Check if a configuration key exists
     * @param key Configuration key
     * @return true if key exists in any tier
     */
    bool HasKey(const std::string& key) const;

    /**
     * Get the JSON configuration path for a YAML key
     * @param yaml_key YAML configuration key
     * @return JSON path if mapped, empty if not
     */
    std::string GetJsonPath(const std::string& yaml_key) const;

    /**
     * Apply a configuration preset
     * @param preset_name Name of preset (e.g., "indoor", "putting")
     * @return true if preset applied successfully
     */
    bool ApplyPreset(const std::string& preset_name);

    /**
     * Validate configuration against schema
     * @return true if configuration is valid
     */
    bool ValidateConfiguration() const;

    /**
     * Get validation errors if any
     * @return Vector of validation error messages
     */
    std::vector<std::string> GetValidationErrors() const;

    /**
     * Export effective configuration (with all overrides applied)
     * @param output_file Path to output file
     * @param format Output format ("yaml" or "json")
     * @return true if export successful
     */
    bool ExportEffectiveConfig(const std::string& output_file, const std::string& format = "yaml") const;

    /**
     * Get the source of a configuration value (for debugging)
     * @param key Configuration key
     * @return Source ("default", "json", "yaml", "cli", or "not_found")
     */
    std::string GetValueSource(const std::string& key) const;

    /**
     * Reload configuration files
     * @return true if reload successful
     */
    bool Reload();

private:
    ConfigurationManager() = default;
    ~ConfigurationManager() = default;

    // Configuration storage (three tiers)
    boost::property_tree::ptree json_config_;     // golf_sim_config.json
    boost::property_tree::ptree yaml_config_;     // pitrac.yaml overrides
    boost::property_tree::ptree cli_overrides_;   // Command-line overrides
    boost::property_tree::ptree mappings_;        // Parameter mappings
    boost::property_tree::ptree presets_;         // Configuration presets

    // File paths for reloading
    std::string json_config_file_;
    std::string yaml_config_file_;

    // Thread safety
    mutable std::mutex config_mutex_;

    // Validation errors
    mutable std::vector<std::string> validation_errors_;

    // Reverse mapping cache (JSON path -> YAML key)
    mutable std::map<std::string, std::string> json_to_yaml_map_;

    /**
     * Load parameter mappings from file
     * @param mappings_file Path to parameter-mappings.yaml
     * @return true if successful
     */
    bool LoadMappings(const std::string& mappings_file);

    /**
     * Build reverse mapping cache from loaded mappings
     */
    void BuildReverseMappingCache();

    /**
     * Map a YAML key to JSON path
     * @param yaml_key YAML configuration key
     * @return Mapped JSON path or original key if no mapping
     */
    std::string MapToJsonPath(const std::string& yaml_key) const;

    /**
     * Map a JSON path to YAML key (reverse mapping)
     * @param json_path JSON configuration path
     * @return Mapped YAML key or original path if no mapping
     */
    std::string MapToYamlKey(const std::string& json_path) const;

    /**
     * Convert YAML value to JSON format
     * @param yaml_key YAML configuration key
     * @param value Value to convert
     * @return Converted value
     */
    std::string ConvertToJson(const std::string& yaml_key, const std::string& value) const;

    /**
     * Convert JSON value to YAML format
     * @param yaml_key YAML configuration key
     * @param value Value to convert
     * @return Converted value
     */
    std::string ConvertFromJson(const std::string& yaml_key, const std::string& value) const;

    /**
     * Expand home directory in path
     * @param path Path potentially containing ~
     * @return Expanded path
     */
    std::string ExpandPath(const std::string& path) const;

    /**
     * Validate a value against schema
     * @param key Configuration key
     * @param value Value to validate
     * @return true if valid
     */
    bool ValidateValue(const std::string& key, const std::string& value) const;

    /**
     * Get value from property tree with dot notation support
     * @param tree Property tree to search
     * @param path Dot-separated path
     * @return Value if found
     */
    template<typename T>
    std::optional<T> GetFromTree(const boost::property_tree::ptree& tree, const std::string& path) const;

    /**
     * Set value in property tree with dot notation support
     * @param tree Property tree to modify
     * @param path Dot-separated path
     * @param value Value to set
     */
    template<typename T>
    void SetInTree(boost::property_tree::ptree& tree, const std::string& path, const T& value) const;
};

// Template implementation
template<typename T>
T ConfigurationManager::GetValue(const std::string& key, const T& default_value) const {
    std::lock_guard<std::mutex> lock(config_mutex_);
    
    // Check CLI overrides first
    if (auto val = GetFromTree<T>(cli_overrides_, key)) {
        return val.value();
    }

    // For YAML config, we need to handle both cases:
    // 1. If the key is a JSON path (e.g., "gs_config.cameras.kCamera1Gain"), 
    //    map it to YAML key and check
    // 2. If the key is already a YAML key, check directly
    
    // Try to map JSON path to YAML key
    std::string yaml_key = MapToYamlKey(key);
    if (yaml_key != key) {
        // It was a JSON path that got mapped to a YAML key
        if (auto val = GetFromTree<T>(yaml_config_, yaml_key)) {
            return val.value();
        }
    }
    
    // Also check with the original key in case it's already a YAML key
    if (auto val = GetFromTree<T>(yaml_config_, key)) {
        return val.value();
    }

    // Map YAML key to JSON path and check JSON config
    std::string json_path = MapToJsonPath(key);
    if (auto val = GetFromTree<T>(json_config_, json_path)) {
        return val.value();
    }

    // Return default
    return default_value;
}

template<typename T>
std::optional<T> ConfigurationManager::GetFromTree(const boost::property_tree::ptree& tree, const std::string& path) const {
    try {
        return tree.get<T>(path);
    } catch (const boost::property_tree::ptree_error&) {
        // Too Noise - Turn OFF - GS_LOG_MSG(trace, "GetFromTree failed for path: " + path);
        return std::nullopt;
    }
}

template<typename T>
void ConfigurationManager::SetInTree(boost::property_tree::ptree& tree, const std::string& path, const T& value) const {
    tree.put(path, value);
}

} // namespace golf_sim