
# Initialize global flags and logging (libraries are embedded by bashly)
initialize_global_flags


action="${args[action]}"
follow="${args[--follow]:-}"

case "$action" in
  start)
    info "Starting ActiveMQ broker..."
    start_service "activemq"
    ;;
  stop)
    info "Stopping ActiveMQ broker..."
    stop_service "activemq"
    ;;
  restart)
    info "Restarting ActiveMQ broker..."
    restart_service "activemq"
    ;;
  status)
    get_service_status "activemq"
    ;;
  logs)
    if [[ "$follow" == "1" ]]; then
      get_service_logs "activemq" "true" "50"
    else
      get_service_logs "activemq" "false" "50"
    fi
    ;;
  console)
    echo "ActiveMQ Web Console: http://localhost:8161/admin"
    echo "Default credentials: admin/admin"
    ;;
  port)
    echo "ActiveMQ broker port: 61616"
    echo "ActiveMQ web console port: 8161"
    ;;
  *)
    error "Unknown action: $action"
    exit 1
    ;;
esac
