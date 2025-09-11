"""Configuration Manager for PiTrac Web Server

Handles reading and writing JSON configuration files with a two-tier system:
1. System defaults: /etc/pitrac/golf_sim_config.json (read-only)
2. User overrides: ~/.pitrac/config/user_settings.json (read-write, sparse)
"""

import json
import logging
import os
import shutil
from datetime import datetime
from pathlib import Path
from typing import Any, Dict, List, Optional, Tuple

logger = logging.getLogger(__name__)


class ConfigurationManager:
    """Manages PiTrac configuration with JSON-based system"""

    def __init__(self):
        self._raw_metadata = self._load_raw_metadata()
        sys_paths = self._raw_metadata.get("systemPaths", {})
        
        def expand_path(path_str: str) -> Path:
            return Path(path_str.replace("~", str(Path.home())))
        
        self.system_config_path = Path(sys_paths.get("systemConfigPath", {}).get("default", "/etc/pitrac/golf_sim_config.json"))
        self.user_settings_path = expand_path(sys_paths.get("userSettingsPath", {}).get("default", "~/.pitrac/config/user_settings.json"))
        self.backup_dir = expand_path(sys_paths.get("backupDirectory", {}).get("default", "~/.pitrac/backups"))

        self.system_config: Dict[str, Any] = {}
        self.user_settings: Dict[str, Any] = {}
        self.merged_config: Dict[str, Any] = {}

        self.restart_required_params = self._load_restart_required_params()

        self.reload()

    def _load_raw_metadata(self) -> Dict[str, Any]:
        """Load raw metadata from configurations.json without processing"""
        try:
            config_path = os.path.join(os.path.dirname(__file__), "configurations.json")
            with open(config_path, "r") as f:
                return json.load(f)
        except Exception as e:
            logger.error(f"Error loading configurations.json: {e}")
            return {"settings": {}}
    
    def _load_restart_required_params(self) -> set:
        """Load parameters that require restart from configurations.json metadata"""
        metadata = self._raw_metadata if hasattr(self, '_raw_metadata') else self._load_raw_metadata()
        settings_metadata = metadata.get("settings", {})

        restart_params = set()
        for key, setting_info in settings_metadata.items():
            if setting_info.get("requiresRestart", False):
                restart_params.add(key)

        logger.info(f"Loaded {len(restart_params)} parameters that require restart")
        return restart_params

    def reload(self) -> None:
        """Reload all configuration files"""
        self.system_config = self._load_json(self.system_config_path)
        self.user_settings = self._load_json(self.user_settings_path)
        self.merged_config = self._merge_configs()
        self.restart_required_params = self._load_restart_required_params()
        logger.info(f"Loaded configuration: {len(self.user_settings)} user overrides")

    def _load_json(self, path: Path) -> Dict[str, Any]:
        """Load JSON file safely"""
        if not path.exists():
            return {}

        try:
            with open(path, "r") as f:
                return json.load(f)
        except (json.JSONDecodeError, IOError) as e:
            logger.error(f"Failed to load {path}: {e}")
            return {}

    def _save_json(self, path: Path, data: Dict[str, Any]) -> bool:
        """Save JSON file with proper formatting"""
        try:
            path.parent.mkdir(parents=True, exist_ok=True)

            temp_path = path.with_suffix(".tmp")
            with open(temp_path, "w") as f:
                json.dump(data, f, indent=2, sort_keys=True)

            temp_path.replace(path)
            logger.info(f"Saved configuration to {path}")
            return True

        except (IOError, OSError) as e:
            logger.error(f"Failed to save {path}: {e}")
            return False

    def _merge_configs(self) -> Dict[str, Any]:
        """Merge system defaults with user overrides"""

        def deep_merge(base: Dict, override: Dict) -> Dict:
            """Recursively merge override into base"""
            result = base.copy()
            for key, value in override.items():
                if (
                    key in result
                    and isinstance(result[key], dict)
                    and isinstance(value, dict)
                ):
                    result[key] = deep_merge(result[key], value)
                else:
                    result[key] = value
            return result

        return deep_merge(self.system_config, self.user_settings)

    def get_config(self, key: Optional[str] = None) -> Any:
        """Get configuration value or entire config

        Args:
            key: Dot-notation path (e.g., 'gs_config.cameras.kCamera1Gain')
                 If None, returns entire merged config

        Returns:
            Configuration value or None if not found
        """
        if key is None:
            return self.get_merged_with_metadata_defaults()

        value = self.get_merged_with_metadata_defaults()
        for part in key.split("."):
            if isinstance(value, dict) and part in value:
                value = value[part]
            else:
                return None

        return value
    
    def get_merged_with_metadata_defaults(self) -> Dict[str, Any]:
        """Get merged config including metadata-defined defaults"""
        result = self.merged_config.copy()
        
        metadata = self.load_configurations_metadata()
        settings_metadata = metadata.get("settings", {})
        
        for key, meta in settings_metadata.items():
            if "default" in meta:
                parts = key.split(".")
                current = result
                exists = True
                
                for i, part in enumerate(parts):
                    if i == len(parts) - 1:
                        if part not in current:
                            exists = False
                    else:
                        if part not in current:
                            current[part] = {}
                        current = current[part]
                
                if not exists:
                    current[parts[-1]] = meta["default"]
        
        return result

    def get_default(self, key: Optional[str] = None) -> Any:
        """Get default system configuration value or metadata default"""
        if key is None:
            return self.get_all_defaults_with_metadata()

        value = self.system_config
        parts = key.split(".")
        found = True
        
        for part in parts:
            if isinstance(value, dict) and part in value:
                value = value[part]
            else:
                found = False
                break
        
        if found:
            return value
            
        metadata = self.load_configurations_metadata()
        settings_metadata = metadata.get("settings", {})
        if key in settings_metadata and "default" in settings_metadata[key]:
            return settings_metadata[key]["default"]
        
        return None
    
    def get_all_defaults_with_metadata(self) -> Dict[str, Any]:
        """Get all defaults including metadata-defined defaults"""
        defaults = self.system_config.copy()
        
        metadata = self.load_configurations_metadata()
        settings_metadata = metadata.get("settings", {})
        
        for key, meta in settings_metadata.items():
            if "default" in meta:
                parts = key.split(".")
                current = defaults
                
                for part in parts[:-1]:
                    if part not in current:
                        current[part] = {}
                    current = current[part]
                
                final_key = parts[-1]
                if final_key not in current:
                    current[final_key] = meta["default"]
        
        return defaults

    def get_user_settings(self) -> Dict[str, Any]:
        """Get only user overrides"""
        return self.user_settings.copy()

    def set_config(self, key: str, value: Any) -> Tuple[bool, str, bool]:
        """Set configuration value

        Args:
            key: Dot-notation path
            value: New value

        Returns:
            Tuple of (success, message, requires_restart)
        """
        default_value = self.get_default(key)

        if value == default_value:
            if self._delete_from_dict(self.user_settings, key):
                self._save_json(self.user_settings_path, self.user_settings)
                self.reload()
                return (
                    True,
                    f"Reset {key} to default value",
                    key in self.restart_required_params,
                )
            return True, "Value already at default", False

        if self._set_in_dict(self.user_settings, key, value):
            self._backup_config()

            if self._save_json(self.user_settings_path, self.user_settings):
                self.reload()
                requires_restart = key in self.restart_required_params
                return True, f"Set {key} = {value}", requires_restart

            return False, "Failed to save configuration", False

        return False, "Failed to set value", False

    def _set_in_dict(self, d: Dict[str, Any], key: str, value: Any) -> bool:
        """Set value in nested dictionary using dot notation"""
        parts = key.split(".")
        current = d

        for part in parts[:-1]:
            if part not in current:
                current[part] = {}
            elif not isinstance(current[part], dict):
                return False
            current = current[part]

        current[parts[-1]] = value
        return True

    def _delete_from_dict(self, d: Dict[str, Any], key: str) -> bool:
        """Delete value from nested dictionary using dot notation"""
        parts = key.split(".")
        current = d

        for part in parts[:-1]:
            if isinstance(current, dict) and part in current:
                current = current[part]
            else:
                return False  # Key doesn't exist

        if isinstance(current, dict) and parts[-1] in current:
            del current[parts[-1]]

            self._cleanup_empty_dicts(d)
            return True

        return False

    def _cleanup_empty_dicts(self, d: Dict[str, Any]) -> None:
        """Remove empty nested dictionaries"""
        keys_to_delete = []

        for key, value in d.items():
            if isinstance(value, dict):
                self._cleanup_empty_dicts(value)
                if not value:  # Empty dict
                    keys_to_delete.append(key)

        for key in keys_to_delete:
            del d[key]

    def reset_all(self) -> Tuple[bool, str]:
        """Reset all user settings to defaults"""
        self._backup_config()

        self.user_settings = {}

        if self._save_json(self.user_settings_path, self.user_settings):
            self.reload()
            return True, "Reset all settings to defaults"

        return False, "Failed to reset configuration"

    def _backup_config(self) -> Optional[Path]:
        """Create backup of current user settings"""
        if not self.user_settings:
            return None

        try:
            self.backup_dir.mkdir(parents=True, exist_ok=True)

            timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
            backup_path = self.backup_dir / f"user_settings_{timestamp}.json"

            shutil.copy2(self.user_settings_path, backup_path)
            logger.info(f"Created backup: {backup_path}")
            return backup_path

        except (IOError, OSError) as e:
            logger.error(f"Failed to create backup: {e}")
            return None

    def get_diff(self) -> Dict[str, Any]:
        """Get differences between user settings and defaults

        Returns:
            Dictionary showing what's different from defaults
        """
        diff = {}

        def compare_nested(user: Dict, default: Dict, path: str = "") -> None:
            for key, value in user.items():
                current_path = f"{path}.{key}" if path else key

                if key not in default:
                    diff[current_path] = {"user": value, "default": None}
                elif isinstance(value, dict) and isinstance(default.get(key), dict):
                    compare_nested(value, default[key], current_path)
                elif value != default.get(key):
                    diff[current_path] = {"user": value, "default": default[key]}

        compare_nested(self.user_settings, self.system_config)
        return diff

    def validate_config(self, key: str, value: Any) -> Tuple[bool, str]:
        """Validate configuration value

        Args:
            key: Configuration key
            value: Value to validate

        Returns:
            Tuple of (is_valid, error_message)
        """
        # Use load_configurations_metadata() to get metadata with dynamic options
        metadata = self.load_configurations_metadata()
        settings_metadata = metadata.get("settings", {})
        validation_rules = metadata.get("validationRules", {})
        
        if key in settings_metadata:
            setting_info = settings_metadata[key]
            setting_type = setting_info.get("type", "")
            
            if setting_type == "select" and "options" in setting_info:
                if key == "gs_config.ball_identification.kONNXModelPath":
                    available_models = self.get_available_models()
                    if available_models:
                        valid_options = list(available_models.values())
                        str_value = str(value)
                        if str_value not in valid_options:
                            return False, f"Must be one of: {', '.join(available_models.keys())}"
                    return True, ""
                else:
                    valid_options = list(setting_info["options"].keys())
                    str_value = str(value)
                    if str_value not in valid_options:
                        return False, f"Must be one of: {', '.join(valid_options)}"
            
            elif setting_type == "boolean":
                if not isinstance(value, bool) and value not in [True, False, "true", "false"]:
                    return False, "Must be true or false"
            
            elif setting_type == "number":
                try:
                    num_val = float(value)
                    if "min" in setting_info and num_val < setting_info["min"]:
                        return False, f"Must be at least {setting_info['min']}"
                    if "max" in setting_info and num_val > setting_info["max"]:
                        return False, f"Must be at most {setting_info['max']}"
                except (TypeError, ValueError):
                    return False, "Must be a number"
            
            return True, ""
        
        for pattern, rule in validation_rules.items():
            if pattern.lower() in key.lower():
                if rule["type"] == "range":
                    try:
                        val = float(value) if pattern == "gain" else int(value)
                        if not rule["min"] <= val <= rule["max"]:
                            return False, rule["errorMessage"]
                    except (TypeError, ValueError):
                        return False, rule["errorMessage"]
                elif rule["type"] == "string":
                    if value and not isinstance(value, str):
                        return False, rule["errorMessage"]
                return True, ""

        return True, ""

    def get_available_models(self) -> Dict[str, str]:
        """
        Discover available YOLO models from the models directory.
        Returns a dict of {display_name: path} for dropdown options.
        """
        models = {}
        metadata = self._raw_metadata if hasattr(self, '_raw_metadata') else self._load_raw_metadata()
        sys_paths = metadata.get("systemPaths", {})
        
        model_search_paths = sys_paths.get("modelSearchPaths", {}).get("default", [])
        model_file_patterns = sys_paths.get("modelFilePatterns", {}).get("default", [])
        
        model_dirs = []
        for path_str in model_search_paths:
            path = Path(path_str.replace("~", str(Path.home())))
            model_dirs.append(path)

        for base_dir in model_dirs:
            if not base_dir.exists():
                continue

            for model_dir in base_dir.iterdir():
                if model_dir.is_dir():
                    onnx_paths = []
                    for pattern in model_file_patterns:
                        onnx_paths.append(model_dir / pattern)

                    for onnx_path in onnx_paths:
                        if onnx_path.exists():
                            display_name = model_dir.name
                            try:
                                relative_path = onnx_path.relative_to(Path.home())
                                path_str = f"~/{relative_path}"
                            except ValueError:
                                path_str = str(onnx_path)

                            models[display_name] = path_str
                            break

        return dict(sorted(models.items()))

    def load_configurations_metadata(self):
        """
        Load configuration metadata from configurations.json
        """
        try:
            config_path = os.path.join(os.path.dirname(__file__), "configurations.json")
            with open(config_path, "r") as f:
                metadata = json.load(f)

            model_options = self.get_available_models()
            if model_options and "settings" in metadata:
                model_key = "gs_config.ball_identification.kONNXModelPath"
                if model_key in metadata["settings"]:
                    metadata["settings"][model_key]["options"] = model_options

            return metadata
        except Exception as e:
            print(f"Error loading configurations.json: {e}")
            return {"settings": {}}

    def get_cli_parameters(self, target: str = "both") -> List[Dict[str, Any]]:
        """Get all CLI parameters for a specific target (camera1, camera2, both)
        
        Args:
            target: Target to filter by ('camera1', 'camera2', or 'both')
            
        Returns:
            List of CLI parameter metadata dictionaries
        """
        metadata = self.load_configurations_metadata()
        settings = metadata.get("settings", {})
        
        cli_params = []
        for key, info in settings.items():
            if info.get("passedVia") == "cli":
                passed_to = info.get("passedTo", "both")
                if passed_to == target or passed_to == "both" or target == "both":
                    cli_params.append({
                        "key": key,
                        "cliArgument": info.get("cliArgument"),
                        "passedTo": passed_to,
                        "type": info.get("type"),
                        "default": info.get("default")
                    })
        return cli_params
    
    def get_environment_parameters(self, target: str = "both") -> List[Dict[str, Any]]:
        """Get all environment parameters for a specific target
        
        Args:
            target: Target to filter by ('camera1', 'camera2', or 'both')
            
        Returns:
            List of environment parameter metadata dictionaries
        """
        metadata = self.load_configurations_metadata()
        settings = metadata.get("settings", {})
        
        env_params = []
        for key, info in settings.items():
            if info.get("passedVia") == "environment":
                passed_to = info.get("passedTo", "both")
                if passed_to == target or passed_to == "both" or target == "both":
                    env_params.append({
                        "key": key,
                        "envVariable": info.get("envVariable"),
                        "passedTo": passed_to,
                        "type": info.get("type"),
                        "default": info.get("default")
                    })
        return env_params

    def flatten_config(
        self, config: Dict[str, Any], prefix: str = ""
    ) -> Dict[str, Any]:
        """Flatten nested config dict into dot-notation keys."""
        result = {}
        for key, value in config.items():
            full_key = f"{prefix}.{key}" if prefix else key
            if isinstance(value, dict):
                result.update(self.flatten_config(value, full_key))
            else:
                result[full_key] = value
        return result

    def get_categories(self) -> Dict[str, List[str]]:
        """Get configuration organized by categories based on configurations.json

        Returns:
            Dictionary with category names and their parameters
        """
        metadata = self.load_configurations_metadata()
        settings_metadata = metadata.get("settings", {})
        category_list = metadata.get("categoryList", [
            "Basic", "Cameras", "Simulators", "Ball Detection", 
            "AI Detection", "Storage", "Network", "Logging",
            "Strobing", "Spin Analysis", "Calibration", "Advanced"
        ])

        categories = {cat: [] for cat in category_list}

        processed_keys = set()

        for key, setting_info in settings_metadata.items():
            processed_keys.add(key)
            if setting_info.get("showInBasic", False):
                categories["Basic"].append(key)
            category = setting_info.get("category", "Advanced")
            if category in categories:
                categories[category].append(key)

        for key in self.flatten_config(self.merged_config).keys():
            if key not in processed_keys:
                category = self.auto_categorize_key(key)
                categories[category].append(key)

        categories = {k: v for k, v in categories.items() if v}

        return categories

    def auto_categorize_key(self, key: str) -> str:
        """Auto-categorize keys using metadata-driven rules."""
        metadata = self._raw_metadata if hasattr(self, '_raw_metadata') else self._load_raw_metadata()
        rules = metadata.get("autoCategorizationRules", [])
        
        for rule in rules:
            patterns = rule.get("pattern", [])
            category = rule.get("category", "Advanced")
            case_sensitive = rule.get("caseSensitive", False)
            exclude = rule.get("exclude", [])
            
            if exclude:
                if any(excl in key.lower() for excl in exclude):
                    continue
            
            for pattern in patterns:
                if case_sensitive:
                    if pattern in key:
                        return category
                else:
                    if pattern.lower() in key.lower():
                        return category
        
        return "Advanced"

    def get_basic_subcategories(self):
        """Get subcategories for Basic settings."""
        metadata = self.load_configurations_metadata()
        settings_metadata = metadata.get("settings", {})

        subcategories = {}
        for key, setting_info in settings_metadata.items():
            if setting_info.get("showInBasic", False):
                subcat = setting_info.get("basicSubcategory", "Other")
                if subcat not in subcategories:
                    subcategories[subcat] = []
                subcategories[subcat].append(key)

        return subcategories

    def export_config(self) -> Dict[str, Any]:
        """Export current configuration for backup/sharing"""
        return {
            "version": "1.0",
            "exported_at": datetime.now().isoformat(),
            "user_settings": self.user_settings,
            "system_version": self.system_config.get("version", "unknown"),
        }

    def import_config(self, config_data: Dict[str, Any]) -> Tuple[bool, str]:
        """Import configuration from exported data"""
        if "user_settings" not in config_data:
            return False, "Invalid configuration format"

        self._backup_config()

        self.user_settings = config_data["user_settings"]

        if self._save_json(self.user_settings_path, self.user_settings):
            self.reload()
            return True, "Configuration imported successfully"

        return False, "Failed to import configuration"
