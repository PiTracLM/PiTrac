initialize_global_flags


log_warn "The 'tomee' command is deprecated. Please use 'pitrac web' instead."
echo ""

action="${args[action]}"
follow="${args[--follow]:-}"

case "$action" in
  start)
    info "Starting TomEE web server..."
    start_service "tomee"
    ;;
  stop)
    info "Stopping TomEE web server..."
    stop_service "tomee"
    ;;
  restart)
    info "Restarting TomEE web server..."
    restart_service "tomee"
    ;;
  status)
    get_service_status "tomee"
    ;;
  deploy)
    deploy_webapp_if_needed
    ;;
  logs)
    if [[ "$follow" == "1" ]]; then
      get_service_logs "tomee" "true" "50"
    else
      get_service_logs "tomee" "false" "50"
    fi
    ;;
  *)
    error "Unknown action: $action"
    exit 1
    ;;
esac
