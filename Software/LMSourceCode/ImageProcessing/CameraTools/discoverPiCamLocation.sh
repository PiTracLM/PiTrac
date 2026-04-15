#!/bin/bash
rm -f discover_media.txt discover_device.txt discover_result.txt $PITRAC_ROOT/test.txt

for ((m = 0; m <= 5; ++m)); do
    rm -f discover_result.txt
    media-ctl -d "/dev/media$m" --print-dot | grep imx >discover_media.txt
    awk -F"imx296 " '{print $2}' <discover_media.txt | cut -d- -f1 >discover_device.txt
    echo -n -e "Camera is at /dev/media$m on device number " >discover_result.txt
    cat discover_device.txt >>discover_result.txt

    # If we found a camera, save the results.  Don't stop -- there might be more than one camera
    if grep imx discover_media.txt >/dev/null; then cat discover_result.txt >>$PITRAC_ROOT/test.txt; fi
done

rm -f discover_media.txt discover_device.txt discover_result.txt
