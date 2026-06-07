#!/bin/bash
CONFIGFS="/sys/kernel/config"
GADGET="$CONFIGFS/usb_gadget/g1"
FUNCTION="$GADGET/functions/uvc.0"

mkdir -p $FUNCTION

echo 0x1d6b >$GADGET/idVendor
echo 0x0104 >$GADGET/idProduct
echo 0x0100 >$GADGET/bcdDevice
echo 0x0320 >$GADGET/bcdUSB
echo 0xEF >$GADGET/bDeviceClass
echo 0x02 >$GADGET/bDeviceSubClass
echo 0x01 >$GADGET/bDeviceProtocol

mkdir -p $GADGET/strings/0x409
echo 1000001 >$GADGET/strings/0x409/serialnumber
echo "Raspberry Pi" >$GADGET/strings/0x409/manufacturer
echo "Webcam" >$GADGET/strings/0x409/product

mkdir -p $GADGET/configs/c.1/strings/0x409
echo 500 >$GADGET/configs/c.1/MaxPower
echo "UVC" >$GADGET/configs/c.1/strings/0x409/configuration

# cat <<EOF $GADGET/functions/uvc0/control/processing/default/bmControls
# 0
# 0
# EOF

config_frame() {
    FORMAT=$1
    NAME=$2
    WIDTH=$3
    HEIGHT=$4

    framedir=$FUNCTION/streaming/$FORMAT/$NAME/${HEIGHT}p

    mkdir -p "$framedir"

    echo "$WIDTH" >"$framedir"/wWidth
    echo "$HEIGHT" >"$framedir"/wHeight
    echo 333333 >"$framedir"/dwDefaultFrameInterval
    echo $(($WIDTH * $HEIGHT * 80)) >"$framedir"/dwMinBitRate
    echo $(($WIDTH * $HEIGHT * 160)) >"$framedir"/dwMaxBitRate
    echo $(($WIDTH * $HEIGHT * 2)) >"$framedir"/dwMaxVideoFrameBufferSize
    cat <<EOF >"$framedir"/dwFrameInterval
333333
400000
666666
EOF
}

config_frame mjpeg mjpeg 1056 704

mkdir $FUNCTION/streaming/header/h

# This section links the format descriptors and their associated frames
# to the header
cd $FUNCTION/streaming/header/h
#ln -s ../../uncompressed/yuyv
ln -s ../../mjpeg/mjpeg

# This section ensures that the header will be transmitted for each
# speed's set of descriptors. If support for a particular speed is not
# needed then it can be skipped here.
cd ../../class/fs
ln -s ../../header/h
cd ../../class/hs
ln -s ../../header/h
cd ../../class/ss
ln -s ../../header/h
cd ../../../control

mkdir header/h
ln -s header/h class/fs
ln -s header/h class/ss

ln -s $FUNCTION $GADGET/configs/c.1/uvc.usb0

udevadm settle -t 5 || :
ls /sys/class/udc >$GADGET/UDC
