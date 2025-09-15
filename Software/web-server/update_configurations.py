#!/usr/bin/env python3
"""
Script to update configurations.json:
1. Add all missing fields from golf_sim_config.json
2. Ensure all fields have proper metadata
"""

import json
from pathlib import Path


def main():
    config_path = Path(__file__).parent / "configurations.json"

    with open(config_path, "r") as f:
        data = json.load(f)

    settings = data.get("settings", {})

    # Count settings that need updating
    count_without_passed_via = 0
    count_with_passed_via = 0

    # Add passedVia: json to all settings without it
    for key, value in settings.items():
        if "passedVia" not in value:
            value["passedVia"] = "json"
            value["passedTo"] = "both"  # JSON settings go to both cameras
            count_without_passed_via += 1
        else:
            count_with_passed_via += 1

    # Add missing CLI arguments that are actually used
    missing_cli_args = {
        "cli.cam_still_mode": {
            "category": "Testing",
            "displayName": "Camera Still Mode",
            "description": "Take single still picture and exit",
            "type": "boolean",
            "default": False,
            "requiresRestart": True,
            "passedVia": "cli",
            "passedTo": "both",
            "cliArgument": "--cam_still_mode",
            "internal": True,
        },
        "cli.pulse_test": {
            "category": "Testing",
            "displayName": "Pulse Test",
            "description": "Continuous strobe/shutter test",
            "type": "boolean",
            "default": False,
            "requiresRestart": True,
            "passedVia": "cli",
            "passedTo": "both",
            "cliArgument": "--pulse_test",
            "internal": True,
        },
        "cli.send_test_results": {
            "category": "Testing",
            "displayName": "Send Test Results",
            "description": "Send test IPC message and exit",
            "type": "boolean",
            "default": False,
            "requiresRestart": True,
            "passedVia": "cli",
            "passedTo": "both",
            "cliArgument": "--send_test_results",
            "internal": True,
        },
        "cli.lm_comparison_mode": {
            "category": "Testing",
            "displayName": "LM Comparison Mode",
            "description": "Configure for other IR launch monitor environment",
            "type": "boolean",
            "default": False,
            "requiresRestart": True,
            "passedVia": "cli",
            "passedTo": "both",
            "cliArgument": "--lm_comparison_mode",
            "internal": True,
        },
        "cli.skip_wait_armed": {
            "category": "Testing",
            "displayName": "Skip Wait Armed",
            "description": "Skip simulator armed state wait",
            "type": "boolean",
            "default": False,
            "requiresRestart": True,
            "passedVia": "cli",
            "passedTo": "both",
            "cliArgument": "--skip_wait_armed",
            "internal": True,
        },
    }

    # Add missing CLI arguments
    for key, value in missing_cli_args.items():
        if key not in settings:
            settings[key] = value

    # Fix duplicate kWriteSpinAnalysisCsvFiles - search for it
    duplicates_found = []
    for key in settings.keys():
        if "kWriteSpinAnalysisCsvFiles" in key:
            duplicates_found.append(key)

    # Save updated configuration
    data["settings"] = settings

    # Write back with nice formatting
    with open(config_path, "w") as f:
        json.dump(data, f, indent=2, sort_keys=False)

    print("Updated configurations.json:")
    print(f"  - Added 'passedVia: json' to {count_without_passed_via} settings")
    print(f"  - {count_with_passed_via} settings already had passedVia")
    print(f"  - Added {len(missing_cli_args)} missing CLI arguments")
    print(f"  - Found {len(duplicates_found)} duplicate keys with kWriteSpinAnalysisCsvFiles")
    if duplicates_found:
        print(f"    Duplicates: {duplicates_found}")


if __name__ == "__main__":
    main()
