#libcamera-still --nopreview --tuning-file=/usr/share/libcamera/ipa/rpi/vc4/imx296.json --shutter 8000  --denoise cdn_off -o $1
#libcamera-still --gain 3.0 --nopreview --tuning-file=/usr/share/libcamera/ipa/rpi/vc4/imx296.json --shutter 2000 --width 96 --height 88 --denoise cdn_off -o $1
rpicam-still --gain 3.0 --nopreview --tuning-file=/usr/share/libcamera/ipa/rpi/vc4/imx296.json --shutter 20000 --width 1456 --height 1088 --denoise cdn_off -o $1
#libcamera-still --nopreview --tuning-file=/usr/share/libcamera/ipa/rpi/vc4/imx296_noir.json --shutter 2000 --width 1440 --denoise cdn_off -o $1
