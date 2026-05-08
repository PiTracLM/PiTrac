#!/bin/sh
TRIGGER_MODE=/sys/module/imx296/parameters/trigger_mode
echo 0 > "$TRIGGER_MODE"
