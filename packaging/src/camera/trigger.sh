
# Initialize global flags and logging (libraries are embedded by bashly)
initialize_global_flags


mode="${args[mode]}"
camera_slot="${args[--camera]:-}"

echo "Setting camera trigger mode to: $mode"

script_base="/usr/lib/pitrac/ImageProcessing/CameraTools"

if [[ "$mode" == "external" ]]; then
  script="$script_base/setCameraTriggerExternal.sh"
elif [[ "$mode" == "internal" ]]; then
  script="$script_base/setCameraTriggerInternal.sh"
else
  error "Invalid trigger mode: $mode"
  exit 1
fi

if [[ -f "$script" ]]; then
  if [[ $EUID -eq 0 ]]; then
    "$script"
  else
    sudo "$script"
  fi
  success "Camera trigger mode set to $mode"
else
  error "Trigger script not found: $script"
  exit 1
fi
