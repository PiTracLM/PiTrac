initialize_global_flags


show_json="${args[--json]:-}"
show_services="${args[--services]:-}"

if [[ "$show_json" == "1" ]]; then
  # JSON output
  cat <<EOF
{
  "pitrac": {
    "running": $(is_pitrac_running && echo "true" || echo "false"),
    "pid": $(is_pitrac_running && cat "$PITRAC_PID_FILE" 2>/dev/null || echo "null")
  },
  "services": {
    "pitrac-web": $(is_service_running "pitrac-web" && echo "true" || echo "false"),
    "activemq": $(is_service_running "activemq" && echo "true" || echo "false")
  },
  "hardware": {
    "pi_model": "$(detect_pi_model)",
    "gpio_available": $(is_gpio_available && echo "true" || echo "false"),
    "cameras": $(check_camera_availability && echo "true" || echo "false")
  }
}
EOF
else
  # Human-readable output
  echo "=== PiTrac Status ==="
  echo ""
  
  echo "Main Process:"
  if is_pitrac_running; then
    pid=$(cat "$PITRAC_PID_FILE" 2>/dev/null)
    success "  PiTrac is running (PID: $pid)"
  else
    warn "  PiTrac is not running"
  fi
  
  if [[ "$show_services" == "1" ]] || [[ "$show_services" != "1" ]]; then
    echo ""
    echo "Services:"
    if is_service_running "pitrac-web"; then
      success "  PiTrac web server is running"
    else
      warn "  PiTrac web server is not running"
    fi
    
    if is_service_running "activemq"; then
      success "  ActiveMQ broker is running"
    else
      warn "  ActiveMQ broker is not running"
    fi
  fi
  
  echo ""
  echo "Hardware:"
  echo "  Pi Model: $(detect_pi_model)"
  
  if is_gpio_available; then
    success "  GPIO: Available (chip $(get_gpio_chip))"
  else
    warn "  GPIO: Not available"
  fi
  
  if check_camera_availability; then
    success "  Cameras: Detected"
  else
    warn "  Cameras: Not detected"
  fi
fi
