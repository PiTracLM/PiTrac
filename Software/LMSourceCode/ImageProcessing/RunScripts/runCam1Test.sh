# SPDX-License-Identifier: GPL-2.0-only */
#
# Copyright (C) 2022-2025, Verdant Consultants, LLC.
#
#!/bin/bash

. $PITRAC_ROOT/ImageProcessing/RunScripts/runPiTracCommon.sh


rm -f Logs/*.log

$PITRAC_ROOT/ImageProcessing/build/pitrac_lm --show_images 0 --logging_level trace  $PITRAC_COMMON_CMD_LINE_ARGS  --artifact_save_level=all --wait_keys 0 --system_mode test  --search_center_x 800  --search_center_y 550

# --post-process-file /mnt/VerdantShare/dev/GolfSim/LM/ImageProcessing/assets/motion_detect.json
