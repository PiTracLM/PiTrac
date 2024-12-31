# SPDX-License-Identifier: GPL-2.0-only */
#
# Copyright (C) 2022-2025, Verdant Consultants, LLC.
#

if [ -z "${PITRAC_ROOT}" ]; then
  echo "PITRAC_ROOT environment variable not set.  Exiting."
  exit
else
  echo "PITRAC_ROOT set to: ${PITRAC_ROOT}"
fi


export PITRAC_COMMON_CMD_LINE_ARGS="--config_file=$PITRAC_ROOT/ImageProcessing/golf_sim_config.json"

# Ensure that any images that will be displayed can show up on any connected monitor
export DISPLAY=:0.0

