#!/bin/bash
# Thanks to https://gist.github.com/Hermann-SW/e6049fe1a24fc2b5a53c654e0e9f6b9c
#media-ctl -d /dev/media0 --set-v4l2 "'imx296 10-001a':0 [fmt:SBGGR10_1X10/720x540 crop:(0,0)/720x540], 'imx296 10-001a':0 [fmt:SBGGR10_1X10/128x96 crop:(0,0)/128x96]" -v
#
if [[ $# -lt 4 ]];  then  echo Format: "$0" width height startX StartY; exit;  fi
if [[ $# -gt 4 ]];  then  SHTR="--shutter"; else SHTR="";  fi
for((m=1; m<=5; ++m))
do
    if  media-ctl -d "/dev/media$m" --set-v4l2 "'imx296 10-001a':0 [fmt:SBGGR10_1X10/${1}x$2 crop:($(( $3 )),$(( $4 )))/${1}x$2]" >/dev/null;  then  echo -e "/dev/media$m\n"; break;  fi
done
libcamera-hello --list-cameras  ;echo
# rm -f /dev/shm/tst.pts
# libcamera-vid --width "$1" --height "$2" --denoise cdn_off --framerate "$3" --save-pts /dev/shm/tst.pts -t "$4" "$SHTR" "$5" -o /dev/shm/tst.h264 -n  ;echo 
# rm -f tstamps.csv && ptsanalyze /dev/shm/tst.pts
