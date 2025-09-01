
# Initialize global flags and logging (libraries are embedded by bashly)
initialize_global_flags


duration="${args[duration]:-5}"
camera_slot="${args[--camera]:-}"

echo "=== Camera Test ==="
echo "Testing camera for ${duration} seconds..."
echo ""

camera_cmd=$(get_camera_command)

if command -v "$camera_cmd" >/dev/null 2>&1; then
  if [[ -n "$camera_slot" ]]; then
    $camera_cmd --camera "$camera_slot" -t "$((duration * 1000))"
  else
    $camera_cmd -t "$((duration * 1000))"
  fi
else
  error "Camera command not found: $camera_cmd"
  exit 1
fi
