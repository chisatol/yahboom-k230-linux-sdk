#!/bin/bash
set -euo pipefail

config=/root/wifi-config.sh
hostapd_conf=/run/hostapd-k230.conf
hostapd_pid=/run/hostapd-k230.pid
dnsmasq_conf=/run/dnsmasq-k230.conf
dnsmasq_pid=/run/dnsmasq-k230.pid
iface_file=/run/yahboom-wifi-iface

stop_hotspot() {
    for pid_file in "$dnsmasq_pid" "$hostapd_pid"; do
        if [ -s "$pid_file" ]; then
            kill "$(cat "$pid_file")" 2>/dev/null || true
            rm -f "$pid_file"
        fi
    done
    rm -f "$hostapd_conf" "$dnsmasq_conf"
}

if [ "${1:-start}" = stop ]; then
    stop_hotspot
    if [ -s "$iface_file" ]; then
        nmcli device set "$(cat "$iface_file")" managed yes 2>/dev/null || true
        rm -f "$iface_file"
    fi
    exit 0
fi

if [ ! -r "$config" ]; then
    echo "Missing Wi-Fi settings: $config" >&2
    exit 1
fi
# shellcheck source=/dev/null
source "$config"

wifi_if="${WIFI_INTERFACE:-wlan0}"
if [ -z "$wifi_if" ]; then
    for _ in $(seq 1 30); do
        wifi_if=$(nmcli -t -f DEVICE,TYPE device status | awk -F: '$2 == "wifi" && $1 !~ /^p2p-dev-/ { print $1; exit }')
        [ -n "$wifi_if" ] && break
        sleep 1
    done
fi
if [ -z "$wifi_if" ]; then
    echo "No Wi-Fi interface found" >&2
    exit 1
fi

nmcli radio wifi on
nmcli connection delete "K230 fallback hotspot" >/dev/null 2>&1 || true

wifi_connected=false
if [ -n "${WIFI_SSID:-}" ] && [ -n "${WIFI_PASSWORD:-}" ]; then
    nmcli connection delete "K230 configured Wi-Fi" >/dev/null 2>&1 || true
    for attempt in 1 2 3; do
        echo "Wi-Fi connection attempt $attempt/3 for SSID: $WIFI_SSID"
        nmcli device wifi rescan ifname "$wifi_if" >/dev/null 2>&1 || true
        sleep 4
        if nmcli --wait 25 device wifi connect "$WIFI_SSID" \
            password "$WIFI_PASSWORD" ifname "$wifi_if" \
            name "K230 configured Wi-Fi"; then
            nmcli connection modify "K230 configured Wi-Fi" \
                connection.autoconnect yes connection.autoconnect-priority 100
            for _ in $(seq 1 10); do
                if nmcli -g GENERAL.STATE device show "$wifi_if" | grep -q '^100'; then
                    wifi_connected=true
                    break
                fi
                sleep 1
            done
        fi
        [ "$wifi_connected" = true ] && break
        [ "$attempt" -eq 3 ] || sleep 3
    done
fi

if [ "$wifi_connected" = true ]; then
    echo "Connected to configured Wi-Fi: $WIFI_SSID"
    exit 0
fi

: "${HOTSPOT_SSID:=}"
: "${HOTSPOT_PASSWORD:=}"
if [ -z "$HOTSPOT_SSID" ] && [ -z "$HOTSPOT_PASSWORD" ]; then
    echo "No upstream Wi-Fi or fallback hotspot configured; edit $config"
    exit 0
fi
if [ -z "$HOTSPOT_SSID" ] || [ -z "$HOTSPOT_PASSWORD" ]; then
    echo "Set both HOTSPOT_SSID and HOTSPOT_PASSWORD in $config" >&2
    exit 1
fi
if [ "${#HOTSPOT_PASSWORD}" -lt 8 ]; then
    echo "HOTSPOT_PASSWORD must contain at least 8 characters" >&2
    exit 1
fi

echo "Configured Wi-Fi unavailable; starting fallback hotspot: $HOTSPOT_SSID"
if [ "${#HOTSPOT_SSID}" -gt 32 ] || [[ "$HOTSPOT_SSID" == *$'\n'* ]]; then
    echo "HOTSPOT_SSID must be at most 32 bytes and contain no newline" >&2
    exit 1
fi
if [[ "$HOTSPOT_PASSWORD" == *[^\ -\~]* ]] || [ "${#HOTSPOT_PASSWORD}" -gt 63 ]; then
    echo "HOTSPOT_PASSWORD must be 8-63 printable ASCII characters" >&2
    exit 1
fi

nmcli connection down "K230 configured Wi-Fi" >/dev/null 2>&1 || true
nmcli device disconnect "$wifi_if" >/dev/null 2>&1 || true
nmcli device set "$wifi_if" managed no
printf '%s\n' "$wifi_if" > "$iface_file"
ip link set dev "$wifi_if" down
ip address flush dev "$wifi_if"
ip address add 10.42.0.1/24 dev "$wifi_if"
ip link set dev "$wifi_if" up

cat > "$hostapd_conf" <<HOSTAPD_EOF
interface=$wifi_if
driver=nl80211
ssid=$HOTSPOT_SSID
hw_mode=g
channel=1
ieee80211n=0
auth_algs=1
wpa=2
wpa_key_mgmt=WPA-PSK
rsn_pairwise=CCMP
wpa_passphrase=$HOTSPOT_PASSWORD
HOSTAPD_EOF

cat > "$dnsmasq_conf" <<DNSMASQ_EOF
interface=$wifi_if
bind-interfaces
dhcp-authoritative
dhcp-range=10.42.0.20,10.42.0.100,255.255.255.0,12h
dhcp-option=3,10.42.0.1
dhcp-option=6,1.1.1.1,8.8.8.8
no-resolv
no-hosts
port=0
pid-file=$dnsmasq_pid
DNSMASQ_EOF

hostapd -B -P "$hostapd_pid" "$hostapd_conf"
sleep 2
if [ ! -s "$hostapd_pid" ] || ! kill -0 "$(cat "$hostapd_pid")" 2>/dev/null; then
    echo "hostapd did not stay running; inspect journalctl -u yahboom-wifi" >&2
    exit 1
fi
dnsmasq --conf-file="$dnsmasq_conf"
echo "Hotspot active: $HOTSPOT_SSID on $wifi_if (10.42.0.1/24)"
