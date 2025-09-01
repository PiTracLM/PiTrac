
# Initialize global flags and logging (libraries are embedded by bashly)
initialize_global_flags


key="${args[key]}"
value="${args[value]}"
use_global="${args[--global]:-}"

if [[ "$use_global" == "1" ]]; then
  config_file="/etc/pitrac/pitrac.yaml"
  if [[ $EUID -ne 0 ]]; then
    error "Setting global config requires sudo"
    exit 1
  fi
else
  config_file="${USER_CONFIG:-$DEFAULT_CONFIG}"
fi

log_warn "Manual edit required. Use 'pitrac config edit' to set:"
echo "$key: $value"
