# SPDX-License-Identifier: GPL-2.0-only */
#
# Copyright (C) 2022-2025, Verdant Consultants, LLC.
#
#!/bin/bash 

echo "Inserting Closed Source Object Files..."
cp -f $PITRAC_ROOT/ImageProcessing/ClosedSourceObjectFiles/* $PITRAC_ROOT/ImageProcessing/build/pitrac_lm.p
cp -f $PITRAC_ROOT/ImageProcessing/ClosedSourceObjectFiles/* $PITRAC_ROOT/ImageProcessing
touch gs_e6_response.cpp.o $PITRAC_ROOT/ImageProcessing/build/pitrac_lm.p/gs_e6_response.cpp.o
