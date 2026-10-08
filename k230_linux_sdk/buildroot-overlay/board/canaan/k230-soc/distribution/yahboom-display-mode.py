#!/usr/bin/python3
import glob
import os
import select
import struct
import subprocess
import time


EV_KEY = 1
KEY_PROG1 = 148
INPUT_EVENT = struct.Struct("@llHHi")
CAMERA_RETURN_MARKER = "/home/elaina/.local/state/yahboom-camera-return"
touch_process = None


def set_dpms(state):
    subprocess.run(["xset", "dpms", "force", state], check=True)


def start_touch():
    global touch_process
    if touch_process is not None and touch_process.poll() is None:
        return
    touch_process = subprocess.Popen(["/usr/local/sbin/yahboom-touch-absolute.py"])
    for _ in range(30):
        if touch_process.poll() is not None:
            raise subprocess.CalledProcessError(
                touch_process.returncode, "yahboom-touch-absolute.py"
            )
        time.sleep(0.1)


def stop_touch():
    global touch_process
    if touch_process is None:
        return
    if touch_process.poll() is None:
        touch_process.terminate()
        try:
            touch_process.wait(timeout=2)
        except subprocess.TimeoutExpired:
            touch_process.kill()
            touch_process.wait()
    touch_process = None


def close_desktop():
    stop_touch()
    subprocess.run(["xset", "dpms", "force", "off"], check=False)
    subprocess.run(
        ["sudo", "-n", "/usr/bin/systemctl", "stop", "lightdm.service"],
        check=True,
    )


def initialize_display():
    if os.path.exists(CAMERA_RETURN_MARKER):
        os.unlink(CAMERA_RETURN_MARKER)
    for attempt in range(30):
        try:
            subprocess.run(["xset", "+dpms"], check=True)
            subprocess.run(["xset", "s", "off"], check=True)
            subprocess.run(["xset", "s", "noblank"], check=True)
            subprocess.run(["xset", "dpms", "0", "0", "0"], check=True)
            set_dpms("on")
            break
        except (OSError, subprocess.CalledProcessError) as error:
            if attempt == 29:
                print("Could not switch the panel off: %s" % error, flush=True)
            else:
                time.sleep(1)
def open_event_devices(poller, devices):
    for path in glob.glob("/dev/input/event*"):
        if path in devices:
            continue
        try:
            fd = os.open(path, os.O_RDONLY | os.O_NONBLOCK | os.O_CLOEXEC)
            poller.register(fd, select.POLLIN | select.POLLERR | select.POLLHUP)
            devices[path] = fd
        except OSError as error:
            print("Cannot monitor %s: %s" % (path, error), flush=True)


def close_device(poller, devices, path):
    fd = devices.pop(path, None)
    if fd is not None:
        try:
            poller.unregister(fd)
        except (KeyError, OSError):
            pass
        os.close(fd)


def main():
    initialize_display()
    try:
        start_touch()
    except (OSError, subprocess.CalledProcessError) as error:
        print("Desktop touch failed to start: %s" % error, flush=True)

    poller = select.poll()
    devices = {}
    open_event_devices(poller, devices)
    key_down = False
    last_key_action = 0.0

    while True:
        open_event_devices(poller, devices)
        for fd, event_mask in poller.poll(1000):
            path = next((name for name, device_fd in devices.items() if device_fd == fd), None)
            if path is None:
                continue
            if event_mask & (select.POLLERR | select.POLLHUP):
                close_device(poller, devices, path)
                continue
            try:
                data = os.read(fd, INPUT_EVENT.size * 32)
            except BlockingIOError:
                continue
            except OSError:
                close_device(poller, devices, path)
                continue

            for offset in range(0, len(data) - INPUT_EVENT.size + 1, INPUT_EVENT.size):
                _, _, event_type, code, value = INPUT_EVENT.unpack_from(data, offset)
                if event_type == EV_KEY and code == KEY_PROG1 and value == 0:
                    key_down = False
                elif (event_type == EV_KEY and code == KEY_PROG1 and
                      value == 1 and not key_down):
                    key_down = True
                    now = time.monotonic()
                    if now - last_key_action < 0.75:
                        continue
                    last_key_action = now
                    try:
                        close_desktop()
                        print("KEY1 stopped desktop session", flush=True)
                        return
                    except (OSError, subprocess.CalledProcessError) as error:
                        print("KEY1 display switch failed: %s" % error, flush=True)


if __name__ == "__main__":
    main()
