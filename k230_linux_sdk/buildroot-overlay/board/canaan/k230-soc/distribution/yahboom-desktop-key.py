#!/usr/bin/python3
import glob
import os
import select
import signal
import struct
import subprocess
import time


EV_KEY = 1
KEY_PROG1 = 148
INPUT_EVENT = struct.Struct("@llHHi")
RETURN_MARKER = "/home/elaina/.local/state/yahboom-camera-return"
CAMERA_SERVICE = "yahboom-camera-preview.service"
stopping = False
last_key_action = 0.0


def handle_stop(_signum, _frame):
    global stopping
    stopping = True


def unit_active(unit):
    return subprocess.run(
        ["systemctl", "is-active", "--quiet", unit], check=False
    ).returncode == 0


def close_devices(poller, devices):
    for fd in devices.values():
        try:
            poller.unregister(fd)
        except (KeyError, OSError):
            pass
        os.close(fd)
    devices.clear()


def disable_display_controller():
    with open("/sys/class/graphics/fb0/blank", "w", encoding="ascii") as blank:
        blank.write("4\n")
    print("Powered down framebuffer and LCD panel", flush=True)
    return None


def enable_display_controller():
    with open("/sys/class/graphics/fb0/blank", "w", encoding="ascii") as blank:
        blank.write("0\n")


def display_is_powered_down():
    paths = glob.glob("/sys/class/drm/card*-DSI-*/dpms")
    if not paths:
        return False
    try:
        for path in paths:
            with open(path, encoding="ascii") as dpms:
                if dpms.read().strip() != "Off":
                    return False
        return True
    except OSError:
        return False


def monitor_desktop_wake():
    global last_key_action
    try:
        disable_display_controller()
    except (OSError, RuntimeError) as error:
        print("Could not power down DRM display: %s" % error, flush=True)
    next_power_check = time.monotonic() + 0.5
    poller = select.poll()
    devices = {}
    key_down = False

    try:
        while (not stopping and not unit_active("lightdm.service") and
               not unit_active(CAMERA_SERVICE)):
            now = time.monotonic()
            if now >= next_power_check:
                if not display_is_powered_down():
                    try:
                        disable_display_controller()
                    except (OSError, RuntimeError) as error:
                        print("Could not reapply display powerdown: %s" % error, flush=True)
                next_power_check = now + 0.5

            for path in glob.glob("/dev/input/event*"):
                if path in devices:
                    continue
                try:
                    fd = os.open(path, os.O_RDONLY | os.O_NONBLOCK | os.O_CLOEXEC)
                    poller.register(fd, select.POLLIN | select.POLLERR | select.POLLHUP)
                    devices[path] = fd
                except OSError:
                    pass

            for fd, mask in poller.poll(250):
                path = next((name for name, device_fd in devices.items() if device_fd == fd), None)
                if path is None:
                    continue
                if mask & (select.POLLERR | select.POLLHUP):
                    poller.unregister(fd)
                    os.close(fd)
                    devices.pop(path, None)
                    continue
                try:
                    data = os.read(fd, INPUT_EVENT.size * 32)
                except (BlockingIOError, OSError):
                    continue
                for offset in range(0, len(data) - INPUT_EVENT.size + 1, INPUT_EVENT.size):
                    _, _, event_type, code, value = INPUT_EVENT.unpack_from(data, offset)
                    if event_type != EV_KEY or code != KEY_PROG1:
                        continue
                    if value == 0:
                        key_down = False
                    elif value == 1 and not key_down:
                        now = time.monotonic()
                        if now - last_key_action < 0.75:
                            continue
                        last_key_action = now
                        try:
                            enable_display_controller()
                        except OSError as error:
                            print("Could not enable framebuffer: %s" % error, flush=True)
                        open(RETURN_MARKER, "w").close()
                        subprocess.run(["systemctl", "start", "lightdm.service"], check=False)
                        return
    finally:
        close_devices(poller, devices)


def main():
    signal.signal(signal.SIGTERM, handle_stop)
    signal.signal(signal.SIGINT, handle_stop)
    while not stopping:
        if unit_active("lightdm.service") or unit_active(CAMERA_SERVICE):
            time.sleep(0.5)
            continue
        monitor_desktop_wake()


if __name__ == "__main__":
    main()
