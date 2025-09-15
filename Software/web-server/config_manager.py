"""Configuration Manager for PiTrac Web Server

Handles reading and writing JSON configuration files with a three-tier system:
1. Generated defaults: From configurations.json metadata
2. Calibration data: ~/.pitrac/config/calibration_data.json (preserved across regenerations)
3. User overrides: ~/.pitrac/config/user_settings.json (read-write, sparse)
"""

import json
import logging
import os
import fcntl
import copy
from pathlib import Path
from threading import RLock
from typing import Any, Dict, List, Optional, Tuple

logger = logging.getLogger(__name__)


class ConfigurationManager:
    """Manages PiTrac configuration with JSON-based system"""

    def __init__(self):
        self._lock = RLock()

        self._raw_metadata = self._load_raw_metadata()
        sys_paths = self._raw_metadata.get("systemPaths", {})

        def expand_path(path_str: str) -> Path:
            return Path(path_str.replace("~", str(Path.home())))

        # Configuration paths for three-tier system
        self.user_settings_path = expand_path(
            sys_paths.get("userSettingsPath", {}).get("default", "~/.pitrac/config/user_settings.json")
        )
        self.calibration_data_path = expand_path("~/.pitrac/config/calibration_data.json")

        self.user_settings: Dict[str, Any] = {}
        self.calibration_data: Dict[str, Any] = {}
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
        metadata = self._raw_metadata if hasattr(self, "_raw_metadata") else self._load_raw_metadata()
        settings_metadata = metadata.get("settings", {})

        restart_params = set()
        for key, setting_info in settings_metadata.items():
            if setting_info.get("requiresRestart", False):
                restart_params.add(key)

        logger.info(f"Loaded {len(restart_params)} parameters that require restart")
        return restart_params

    def reload(self) -> None:
        """Reload configuration from metadata, calibration data, and user settings"""
        with self._lock:
            self.user_settings = self._load_json(self.user_settings_path)
            self.calibration_data = self._load_json(self.calibration_data_path)
            # Build merged config from metadata defaults + calibration + user overrides
            self.merged_config = self._build_config_from_metadata()
            self.restart_required_params = self._load_restart_required_params()
            logger.info(
                f"Loaded configuration: {len(self.calibration_data)} calibration fields, {len(self.user_settings)} user overrides"
            )

    def _rebuild_merged_config(self) -> None:
        """Rebuild merged config from current state (internal use, assumes lock is held)"""
        # This method is called from within locked methods, so no lock needed here
        self.merged_config = self._build_config_from_metadata()
        self.restart_required_params = self._load_restart_required_params()

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
        """Save JSON file with proper formatting and file locking"""
        try:
            path.parent.mkdir(parents=True, exist_ok=True)

            data_copy = copy.deepcopy(data)

            temp_path = path.with_suffix(".tmp")
            with open(temp_path, "w") as f:
                fcntl.flock(f.fileno(), fcntl.LOCK_EX)
                try:
                    json.dump(data_copy, f, indent=2, sort_keys=True)
                    f.flush()
                    os.fsync(f.fileno())
                finally:
                    fcntl.flock(f.fileno(), fcntl.LOCK_UN)

            temp_path.replace(path)
            logger.info(f"Saved configuration to {path}")
            return True

        except (IOError, OSError) as e:
            logger.error(f"Failed to save {path}: {e}")
            return False

    def _build_config_from_metadata(self) -> Dict[str, Any]:
        """Build configuration from metadata defaults, calibration data, and user overrides"""
        config = {}
        metadata = self.load_configurations_metadata()
        settings_metadata = metadata.get("settings", {})

        # First, add all defaults from metadata
        for key, setting_info in settings_metadata.items():
            if "default" in setting_info:
                parts = key.split(".")
                current = config

                # Navigate/create nested structure
                for part in parts[:-1]:
                    if part not in current:
                        current[part] = {}
                    current = current[part]

                # Set the default value
                current[parts[-1]] = setting_info["default"]

        # Helper function for deep merging
        def deep_merge(base: Dict, override: Dict) -> Dict:
            """Recursively merge override into base"""
            result = base.copy()
            for key, value in override.items():
                if key in result and isinstance(result[key], dict) and isinstance(value, dict):
                    result[key] = deep_merge(result[key], value)
                else:
                    result[key] = value
            return result

        # Apply calibration data (persistent layer)
        config = deep_merge(config, self.calibration_data)

        # Then apply user overrides (highest priority)
        return deep_merge(config, self.user_settings)

    def get_config(self, key: Optional[str] = None) -> Any:
        """Get configuration value or entire config

        Args:
            key: Dot-notation path (e.g., 'gs_config.cameras.kCamera1Gain')
                 If None, returns entire merged config

        Returns:
            Configuration value or None if not found
        """
        with self._lock:
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
        """Get merged config (already includes metadata defaults)"""
        with self._lock:
            return copy.deepcopy(self.merged_config)

    def get_default(self, key: Optional[str] = None) -> Any:
        """Get default value from metadata"""
        if key is None:
            return self.get_all_defaults_with_metadata()

        metadata = self.load_configurations_metadata()
        settings_metadata = metadata.get("settings", {})
        if key in settings_metadata and "default" in settings_metadata[key]:
            return settings_metadata[key]["default"]

        return None

    def get_all_defaults_with_metadata(self) -> Dict[str, Any]:
        """Get all defaults from metadata"""
        defaults = {}

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
        with self._lock:
            return copy.deepcopy(self.user_settings)

    def set_config(self, key: str, value: Any) -> Tuple[bool, str, bool]:
        """Set configuration value

        Args:
            key: Dot-notation path
            value: New value

        Returns:
            Tuple of (success, message, requires_restart)
        """
        with self._lock:
            default_value = self.get_default(key)
            is_calibration = self._is_calibration_field(key)

            if value == default_value:
                if is_calibration:
                    calibration_copy = copy.deepcopy(self.calibration_data)
                    if self._delete_from_dict(calibration_copy, key):
                        if self._save_json(self.calibration_data_path, calibration_copy):
                            self.calibration_data = calibration_copy
                            self._rebuild_merged_config()
                            return (
                                True,
                                f"Reset calibration {key} to default value",
                                key in self.restart_required_params,
                            )
                else:
                    settings_copy = copy.deepcopy(self.user_settings)
                    if self._delete_from_dict(settings_copy, key):
                        if self._save_json(self.user_settings_path, settings_copy):
                            self.user_settings = settings_copy
                            self._rebuild_merged_config()
                            return (
                                True,
                                f"Reset {key} to default value",
                                key in self.restart_required_params,
                            )
                return True, "Value already at default", False

            if is_calibration:
                calibration_copy = copy.deepcopy(self.calibration_data)
                if self._set_in_dict(calibration_copy, key, value):
                    if self._save_json(self.calibration_data_path, calibration_copy):
                        self.calibration_data = calibration_copy
                        self._rebuild_merged_config()
                        requires_restart = key in self.restart_required_params
                        return True, f"Set calibration {key} = {value}", requires_restart
                    return False, "Failed to save calibration data", False
            else:
                settings_copy = copy.deepcopy(self.user_settings)
                if self._set_in_dict(settings_copy, key, value):
                    if self._save_json(self.user_settings_path, settings_copy):
                        self.user_settings = settings_copy
                        self._rebuild_merged_config()
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

    def _cleanup_empty_dicts(self, d: Dict[str, Any], max_depth: int = 100, current_depth: int = 0) -> None:
        """Remove empty nested dictionaries

        Args:
            d: Dictionary to clean up
            max_depth: Maximum recursion depth (default 100)
            current_depth: Current recursion depth
        """
        if current_depth >= max_depth:
            logger.warning(f"Maximum recursion depth {max_depth} reached in _cleanup_empty_dicts")
            return

        keys_to_delete = []

        for key, value in d.items():
            if isinstance(value, dict):
                self._cleanup_empty_dicts(value, max_depth, current_depth + 1)
                if not value:  # Empty dict
                    keys_to_delete.append(key)

        for key in keys_to_delete:
            del d[key]

    def reset_all(self) -> Tuple[bool, str]:
        """Reset all user settings to defaults"""

        self.user_settings = {}

        if self._save_json(self.user_settings_path, self.user_settings):
            self.reload()
            return True, "Reset all settings to defaults"

        return False, "Failed to reset configuration"

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

        default_config = self.get_all_defaults_with_metadata()
        compare_nested(self.user_settings, default_config)
        return diff

    def validate_config(self, key: str, value: Any) -> Tuple[bool, str]:
        """Validate configuration value

        Args:
            key: Configuration key
            value: Value to validate

        Returns:
            Tuple of (is_valid, error_message)
        """
        with self._lock:
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

    def generate_golf_sim_config(self) -> Path:
        """Generate golf_sim_config.json from configurations metadata and user settings

        This method creates a complete golf_sim_config.json file by:
        1. Taking all settings marked with passedVia: "json"
        2. Getting their values (default + user overrides)
        3. Building the nested JSON structure expected by pitrac_lm

        Returns:
            Path to the generated configuration file

        Raises:
            RuntimeError: If generation fails
        """
        try:
            config = {}
            metadata = self.load_configurations_metadata()
            settings_metadata = metadata.get("settings", {})

            if not settings_metadata:
                raise RuntimeError("No settings found in configurations metadata")

            # Process all settings and build the JSON structure
            json_settings_count = 0
            for key, setting_info in settings_metadata.items():
                # Skip non-JSON routed settings
                passed_via = setting_info.get("passedVia", "json")  # Default to json if not specified
                if passed_via in ["cli", "environment"]:
                    continue

                # Get the merged value (default + calibration + user override)
                value = self.get_config(key)
                if value is not None:
                    # Build nested structure from dot notation key
                    self._set_nested_json(config, key, value)
                    json_settings_count += 1

            if json_settings_count == 0:
                raise RuntimeError("No JSON settings found to generate config")

            # Save to generated location
            generated_path = self.user_settings_path.parent / "generated_golf_sim_config.json"
            if not self._save_json(generated_path, config):
                raise RuntimeError(f"Failed to save generated config to {generated_path}")

            logger.info(f"Generated golf_sim_config.json with {json_settings_count} settings at {generated_path}")
            return generated_path

        except Exception as e:
            logger.error(f"Failed to generate golf_sim_config.json: {e}")
            raise RuntimeError(f"Config generation failed: {e}")

    def _set_nested_json(self, config: dict, key: str, value: Any):
        """Set value in nested JSON structure based on dot notation key

        Args:
            config: The config dict to modify
            key: Dot notation key (e.g., "gs_config.cameras.kCamera1Gain")
            value: The value to set
        """
        parts = key.split(".")
        current = config

        # Navigate/create the nested structure
        for part in parts[:-1]:
            if part not in current:
                current[part] = {}
            current = current[part]

        # Set the final value
        final_key = parts[-1]

        # Convert boolean values to "0" or "1" strings for compatibility
        if isinstance(value, bool):
            current[final_key] = "1" if value else "0"
        elif value is None:
            # Don't set None values
            return
        elif isinstance(value, (list, dict)):
            # Preserve arrays and objects as-is (for calibration matrices, etc.)
            current[final_key] = value
        else:
            # Convert to string and expand paths with ~
            str_value = str(value)
            if "~/" in str_value:
                str_value = str_value.replace("~", str(Path.home()))
            current[final_key] = str_value

    def get_available_models(self) -> Dict[str, str]:
        """
        Discover available YOLO models from the models directory.
        Returns a dict of {display_name: path} for dropdown options.
        """
        models = {}
        metadata = self._raw_metadata if hasattr(self, "_raw_metadata") else self._load_raw_metadata()
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
                    cli_params.append(
                        {
                            "key": key,
                            "cliArgument": info.get("cliArgument"),
                            "passedTo": passed_to,
                            "type": info.get("type"),
                            "default": info.get("default"),
                        }
                    )
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
                    env_params.append(
                        {
                            "key": key,
                            "envVariable": info.get("envVariable"),
                            "passedTo": passed_to,
                            "type": info.get("type"),
                            "default": info.get("default"),
                        }
                    )
        return env_params

    def flatten_config(self, config: Dict[str, Any], prefix: str = "") -> Dict[str, Any]:
        """Flatten nested config dict into dot-notation keys."""
        result = {}
        for key, value in config.items():
            full_key = f"{prefix}.{key}" if prefix else key
            if isinstance(value, dict):
                result.update(self.flatten_config(value, full_key))
            else:
                result[full_key] = value
        return result

    def get_categories(self) -> Dict[str, Dict[str, List[str]]]:
        """Get configuration organized by categories with basic/advanced subcategories

        Returns:
            Dictionary with category names containing basic and advanced settings
        """
        metadata = self.load_configurations_metadata()
        settings_metadata = metadata.get("settings", {})
        category_list = metadata.get(
            "categoryList",
            [
                "Cameras",
                "Simulators",
                "Ball Detection",
                "AI Detection",
                "Storage",
                "Network",
                "Logging",
                "Strobing",
                "Spin Analysis",
                "Calibration",
                "System",
                "Testing",
                "Debugging",
                "Club Data",
                "Display",
            ],
        )

        # Initialize categories with basic and advanced subcategories
        categories = {cat: {"basic": [], "advanced": []} for cat in category_list}

        processed_keys = set()

        for key, setting_info in settings_metadata.items():
            processed_keys.add(key)
            category = setting_info.get("category", "Advanced")

            # Determine if this is a basic or advanced setting
            subcategory = setting_info.get("subcategory", "advanced")

            if category in categories:
                categories[category][subcategory].append(key)

        # No auto-categorization - all items must have explicit categories

        # Remove empty categories
        categories = {k: v for k, v in categories.items() if v["basic"] or v["advanced"]}

        return categories

    def _is_calibration_field(self, key: str) -> bool:
        """Check if a field is calibration-related and should be persisted separately

        Args:
            key: Configuration key to check

        Returns:
            True if this is a calibration field
        """
        calibration_patterns = [
            "CalibrationMatrix",
            "DistortionVector",
            "Camera1Angles",
            "Camera2Angles",
            "Camera1FocalLength",
            "Camera2FocalLength",
            "Camera1Positions",
            "Camera2Positions",
            "Camera1Offset",
            "Camera2Offset",
            "calibration.",
            "kAutoCalibration",
            "_ENCLOSURE_",
        ]
        return any(pattern in key for pattern in calibration_patterns)

    # Auto-categorization removed - all items must have explicit categories

    def get_basic_subcategories(self):
        """DEPRECATED: Use get_categories() instead which now includes subcategories."""
        # Return empty dict for backward compatibility
        return {}

    def export_config(self) -> Dict[str, Any]:
        """Export current configuration for backup or sharing

        Returns:
            Dictionary containing user settings and calibration data
        """
        with self._lock:
            export_data = {
                "user_settings": copy.deepcopy(self.user_settings),
                "calibration_data": copy.deepcopy(self.calibration_data),
                "metadata": {"exported_at": "", "version": "1.0"},  # Could add timestamp if needed
            }
            return export_data

    def import_config(self, import_data: Dict[str, Any]) -> Tuple[bool, str]:
        """Import configuration from exported data

        Args:
            import_data: Dictionary with user_settings and optional calibration_data

        Returns:
            Tuple of (success, message)
        """
        with self._lock:
            try:
                if not isinstance(import_data, dict):
                    return False, "Import data must be a dictionary"

                if "user_settings" in import_data:
                    new_user_settings = import_data["user_settings"]
                    if isinstance(new_user_settings, dict):
                        self.user_settings = copy.deepcopy(new_user_settings)
                        if not self._save_json(self.user_settings_path, self.user_settings):
                            return False, "Failed to save imported user settings"

                if "calibration_data" in import_data:
                    new_calibration_data = import_data["calibration_data"]
                    if isinstance(new_calibration_data, dict):
                        self.calibration_data = copy.deepcopy(new_calibration_data)
                        if not self._save_json(self.calibration_data_path, self.calibration_data):
                            return False, "Failed to save imported calibration data"

                self._rebuild_merged_config()

                return True, "Configuration imported successfully"

            except Exception as e:
                logger.error(f"Error importing configuration: {e}")
                return False, f"Import failed: {e}"
