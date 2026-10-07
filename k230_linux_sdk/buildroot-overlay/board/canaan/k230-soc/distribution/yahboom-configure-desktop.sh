#!/bin/bash
set -euo pipefail

if [ "$#" -ne 1 ] || [ ! -d "$1" ]; then
    echo "Usage: $0 <rootfs>" >&2
    exit 2
fi

rootfs=$1
distribution_dir=$(cd "$(dirname "$0")" && pwd)

# KEY1 owns desktop startup so the panel boots fully blank by default.
if [ -d "${rootfs}/etc/systemd/system" ]; then
    find "${rootfs}/etc/systemd/system" -type l -print0 |
        while IFS= read -r -d '' link; do
            case "$(readlink "${link}")" in
                *lightdm.service|*/display-manager.service)
                    rm -f "${link}"
                    ;;
            esac
        done
fi

openbox_config="${rootfs}/root/.config/openbox/lxde-rc.xml"
openbox_default="${rootfs}/etc/xdg/openbox/LXDE/rc.xml"
if [ ! -s "${openbox_config}" ] ||
   ! python3 -c 'import sys, xml.etree.ElementTree as ET; ET.parse(sys.argv[1])' \
       "${openbox_config}" 2>/dev/null; then
    if [ ! -s "${openbox_default}" ] ||
       ! python3 -c 'import sys, xml.etree.ElementTree as ET; ET.parse(sys.argv[1])' \
           "${openbox_default}" 2>/dev/null; then
        echo "Missing or invalid default Openbox LXDE configuration: ${openbox_default}" >&2
        exit 1
    fi
    install -D -m 0644 "${openbox_default}" "${openbox_config}"
fi

install -D -m 0755 "${distribution_dir}/yahboom-display-mode.py" \
    "${rootfs}/usr/local/sbin/yahboom-display-mode.py"
install -D -m 0755 "${distribution_dir}/yahboom-touch-absolute.py" \
    "${rootfs}/usr/local/sbin/yahboom-touch-absolute.py"
install -D -m 0644 "${distribution_dir}/yahboom-display-mode.desktop" \
    "${rootfs}/etc/xdg/autostart/yahboom-display-mode.desktop"
install -D -m 0755 "${distribution_dir}/yahboom-camera-preview" \
    "${rootfs}/usr/local/sbin/yahboom-camera-preview"
install -D -m 0755 "${distribution_dir}/yahboom-camera-mode.py" \
    "${rootfs}/usr/local/sbin/yahboom-camera-mode.py"
install -D -m 0644 "${distribution_dir}/yahboom-camera-preview.service" \
    "${rootfs}/etc/systemd/system/yahboom-camera-preview.service"
install -D -m 0755 "${distribution_dir}/yahboom-desktop-key.py" \
    "${rootfs}/usr/local/sbin/yahboom-desktop-key.py"
install -D -m 0644 "${distribution_dir}/yahboom-desktop-key.service" \
    "${rootfs}/etc/systemd/system/yahboom-desktop-key.service"
mkdir -p "${rootfs}/etc/systemd/system/multi-user.target.wants"
ln -sf /etc/systemd/system/yahboom-desktop-key.service \
    "${rootfs}/etc/systemd/system/multi-user.target.wants/yahboom-desktop-key.service"
install -D -m 0644 "${distribution_dir}/yahboom-camera.desktop" \
    "${rootfs}/usr/share/applications/yahboom-camera.desktop"
if [ -e "${rootfs}/root/Desktop/Camera.desktop" ]; then
    rm -f "${rootfs}/root/Desktop/Camera.desktop"
fi

calibration="${rootfs}/etc/X11/xorg.conf.d/99-calibration.conf"
mkdir -p "$(dirname "${calibration}")"
cat > "${calibration}" <<'XORG_EOF'
Section "InputClass"
    Identifier "touchscreen"
    Driver "libinput"
    MatchIsTouchscreen "on"
    MatchDevicePath "/dev/input/event*"
    Option "Ignore" "true"
    Option "CalibrationMatrix" "0 -1 1 -1 0 1 0 0 1"
EndSection
XORG_EOF

desktop_items="${rootfs}/root/.config/pcmanfm/LXDE/desktop-items-0.conf"
mkdir -p "$(dirname "${desktop_items}")"
if [ ! -f "${desktop_items}" ]; then
    printf '[*]\n' > "${desktop_items}"
fi
if grep -q '^desktop_font=' "${desktop_items}"; then
    sed -i 's/^desktop_font=.*/desktop_font=Sans 8/' "${desktop_items}"
else
    printf '\ndesktop_font=Sans 8\n' >> "${desktop_items}"
fi
if grep -q '^desktop_icon_size=' "${desktop_items}"; then
    sed -i 's/^desktop_icon_size=.*/desktop_icon_size=48/' "${desktop_items}"
else
    printf 'desktop_icon_size=48\n' >> "${desktop_items}"
fi
if grep -q '^wallpaper=' "${desktop_items}"; then
    sed -i 's|^wallpaper=.*|wallpaper=/usr/share/images/desktop-base/desktop-background|' "${desktop_items}"
else
    printf 'wallpaper=/usr/share/images/desktop-base/desktop-background\n' >> "${desktop_items}"
fi
if grep -q '^wallpaper_mode=' "${desktop_items}"; then
    sed -i 's/^wallpaper_mode=.*/wallpaper_mode=stretch/' "${desktop_items}"
else
    printf 'wallpaper_mode=stretch\n' >> "${desktop_items}"
fi

panel_config="${rootfs}/root/.config/lxpanel/LXDE/panels/panel"
mkdir -p "$(dirname "${panel_config}")"
if [ ! -f "${panel_config}" ]; then
    install -D -m 0644 "${rootfs}/etc/xdg/lxpanel/LXDE/panels/panel" "${panel_config}"
fi
sed -i 's/^    height=.*/    height=26/' "${panel_config}"

printf 'Xft.dpi: 96\n' > "${rootfs}/root/.Xresources"
