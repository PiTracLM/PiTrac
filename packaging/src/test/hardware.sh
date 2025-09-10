initialize_global_flags


skip_camera="${args[--skip-camera]:-}"
skip_gpio="${args[--skip-gpio]:-}"

echo "=== Hardware Test ==="
echo ""

# Test Pi detection
echo "1. Raspberry Pi Detection:"
if is_raspberry_pi; then
  success "Running on Raspberry Pi ($(detect_pi_model))"
else
  warn "Not running on Raspberry Pi"
fi

# Test GPIO
if [[ "$skip_gpio" != "1" ]]; then
  echo ""
  echo "2. GPIO Test:"
  if is_gpio_available; then
    success "GPIO available (chip $(get_gpio_chip))"
  else
    error "GPIO not available"
  fi
fi

# Test cameras
if [[ "$skip_camera" != "1" ]]; then
  echo ""
  echo "3. Camera Test:"
  if check_camera_availability; then
    success "Camera(s) detected"
    get_camera_slots
  else
    error "No cameras detected"
  fi
fi

# Test services
echo ""
echo "4. Service Test:"
if is_service_running "activemq"; then
  success "ActiveMQ running"
else
  warn "ActiveMQ not running"
fi

if is_service_running "pitrac-web"; then
  success "PiTrac web server running"
else
  warn "PiTrac web server not running"
fi
