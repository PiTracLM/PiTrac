"""Configuration Manager for PiTrac Web Server

Builds configuration from a three-tier system:
1. Generated defaults: From configurations.json metadata
2. Calibration data: calibration table in the SQLite database
3. User overrides: settings table in the SQLite database (sparse)

~/.pitrac/config/user_settings.json and calibration_data.json are imported once at
startup into whichever of those tables is still empty, then renamed to *.imported.
"""

import copy
import ipaddress
import json
import logging
import math
import os
import re
from datetime import datetime
from pathlib import Path
from threading import RLock
from typing import Any, Callable, Dict, List, Optional, Tuple

from constants import DB_PATH
from db.database import Database
from db.repositories import KeyValueRepository

logger = logging.getLogger(__name__)

_OLD_SIM = "gs_config.golf_simulator_interfaces"
_RENAMED_SETTINGS = {
    f"{_OLD_SIM}.GSPro.kGSProConnectAddress": "simulators.gspro.host",
    f"{_OLD_SIM}.GSPro.kGSProConnectPort": "simulators.gspro.port",
    f"{_OLD_SIM}.E6.kE6ConnectAddress": "simulators.e6.host",
    f"{_OLD_SIM}.E6.kE6ConnectPort": "simulators.e6.port",
    f"{_OLD_SIM}.E6.kE6InterMessageDelayMs": "simulators.e6.inter_message_delay_ms",
    f"{_OLD_SIM}.kLaunchMonitorIdString": None,
}
_MODEL_KINDS = {
    "gs_config.ball_identification.kModelPath": "ball",
    "gs_config.spin_analysis.kSpinModelPath": "spin",
}


def _deep_merge(base: Dict, override: Dict) -> Dict:
    """Recursively merge override into base"""
    result = base.copy()
    for key, value in override.items():
        if key in result and isinstance(result[key], dict) and isinstance(value, dict):
            result[key] = _deep_merge(result[key], value)
        else:
            result[key] = value
    return result


def _set_in_dict(d: Dict[str, Any], key: str, value: Any) -> bool:
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


def _flatten(nested: Dict[str, Any], prefix: str = "") -> Dict[str, Any]:
    flat = {}
    for key, value in nested.items():
        full_key = f"{prefix}.{key}" if prefix else key
        if isinstance(value, dict):
            flat.update(_flatten(value, full_key))
        else:
            flat[full_key] = value
    return flat


def _without_dotted_keys(nested: Dict[str, Any], dropped: List[str], prefix: str = "") -> Dict[str, Any]:
    clean = {}
    for key, value in nested.items():
        full_key = f"{prefix}.{key}" if prefix else key
        if "." in key:
            dropped.append(full_key)
        elif isinstance(value, dict):
            clean[key] = _without_dotted_keys(value, dropped, full_key)
        else:
            clean[key] = value
    return clean


def _as_integer(value: Any) -> Any:
    """Truncate toward zero like stoi did; boost's integer parse rejects "35.5" outright."""
    try:
        return int(float(value))
    except (TypeError, ValueError, OverflowError):
        return value


_HOSTNAME_LABEL = re.compile(r"[A-Za-z0-9]([A-Za-z0-9-]{0,61}[A-Za-z0-9])?")
HOST_ERROR = "Enter an IP address or hostname without a port"


def _is_valid_host(host: str) -> bool:
    if host == "":
        return True
    try:
        ipaddress.IPv4Address(host)
        return True
    except ValueError:
        pass
    labels = host.split(".")
    return len(host) <= 253 and not labels[-1].isdigit() and all(_HOSTNAME_LABEL.fullmatch(label) for label in labels)


def coerce(setting_type: str, value: Any) -> Any:
    """Convert a submitted value to a metadata type; raises ValueError with a plain message."""
    if setting_type in ("select", "string", "text", "path", "ip_address"):
        if value is None or isinstance(value, (dict, list)):
            raise ValueError("Must be text")
        return str(value).strip() if setting_type == "ip_address" else str(value)

    if setting_type == "boolean":
        # The generated config writes booleans as "1" and "0"
        if isinstance(value, str) and value.lower() in ("true", "false", "1", "0"):
            return value.lower() in ("true", "1")
        if value in (True, False):
            return bool(value)
        raise ValueError("Must be true or false")

    if setting_type in ("integer", "number", "float"):
        if isinstance(value, bool) or value is None:
            raise ValueError("Must be a number")
        try:
            number = float(value)
            if not math.isfinite(number):
                raise ValueError
            if setting_type == "integer":
                return int(number)
            if setting_type == "number" and number.is_integer():
                return int(number)
            return number
        except (TypeError, ValueError, OverflowError):
            raise ValueError("Must be a number") from None

    if setting_type == "array" and isinstance(value, str):
        try:
            return json.loads(value)
        except json.JSONDecodeError:
            raise ValueError("Invalid JSON array format") from None

    return value


