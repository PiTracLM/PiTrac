#!/usr/bin/env bash
# lib/global_flags.sh - Handle global CLI flags for PiTrac
# This library manages global flags like --verbose, --debug, --info, --trace
# and ensures consistent logging behavior across all commands

# Source logging library if not already loaded
if [[ -z "${LOGGING_LOADED:-}" ]]; then
    if [[ -f "${BASH_SOURCE%/*}/logging.sh" ]]; then
        source "${BASH_SOURCE%/*}/logging.sh"
    fi
fi

# Setup global logging based on CLI flags
# Priority order (highest to lowest): --trace > --debug > --info/--verbose > default
setup_global_logging() {
    local log_level_set=false
    
    # Check for trace flag first (most verbose - level 0)
    if [[ "${args[--trace]:-}" == "1" ]]; then
        export LOG_LEVEL=$LOG_LEVEL_TRACE
        log_trace "Trace logging enabled via --trace flag"
        log_level_set=true
    # Then debug (level 1)
    elif [[ "${args[--debug]:-}" == "1" ]]; then
        export LOG_LEVEL=$LOG_LEVEL_DEBUG
        log_debug "Debug logging enabled via --debug flag"
        log_level_set=true
    # Then info (level 2)
    elif [[ "${args[--info]:-}" == "1" ]]; then
        export LOG_LEVEL=$LOG_LEVEL_INFO
        log_info "Info logging enabled via --info flag"
        log_level_set=true
    # Then verbose (also maps to info level)
    elif [[ "${args[--verbose]:-}" == "1" ]]; then
        export LOG_LEVEL=$LOG_LEVEL_INFO
        log_info "Verbose logging enabled via --verbose flag"
        log_level_set=true
    fi
    
    # If no flag was set, use environment variable or default to WARN
    if [[ "$log_level_set" == "false" ]]; then
        if [[ -n "${PITRAC_LOG_LEVEL:-}" ]]; then
            case "${PITRAC_LOG_LEVEL}" in
                trace|TRACE) export LOG_LEVEL=$LOG_LEVEL_TRACE ;;
                debug|DEBUG) export LOG_LEVEL=$LOG_LEVEL_DEBUG ;;
                info|INFO) export LOG_LEVEL=$LOG_LEVEL_INFO ;;
                warn|WARN) export LOG_LEVEL=$LOG_LEVEL_WARN ;;
                error|ERROR) export LOG_LEVEL=$LOG_LEVEL_ERROR ;;
                *) export LOG_LEVEL=$LOG_LEVEL_WARN ;;
            esac
            log_debug "Log level set from PITRAC_LOG_LEVEL environment variable: $PITRAC_LOG_LEVEL"
        else
            # Default to WARN level for normal operation
            export LOG_LEVEL=$LOG_LEVEL_WARN
        fi
    fi
    
    log_trace "Effective log level: $LOG_LEVEL"
}

setup_config_file() {
    if [[ -n "${args[--config]:-}" ]]; then
        export PITRAC_CONFIG="${args[--config]}"
        log_debug "Using configuration file from --config flag: ${args[--config]}"
        
        if [[ ! -f "${PITRAC_CONFIG}" ]]; then
            log_warn "Configuration file does not exist: ${PITRAC_CONFIG}"
        else
            log_info "Using configuration file: ${PITRAC_CONFIG}"
        fi
    fi
}

build_pitrac_logging_args() {
    local -n cmd_args_ref=$1
    
    if [[ "${args[--trace]:-}" == "1" ]]; then
        cmd_args_ref+=("--logging_level=trace")
        log_trace "Setting PiTrac binary logging to trace"
    elif [[ "${args[--debug]:-}" == "1" ]]; then
        cmd_args_ref+=("--logging_level=debug")
        log_debug "Setting PiTrac binary logging to debug"
    elif [[ "${args[--info]:-}" == "1" ]] || [[ "${args[--verbose]:-}" == "1" ]]; then
        cmd_args_ref+=("--logging_level=info")
        log_info "Setting PiTrac binary logging to info"
    else
        # Default to info for the binary (it has its own defaults)
        cmd_args_ref+=("--logging_level=info")
    fi
}

initialize_global_flags() {
    setup_global_logging

    setup_config_file

    log_trace "Executing script: ${BASH_SOURCE[1]:-unknown}"
    log_trace "Command: ${args[command]:-unknown}"
    
    if [[ $LOG_LEVEL -le $LOG_LEVEL_TRACE ]]; then
        log_trace "All arguments:"
        for key in "${!args[@]}"; do
            log_trace "  args[$key]='${args[$key]}'"
        done
    fi
}

export -f setup_global_logging
export -f setup_config_file
export -f build_pitrac_logging_args
export -f initialize_global_flags

export GLOBAL_FLAGS_LOADED=1