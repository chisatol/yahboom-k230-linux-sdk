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
stopping = False
return_requested = False


def handle_stop(_signum, _frame):
    global stopping
    stopping = True


def handle_return(_signum, _frame):
    global return_requested
    return_requested = True


def set_sensor_mode():
    path = "/proc/vsi/isp_subdev0"
    if not os.path.exists(path):
        raise RuntimeError("GC2093 ISP control is unavailable")
    with open(path, "w", encoding="ascii") as control:
        control.write("0 mode=1\n")


def wait_for_key1(camera):
    poller = select.poll()
    devices = {}
    key_down = False
    key1_pressed = False
    last_key_action = 0.0

    while (camera.poll() is None and not stopping and not return_requested and
           not key1_pressed):
        for path in glob.glob("/dev/input/event*"):
            if path in devices:
                continue
            try:
                fd = os.open(path, os.O_RDONLY | os.O_NONBLOCK | os.O_CLOEXEC)
                poller.register(fd, select.POLLIN | select.POLLERR | select.POLLHUP)
                devices[path] = fd
            except OSError:
                pass

        for fd, mask in poller.poll(200):
            if mask & (select.POLLERR | select.POLLHUP):
                path = next((name for name, device_fd in devices.items() if device_fd == fd), None)
                if path is not None:
                    devices.pop(path, None)
                try:
                    poller.unregister(fd)
                    os.close(fd)
                except OSError:
                    pass
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
                    key_down = True
                    now = time.monotonic()
                    if now - last_key_action < 0.75:
                        continue
                    last_key_action = now
                    key1_pressed = True
                    break
            if key1_pressed:
                break

    for fd in devices.values():
        try:
            poller.unregister(fd)
            os.close(fd)
        except OSError:
            pass
    return key1_pressed or return_requested


def stop_camera(camera):
    if camera.poll() is not None:
        return
    camera.terminate()
    try:
        camera.wait(timeout=3)
    except subprocess.TimeoutExpired:
        camera.kill()
        camera.wait()


def restore_desktop():
    open(RETURN_MARKER, "w").close()
    subprocess.run(["systemctl", "start", "lightdm.service"], check=False)


def main():
    global stopping
    signal.signal(signal.SIGTERM, handle_stop)
    signal.signal(signal.SIGINT, handle_stop)
    signal.signal(signal.SIGUSR1, handle_return)
    camera = None
    desktop_was_active = subprocess.run(
        ["systemctl", "is-active", "--quiet", "lightdm.service"], check=False
    ).returncode == 0

    try:
        if desktop_was_active:
            subprocess.run(["systemctl", "stop", "lightdm.service"], check=True)
        subprocess.run(["systemctl", "start", "vvcam.service"], check=True)
        time.sleep(1)
        set_sensor_mode()
        camera = subprocess.Popen([
            "/usr/bin/v4l2-drm", "-d", "2", "-n", "5",
            "-w", "640", "-h", "480", "-f", "BGR3", "--rotation", "3",
        ])
        if wait_for_key1(camera):
            print("KEY1 received; returning to desktop", flush=True)
        elif not stopping:
            print("Camera preview process exited", flush=True)
    finally:
        if camera is not None:
            stop_camera(camera)
        if not stopping:
            restore_desktop()


if __name__ == "__main__":
    main()
