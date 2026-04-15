#!/bin/bash
#
# Determine what the correct img296 tuning file should be on
# this machine.  Only returns the _noir version.
#

model=$(tr -d '\0' </sys/firmware/devicetree/base/model)

if [[ "$model" == *"Raspberry Pi 4"* ]]; then
    echo "/usr/share/libcamera/ipa/rpi/vc4/imx296.json"
elif [[ "$model" == *"Raspberry Pi 5"* ]]; then
    echo "/usr/share/libcamera/ipa/rpi/pisp/imx296.json"
else
    echo "This is not a Raspberry Pi 4 or 5, model is: $model"
    exit
fi
