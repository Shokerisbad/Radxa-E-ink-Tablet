#!/bin/bash
# install_touch_wakeup.sh
# Automates the compilation and installation of the GT911 hardware wake overlay

echo "Compiling and installing GT911 Touch Wake Overlay for RK3566..."

if ! command -v armbian-add-overlay &> /dev/null
then
    echo "[!] Error: 'armbian-add-overlay' not found. Ensure you are running Radxa OS or Armbian."
    echo "[!] Alternatively, use 'sudo rsetup' or compile manually using dtc."
    exit 1
fi

if [ ! -f "wake_pin35.dts" ]; then
    echo "[!] Error: wake_pin35.dts not found in the current directory."
    exit 1
fi

# Run armbian-add-overlay
sudo armbian-add-overlay wake_pin35.dts

if [ $? -eq 0 ]; then
    echo "[OK] Overlay installed successfully!"
    echo "[!] IMPORTANT: You must reboot the system for the overlay to take effect."
    read -p "Would you like to reboot now? (y/n): " choice
    if [ "$choice" == "y" ]; then
        sudo reboot
    else
        echo "Please reboot manually later."
    fi
else
    echo "[!] Failed to apply overlay."
    exit 1
fi
