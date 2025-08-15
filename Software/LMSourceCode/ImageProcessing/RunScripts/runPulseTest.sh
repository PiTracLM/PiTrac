# SPDX-License-Identifier: GPL-2.0-only */
#
# Copyright (C) 2022-2025, Verdant Consultants, LLC.
#

. $PITRAC_ROOT/ImageProcessing/RunScripts/runPiTracCommon.sh

$PITRAC_ROOT/ImageProcessing/build/pitrac_lm --pulse_test --system_mode camera1   $PITRAC_COMMON_CMD_LINE_ARGS  --logging_level trace

