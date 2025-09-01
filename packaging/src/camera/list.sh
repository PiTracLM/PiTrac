
# Initialize global flags and logging (libraries are embedded by bashly)
initialize_global_flags


detailed="${args[--detailed]:-}"

echo "=== Available Cameras ==="
echo ""

camera_cmd=$(get_camera_command)

if command -v "$camera_cmd" >/dev/null 2>&1; then
  if [[ "$detailed" == "1" ]]; then
    $camera_cmd --list-cameras
  else
    get_camera_slots
  fi
else
  error "Camera command not found: $camera_cmd"
  exit 1
fi
