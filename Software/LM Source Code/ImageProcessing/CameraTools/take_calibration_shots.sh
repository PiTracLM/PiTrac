#!/bin/bash
#
# Takes numPics pictures, each about 1 second apart
#
if [[ $# -lt 2 ]];  then  echo Format: "$0" baseFName numPics; exit;  fi
sleep 5
for((m=1; m<=${2}; ++m))
do
	echo '====================== READY ======================='
	sleep 2
	libcamera-still --timeout 1000 --gain 1.0 --nopreview --tuning-file=/usr/share/libcamera/ipa/rpi/vc4/imx296_noir.json --shutter 11000 --width 1456 --height 1088 --denoise cdn_off -o ${1}${m}.png
done
