# SPDX-License-Identifier: GPL-2.0-only */
#
# Copyright (C) 2022-2025, Verdant Consultants, LLC.
#

#!/bin/bash

. $PITRAC_ROOT/ImageProcessing/RunScripts/runPiTracCommon.sh


$PITRAC_ROOT/ImageProcessing/build/pitrac_lm --send_test_results  --system_mode camera1_test_standalone  --search_center_x 700 --search_center_y 600 --logging_level=trace

# --post-process-file /mnt/VerdantShare/dev/GolfSim/LM/ImageProcessing/assets/motion_detect.json
