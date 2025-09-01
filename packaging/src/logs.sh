initialize_global_flags


follow="${args[--follow]:-}"
tail_lines="${args[--tail]:-50}"
show_service="${args[--service]:-}"
show_all="${args[--all]:-}"

if [[ "$show_service" == "1" ]]; then
  get_service_logs "pitrac" "$follow" "$tail_lines"
elif [[ "$show_all" == "1" ]]; then
  if [[ -f "$PITRAC_LOG_FILE" ]]; then
    cat "$PITRAC_LOG_FILE"
  elif [[ -f "/tmp/pitrac.log" ]]; then
    cat "/tmp/pitrac.log"
  else
    error "No log file found"
  fi
elif [[ "$follow" == "1" ]]; then
  if [[ -f "$PITRAC_LOG_FILE" ]]; then
    tail -f "$PITRAC_LOG_FILE"
  elif [[ -f "/tmp/pitrac.log" ]]; then
    tail -f "/tmp/pitrac.log"
  else
    error "No log file found"
  fi
else
  if [[ -f "$PITRAC_LOG_FILE" ]]; then
    tail -n "$tail_lines" "$PITRAC_LOG_FILE"
  elif [[ -f "/tmp/pitrac.log" ]]; then
    tail -n "$tail_lines" "/tmp/pitrac.log"
  else
    error "No log file found"
  fi
fi
