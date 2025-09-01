initialize_global_flags


action="${args[action]}"

case "$action" in
  start)
    info "Starting PiTrac service..."
    start_service "pitrac"
    ;;
  stop)
    info "Stopping PiTrac service..."
    stop_service "pitrac"
    ;;
  restart)
    info "Restarting PiTrac service..."
    restart_service "pitrac"
    ;;
  status)
    get_service_status "pitrac"
    ;;
  enable)
    info "Enabling PiTrac service..."
    enable_service "pitrac"
    ;;
  disable)
    info "Disabling PiTrac service..."
    disable_service "pitrac"
    ;;
  *)
    error "Unknown action: $action"
    exit 1
    ;;
esac
