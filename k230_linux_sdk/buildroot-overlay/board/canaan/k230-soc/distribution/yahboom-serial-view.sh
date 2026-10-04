#!/bin/sh

while ! /usr/bin/tmux has-session -t k230-serial 2>/dev/null; do
    printf '\rWaiting for serial login...'
    sleep 1
done

printf '\033[2J\033[H'
exec /usr/bin/tmux attach-session -t k230-serial
