#!/bin/sh
set -eu

: "${DISPLAY:=:0}"
export DISPLAY

xhost +SI:localuser:k230-browser >/dev/null
cd /home/k230-browser
set +e
runuser --user k230-browser -- env \
    DISPLAY="${DISPLAY}" \
    HOME=/home/k230-browser \
    /usr/bin/firefox-esr "$@" &
browser_pid=$!
wait "${browser_pid}"
status=$?
set -e
xhost -SI:localuser:k230-browser >/dev/null 2>&1 || true
exit "${status}"
