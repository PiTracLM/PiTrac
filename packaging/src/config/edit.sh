
# Initialize global flags and logging (libraries are embedded by bashly)
initialize_global_flags


editor="${args[--editor]:-${EDITOR:-nano}}"
config_file="${USER_CONFIG:-$DEFAULT_CONFIG}"

if [[ ! -f "$config_file" ]]; then
  error "Configuration file not found: $config_file"
  exit 1
fi

"$editor" "$config_file"
log_info "Configuration saved. Run 'pitrac config validate' to check for errors"