def _unflatten(flat: Dict[str, Any]) -> Dict[str, Any]:
    nested: Dict[str, Any] = {}
    for key, value in flat.items():
        _set_in_dict(nested, key, value)
    return nested


class ConfigurationManager:
    """Manages PiTrac configuration stored in SQLite"""

    def __init__(self, db: Optional[Database] = None):
        self._lock = RLock()
        self._metadata_cache = None

        self._raw_metadata = self._load_raw_metadata()
        sys_paths = self._raw_metadata.get("systemPaths", {})

        def expand_path(path_str: str) -> Path:
            return Path(path_str).expanduser()

        # Legacy JSON files, read only to import them into the database
        self.user_settings_path = expand_path(
            sys_paths.get("userSettingsPath", {}).get("default", "~/.pitrac/config/user_settings.json")
        )
        self.calibration_data_path = expand_path("~/.pitrac/config/calibration_data.json")

        self.user_settings: Dict[str, Any] = {}
        self.calibration_data: Dict[str, Any] = {}
        self.merged_config: Dict[str, Any] = {}
        self.transient_overrides: Dict[str, Any] = {}

        self.restart_required_params = self._load_restart_required_params()

        self._config_callbacks: Dict[str, List[Callable[[str, Any], None]]] = {}

        self._db = db or Database(DB_PATH)
        self._settings = KeyValueRepository(self._db, "settings")
        self._calibration = KeyValueRepository(self._db, "calibration")

        self.found_legacy_json = self.user_settings_path.exists() or self.calibration_data_path.exists()
        self._import_json(self._settings, self.user_settings_path)
        self._import_json(self._calibration, self.calibration_data_path)
        self._move_renamed_keys()
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
            self._metadata_cache = None
            self.user_settings = _unflatten(self._settings.load())
            self.calibration_data = _unflatten(self._calibration.load())
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

    def _load_json(self, path: Path) -> Optional[Dict[str, Any]]:
        """Load a JSON object, or None if the file is unreadable or holds something else"""
        try:
            with open(path, "r") as f:
                data = json.load(f)
        except (OSError, ValueError) as e:
            logger.error(f"Failed to load {path}: {e}")
            return None
        return data if isinstance(data, dict) else None

    def _import_json(self, repo: KeyValueRepository, path: Path) -> None:
        """Import a legacy JSON file into an empty table, then rename it"""
        if not path.exists():
            return
        if repo.load():
            logger.info(f"Not importing {path}: the {repo.table} table already has values")
            return

        data = self._load_json(path)
        if data is None:
            logger.warning(f"Not importing {path}: it is not a readable JSON object, keeping the stored values")
            return

        # get_config never resolved dotted keys, so importing them would change behavior
        dotted: List[str] = []
        data = _without_dotted_keys(data, dotted)
        if dotted:
            logger.info(f"Dropping dotted keys from {path}: {', '.join(dotted)}")

        repo.replace_all(_flatten(data))

        target = path.with_name(path.name + ".imported")
        if target.exists():
            stamped = f"{target.name}.{datetime.now():%Y%m%d-%H%M%S}"
            target = path.with_name(stamped)
            n = 0
            while target.exists():
                n += 1
                target = path.with_name(f"{stamped}.{n}")
        try:
            path.rename(target)
        except OSError as e:
            # The rows are written and a re-import is idempotent, so failing here would only crash-loop the service
            logger.error(f"Imported {path} but could not rename it to {target}, will import it again next start: {e}")
            return
        logger.info(f"Imported {path} into the {repo.table} table, renamed to {target.name}")

    def _move_renamed_keys(self) -> None:
        """Move stored settings to their renamed keys in one write; an old sim address also meant the sim was on"""
        stored = self._settings.load()
        old_keys = [key for key in _RENAMED_SETTINGS if key in stored]
        if not old_keys:
            return

        moved = {key: value for key, value in stored.items() if key not in _RENAMED_SETTINGS}
        for old in old_keys:
            new, value = _RENAMED_SETTINGS[old], stored[old]
            if new is None:
                logger.info(f"Dropping stored setting {old}={value!r}, nothing reads it any more")
                continue
            if new in stored:
                logger.info(f"Dropping stored setting {old}={value!r}, {new} is already set")
                continue
            moved[new] = value
            logger.info(f"Moved stored setting {old} to {new}")
            if new.endswith(".host") and isinstance(value, str) and value.strip():
                moved.setdefault(f"{new.rsplit('.', 1)[0]}.enabled", True)

        self._settings.replace_all(moved)

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

        # Apply calibration data (persistent layer)
        config = _deep_merge(config, self.calibration_data)

        # Then apply user overrides (highest priority)
        return _deep_merge(config, self.user_settings)

    def register_callback(self, key_pattern: str, callback: Callable[[str, Any], None]) -> None:
        """Register callback for configuration updates matching pattern

        Args:
            key_pattern: Pattern to match (e.g., 'gs_config.cameras' matches all camera configs)
            callback: Function to call with (key, value) when config updates
        """
        with self._lock:
            if key_pattern not in self._config_callbacks:
                self._config_callbacks[key_pattern] = []
            self._config_callbacks[key_pattern].append(callback)
            logger.debug(f"Registered callback for pattern: {key_pattern}")

    def unregister_callback(self, key_pattern: str, callback: Callable[[str, Any], None]) -> None:
        """Unregister a specific callback

        Args:
            key_pattern: Pattern the callback was registered with
            callback: The callback function to remove
        """
        with self._lock:
            if key_pattern in self._config_callbacks:
                try:
                    self._config_callbacks[key_pattern].remove(callback)
                    if not self._config_callbacks[key_pattern]:
                        del self._config_callbacks[key_pattern]
                    logger.debug(f"Unregistered callback for pattern: {key_pattern}")
                except ValueError:
                    pass

    def _notify_callbacks(self, key: str, value: Any) -> None:
        """Notify registered callbacks of configuration change

        This is called after successful config save, OUTSIDE the config lock,
        to prevent deadlock if callbacks call back into ConfigurationManager.

        Args:
            key: The configuration key that was updated
            value: The new value
        """
        with self._lock:
            callbacks_snapshot = [(pattern, list(cbs)) for pattern, cbs in self._config_callbacks.items()]

        for pattern, callbacks in callbacks_snapshot:
            if key.startswith(pattern) or pattern == "*":
                for callback in callbacks:
                    try:
                        callback(key, value)
                    except Exception as e:
                        logger.error(f"Callback error for {key}: {e}", exc_info=True)

    def _notify_changed(self, before: Dict[str, Any], after: Dict[str, Any]) -> None:
        before, after = _flatten(before), _flatten(after)
        for key in sorted(before.keys() | after.keys()):
            if before.get(key) != after.get(key):
                self._notify_callbacks(key, after.get(key))

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

    def get_calibrated_keys(self) -> List[str]:
        with self._lock:
            return sorted(_flatten(self.calibration_data))

    def calibration_updated_at(self, key: str) -> Optional[str]:
        """When a saved calibration value last changed, or None if it was never saved"""
        return self._calibration.updated_at(key)

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

    def coerce_value(self, key: str, value: Any) -> Any:
        """Convert a submitted value to the type its metadata declares.

        Raises ValueError with a plain message when it cannot be converted.
        Keys without metadata pass through unchanged.
        """
        return coerce(self.load_configurations_metadata().get("settings", {}).get(key, {}).get("type", ""), value)

    def set_config(self, key: str, value: Any) -> Tuple[bool, str, bool]:
        """Set configuration value

        Args:
            key: Dot-notation path
            value: New value

        Returns:
            Tuple of (success, message, requires_restart)
        """
        notify_key = None
        notify_value = None
        result = None

        with self._lock:
            default_value = self.get_default(key)
            is_calibration = self._is_calibration_field(key)

            is_valid, error_msg = self.validate_config(key, value)
            if not is_valid:
                return False, error_msg, False
            value = self.coerce_value(key, value)

            if value == default_value:
                if is_calibration:
                    calibration_copy = copy.deepcopy(self.calibration_data)
                    if self._delete_from_dict(calibration_copy, key):
                        self._calibration.replace_all(_flatten(calibration_copy))
                        self.calibration_data = calibration_copy
                        self._rebuild_merged_config()
                        notify_key = key
                        notify_value = default_value
                        result = (
                            True,
                            f"Reset calibration {key} to default value",
                            key in self.restart_required_params,
                        )
                else:
                    settings_copy = copy.deepcopy(self.user_settings)
                    if self._delete_from_dict(settings_copy, key):
                        self._settings.replace_all(_flatten(settings_copy))
                        self.user_settings = settings_copy
                        self._rebuild_merged_config()
                        notify_key = key
                        notify_value = default_value
                        result = (
                            True,
                            f"Reset {key} to default value",
                            key in self.restart_required_params,
                        )
                if result is None:
                    result = (True, "Value already at default", False)

            elif is_calibration:
                calibration_copy = copy.deepcopy(self.calibration_data)
                if _set_in_dict(calibration_copy, key, value):
                    self._calibration.replace_all(_flatten(calibration_copy))
                    self.calibration_data = calibration_copy
                    self._rebuild_merged_config()
                    notify_key = key
                    notify_value = value
                    requires_restart = key in self.restart_required_params
                    result = (True, f"Set calibration {key} = {value}", requires_restart)
            else:
                settings_copy = copy.deepcopy(self.user_settings)
                if _set_in_dict(settings_copy, key, value):
                    self._settings.replace_all(_flatten(settings_copy))
                    self.user_settings = settings_copy
                    self._rebuild_merged_config()
                    notify_key = key
                    notify_value = value
                    requires_restart = key in self.restart_required_params
                    result = (True, f"Set {key} = {value}", requires_restart)

            if result is None:
                result = (False, "Failed to set value", False)

        if notify_key is not None:
            self._notify_callbacks(notify_key, notify_value)

        return result

    def set_calibration_batch(self, updates: Dict[str, Any]) -> Tuple[bool, str]:
        """Atomic multi-key write to calibration data. All keys must be calibration fields."""
        if not updates:
            return True, "No updates"

        with self._lock:
            for key in updates:
                if not self._is_calibration_field(key):
                    return False, f"Key {key} is not a calibration field"

            calibration_copy = copy.deepcopy(self.calibration_data)
            for key, value in updates.items():
                if not _set_in_dict(calibration_copy, key, value):
                    return False, f"Failed to set {key}"

            self._calibration.replace_all(_flatten(calibration_copy))
            self.calibration_data = calibration_copy
            self._rebuild_merged_config()

            for key, value in updates.items():
                self._notify_callbacks(key, value)

        return True, f"Saved {len(updates)} calibration values"

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
        with self._lock:
            before = self.merged_config
            self._settings.replace_all({})
            self.user_settings = {}
            self._rebuild_merged_config()
            after = self.merged_config
        self._notify_changed(before, after)
        return True, "Reset all settings to defaults"

    def get_diff(self) -> Dict[str, Any]:
        """Get differences between user settings and defaults

        Returns:
            Dictionary showing what's different from defaults, with source indicator
        """
        with self._lock:
            diff = {}

            def compare_nested(overrides: Dict, default: Dict, source: str, path: str = "") -> None:
                for key, value in overrides.items():
                    current_path = f"{path}.{key}" if path else key

                    if key not in default:
                        diff[current_path] = {"user": value, "default": None, "source": source}
                    elif isinstance(value, dict) and isinstance(default.get(key), dict):
                        compare_nested(value, default[key], source, current_path)
                    elif value != default.get(key):
                        diff[current_path] = {"user": value, "default": default[key], "source": source}

            default_config = self.get_all_defaults_with_metadata()
            compare_nested(self.user_settings, default_config, "user")
            compare_nested(self.calibration_data, default_config, "calibration")
            return diff

    def validate_config(self, key: str, value: Any) -> Tuple[bool, str]:
        """Validate configuration value

        Args:
            key: Configuration key
            value: Value to validate

        Returns:
            Tuple of (is_valid, error_message)
        """
        try:
            value = self.coerce_value(key, value)
        except ValueError as e:
            return False, str(e)

        with self._lock:
            metadata = self.load_configurations_metadata()
            settings_metadata = metadata.get("settings", {})
            validation_rules = metadata.get("validationRules", {})

        if key in settings_metadata:
            setting_info = settings_metadata[key]
            setting_type = setting_info.get("type", "")

            if setting_type == "select" and "options" in setting_info:
                if key in _MODEL_KINDS:
                    available_models = self.get_available_models(_MODEL_KINDS[key])
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

            elif setting_type == "ip_address":
                if not _is_valid_host(value):
                    return False, HOST_ERROR

            elif setting_type in ("number", "integer", "float"):
                try:
                    num_val = float(value)
                    if "min" in setting_info and num_val < setting_info["min"]:
                        return False, f"Must be at least {setting_info['min']}"
                    if "max" in setting_info and num_val > setting_info["max"]:
                        return False, f"Must be at most {setting_info['max']}"
                except (TypeError, ValueError):
                    return False, "Must be a number"

            elif setting_type == "array":
                if not isinstance(value, list):
                    return False, "Must be an array"

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

    def build_generated_config(self) -> Dict[str, Any]:
        """Build the merged config dict served to pitrac_lm (defaults + calibration + user settings)."""
        config = {}
        metadata = self.load_configurations_metadata()
        settings_metadata = metadata.get("settings", {})

        if not settings_metadata:
            raise RuntimeError("No settings found in configurations metadata")

        json_settings_count = 0
        for key, setting_info in settings_metadata.items():
            value = self.get_config(key)
            if setting_info.get("type") == "integer":
                value = _as_integer(value)
            if value is not None:
                self._set_nested_json(config, key, value)
                json_settings_count += 1

        if json_settings_count == 0:
            raise RuntimeError("No JSON settings found to generate config")

        return _deep_merge(config, self.transient_overrides)

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
            if str_value.startswith("~"):
                str_value = str(Path(str_value).expanduser())
            current[final_key] = str_value

    def get_available_models(self, kind: str) -> Dict[str, str]:
        """
        Discover available YOLO models of one kind ("ball" or "spin") from the models directory.
        A directory whose name contains "spin" holds a spin model; every other one holds a ball model.
        Returns a dict of {display_name: model_dir_path} for dropdown options.
        The C++ backend loads NCNN model files from the selected directory.
        """
        models = {}
        metadata = self._raw_metadata if hasattr(self, "_raw_metadata") else self._load_raw_metadata()
        sys_paths = metadata.get("systemPaths", {})

        model_search_paths = sys_paths.get("modelSearchPaths", {}).get("default", [])
        model_file_patterns = sys_paths.get("modelFilePatterns", {}).get("default", [])

        model_dirs = []
        for path_str in model_search_paths:
            path = Path(path_str).expanduser()
            model_dirs.append(path)

        for base_dir in model_dirs:
            if not base_dir.exists():
                continue

            for model_dir in base_dir.iterdir():
                if model_dir.is_dir() and ("spin" in model_dir.name.lower()) == (kind == "spin"):
                    for pattern in model_file_patterns:
                        if (model_dir / pattern).exists():
                            display_name = model_dir.name
                            if display_name not in models:
                                models[display_name] = str(model_dir.resolve())
                            break

        return dict(sorted(models.items()))

    def load_configurations_metadata(self):
        """
        Load configuration metadata from configurations.json
        """
        if self._metadata_cache is not None:
            return self._metadata_cache

        try:
            config_path = os.path.join(os.path.dirname(__file__), "configurations.json")
            with open(config_path, "r") as f:
                metadata = json.load(f)

            for model_key, kind in _MODEL_KINDS.items():
                model_options = self.get_available_models(kind)
                if model_options and model_key in metadata.get("settings", {}):
                    metadata["settings"][model_key]["options"] = model_options

            self._metadata_cache = metadata
            return metadata
        except Exception as e:
            logger.error(f"Error loading configurations.json: {e}")
            return {"settings": {}}

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

            if category in categories and not setting_info.get("internal"):
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
            "kDAC_setting",
        ]
        return any(pattern in key for pattern in calibration_patterns)

    # Auto-categorization removed - all items must have explicit categories

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

    def _coerce_imported(self, nested: Dict[str, Any]) -> Dict[str, Any]:
        """Coerce and validate every known key of an imported tree; raises ValueError naming the key."""
        flat = {}
        for key, value in _flatten(nested).items():
            # Simulators live in their own table; an old export's keys would migrate into a duplicate instance
            if key.startswith("simulators."):
                logger.info(f"Not importing {key}, simulators are added from the navbar now")
                continue
            is_valid, error = self.validate_config(key, value)
            if not is_valid:
                raise ValueError(f"{key}: {error}")
            flat[key] = self.coerce_value(key, value)
        return _unflatten(flat)

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

                new_user = None
                new_cal = None

                if isinstance(import_data.get("user_settings"), dict):
                    new_user = self._coerce_imported(import_data["user_settings"])

                if isinstance(import_data.get("calibration_data"), dict):
                    new_cal = self._coerce_imported(import_data["calibration_data"])

                with self._db.transaction() as conn:
                    if new_user is not None:
                        self._settings.replace_all_in(conn, _flatten(new_user))
                    if new_cal is not None:
                        self._calibration.replace_all_in(conn, _flatten(new_cal))

                if new_user is not None:
                    self.user_settings = new_user
                if new_cal is not None:
                    self.calibration_data = new_cal

                before = self.merged_config
                self._rebuild_merged_config()
                after = self.merged_config

            except ValueError as e:
                return False, str(e)
            except Exception as e:
                logger.error(f"Error importing configuration: {e}")
                return False, f"Import failed: {e}"

        self._notify_changed(before, after)
        return True, "Configuration imported successfully"
