#!/bin/sh
SCRIPT_DIR="$(dirname "$(readlink -f "$0")")"
exec "$SCRIPT_DIR/imx296_trigger" 4 0
