#!/bin/bash
# install_touch_wakeup.sh
# Compiles the GT911 hardware wake overlay using dtc

echo "Compiling GT911 Touch Wake Overlay for RK3566..."

if ! command -v dtc &> /dev/null
then
    echo "[!] Error: 'dtc' (Device Tree Compiler) not found."
    echo "[!] Please install it first by running: sudo apt install device-tree-compiler"
    exit 1
fi

if [ ! -f "wake_pin35.dts" ]; then
    echo "[!] Error: wake_pin35.dts not found in the current directory."
    exit 1
fi

# Compile the DTS into a DTBO (Device Tree Blob Overlay)
# The -@ flag is required to allow symbols/overlays
# The -O dtb flag specifies the output format
dtc -@ -I dts -O dtb -o wake_pin35.dtbo wake_pin35.dts

if [ $? -eq 0 ]; then
    echo "[OK] Successfully compiled wake_pin35.dtbo!"
    echo ""
    echo "========================================================="
    echo " NEXT STEPS FOR RADXA OS BOOKWORM:"
    echo "========================================================="
    echo "1. Copy the compiled overlay to the boot partition:"
    echo "   sudo cp wake_pin35.dtbo /boot/dtb/rockchip/overlay/"
    echo "   (If that folder doesn't exist, try: /boot/firmware/overlays/)"
    echo ""
    echo "2. Enable the overlay using Radxa's configuration tool:"
    echo "   sudo rsetup"
    echo "   -> Go to 'Overlays' -> 'Manage overlays' -> find and enable 'wake_pin35'"
    echo ""
    echo "   OR manually add it to your boot config:"
    echo "   Open /boot/extlinux/extlinux.conf (or /boot/uEnv.txt)"
    echo "   Add 'wake_pin35' to the 'overlays=' line."
    echo ""
    echo "3. Reboot the system:"
    echo "   sudo reboot"
    echo "========================================================="
else
    echo "[!] Failed to compile overlay."
    exit 1
fi
