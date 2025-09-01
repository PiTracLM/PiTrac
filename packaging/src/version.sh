
initialize_global_flags


show_build="${args[--build]:-}"

echo "PiTrac Launch Monitor"
echo "Version: 1.0.0"

if [[ "$show_build" == "1" ]]; then
  echo "Build: Bashly Migration"
  echo "Platform: Raspberry Pi OS Bookworm (64-bit)"
  echo "Architecture: ARM64"
  echo "Generated: $(date '+%Y-%m-%d')"
fi
