#!/bin/sh

# The local display attaches to this session without replacing the UART login.
if [ "$(id -u)" -eq 0 ] && [ -z "${TMUX:-}" ] &&
   [ "$(tty 2>/dev/null)" = /dev/ttyS0 ]; then
    exec /usr/bin/tmux new-session -A -s k230-serial
fi
