action="${args[action]}"
follow="${args[--follow]:-}"

case "$action" in
  start)
    info "Starting PiTrac web server..."
    # Check which server is installed
    if [[ -f /etc/systemd/system/pitrac-web.service ]]; then
      start_service "pitrac-web"
    else
      error "No web server service found"
      exit 1
    fi
    ;;
  stop)
    info "Stopping web server..."
    if systemctl is-active --quiet pitrac-web; then
      stop_service "pitrac-web"
    else
      warn "No web server is running"
    fi
    ;;
  restart)
    info "Restarting web server..."
    if [[ -f /etc/systemd/system/pitrac-web.service ]]; then
      restart_service "pitrac-web"
    else
      error "No web server service found"
      exit 1
    fi
    ;;
  status)
    if [[ -f /etc/systemd/system/pitrac-web.service ]]; then
      get_service_status "pitrac-web"
    else
      error "No web server service found"
      exit 1
    fi
    ;;
  logs)
    # Determine which service to check
    local service_name=""
    if [[ -f /etc/systemd/system/pitrac-web.service ]]; then
      service_name="pitrac-web"
    else
      error "No web server service found"
      exit 1
    fi
    
    if [[ "$follow" == "1" ]]; then
      get_service_logs "$service_name" "true" "50"
    else
      get_service_logs "$service_name" "false" "50"
    fi
    ;;
  url)
    echo "Web Dashboard URL: http://localhost:8080"
    echo ""
    echo "Access from another device on the network:"
    local ip_addr=$(hostname -I | awk '{print $1}')
    if [[ -n "$ip_addr" ]]; then
      echo "  http://${ip_addr}:8080"
    fi
    ;;
  *)
    error "Unknown action: $action"
    exit 1
    ;;
esac