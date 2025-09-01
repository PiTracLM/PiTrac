
# Initialize global flags and logging (libraries are embedded by bashly)
initialize_global_flags


echo "=== Calibration Wizard ==="
echo ""

wizard="/usr/lib/pitrac/calibration-wizard"

if [[ -f "$wizard" ]]; then
  exec "$wizard" "$@"
else
  warn "Calibration wizard not installed"
  echo "Use 'pitrac calibrate camera --slot 1' or 'pitrac calibrate camera --slot 2'"
fi
