# SPDX-License-Identifier: GPL-2.0-only */
#
# Copyright (C) 2022-2025, Verdant Consultants, LLC.
#
#!/bin/bash

. $PITRAC_ROOT/ImageProcessing/RunScripts/runPiTracCommon.sh


#rm -f Logs/*.log

$PITRAC_ROOT/ImageProcessing/build/pitrac_lm  --system_mode camera2 --cam_still_mode $PITRAC_COMMON_CMD_LINE_ARGS  --camera_gain 0.6 --logging_level=trace --artifact_save_level=final_results_only

