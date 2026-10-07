#!/usr/bin/python3
import glob
import fcntl
import ctypes
import os
import select
import signal
import struct
import subprocess
import time


EV_KEY = 1
KEY_PROG1 = 148
INPUT_EVENT = struct.Struct("@llHHi")
RETURN_MARKER = "/run/yahboom-camera-return"
CAMERA_SERVICE = "yahboom-camera-preview.service"
BLANK_VT = 8
stopping = False
drm_master_fd = None
drm_master_lib = None


class DrmResources(ctypes.Structure):
    _fields_ = [
        ("count_fbs", ctypes.c_int),
        ("fbs", ctypes.POINTER(ctypes.c_uint32)),
        ("count_crtcs", ctypes.c_int),
        ("crtcs", ctypes.POINTER(ctypes.c_uint32)),
        ("count_connectors", ctypes.c_int),
        ("connectors", ctypes.POINTER(ctypes.c_uint32)),
        ("count_encoders", ctypes.c_int),
        ("encoders", ctypes.POINTER(ctypes.c_uint32)),
        ("min_width", ctypes.c_uint32),
        ("max_width", ctypes.c_uint32),
        ("min_height", ctypes.c_uint32),
        ("max_height", ctypes.c_uint32),
    ]


class DrmObjectProperties(ctypes.Structure):
    _fields_ = [
        ("count_props", ctypes.c_uint32),
        ("props", ctypes.POINTER(ctypes.c_uint32)),
        ("prop_values", ctypes.POINTER(ctypes.c_uint64)),
    ]


class DrmProperty(ctypes.Structure):
    _fields_ = [
        ("prop_id", ctypes.c_uint32),
        ("flags", ctypes.c_uint32),
        ("name", ctypes.c_char * 32),
        ("count_values", ctypes.c_int),
        ("values", ctypes.POINTER(ctypes.c_uint64)),
        ("count_enums", ctypes.c_int),
        ("enums", ctypes.c_void_p),
        ("count_blobs", ctypes.c_int),
        ("blob_ids", ctypes.POINTER(ctypes.c_uint32)),
    ]


class DrmConnector(ctypes.Structure):
    _fields_ = [
        ("connector_id", ctypes.c_uint32),
        ("encoder_id", ctypes.c_uint32),
        ("connector_type", ctypes.c_uint32),
        ("connector_type_id", ctypes.c_uint32),
        ("connection", ctypes.c_int),
        ("mm_width", ctypes.c_uint32),
        ("mm_height", ctypes.c_uint32),
        ("subpixel", ctypes.c_int),
        ("count_modes", ctypes.c_int),
        ("modes", ctypes.c_void_p),
        ("count_props", ctypes.c_int),
        ("props", ctypes.POINTER(ctypes.c_uint32)),
        ("prop_values", ctypes.POINTER(ctypes.c_uint64)),
        ("count_encoders", ctypes.c_int),
        ("encoders", ctypes.POINTER(ctypes.c_uint32)),
    ]


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


def activate_vt(number):
    tty = os.open("/dev/tty0", os.O_RDWR | os.O_CLOEXEC)
    try:
        fcntl.ioctl(tty, 0x5606, number)  # VT_ACTIVATE
        fcntl.ioctl(tty, 0x5607, number)  # VT_WAITACTIVE
    finally:
        os.close(tty)
    if number == BLANK_VT:
        tty = os.open("/dev/tty%d" % number, os.O_RDWR | os.O_CLOEXEC)
        try:
            fcntl.ioctl(tty, 0x4B3B, struct.pack("@i", 1))  # KDSETMODE, KD_GRAPHICS
        finally:
            os.close(tty)


def unbind_fbcon():
    for path in glob.glob("/sys/class/vtconsole/vtcon*/"):
        name_path = os.path.join(path, "name")
        bind_path = os.path.join(path, "bind")
        try:
            with open(name_path, encoding="ascii") as name_file:
                name = name_file.read()
            if "frame buffer device" not in name:
                continue
            with open(bind_path, encoding="ascii") as bind_file:
                bound = bind_file.read().strip() == "1"
            if bound:
                with open(bind_path, "w", encoding="ascii") as bind_file:
                    bind_file.write("0")
        except OSError as error:
            print("Could not unbind framebuffer console: %s" % error, flush=True)


def disable_display_controller():
    global drm_master_fd, drm_master_lib
    drm = ctypes.CDLL("libdrm.so.2", use_errno=True)
    drm.drmModeGetResources.restype = ctypes.POINTER(DrmResources)
    drm.drmModeGetResources.argtypes = [ctypes.c_int]
    drm.drmModeFreeResources.argtypes = [ctypes.POINTER(DrmResources)]
    drm.drmModeObjectGetProperties.restype = ctypes.POINTER(DrmObjectProperties)
    drm.drmModeObjectGetProperties.argtypes = [ctypes.c_int, ctypes.c_uint32, ctypes.c_uint32]
    drm.drmModeGetProperty.restype = ctypes.POINTER(DrmProperty)
    drm.drmModeGetProperty.argtypes = [ctypes.c_int, ctypes.c_uint32]
    drm.drmModeObjectSetProperty.argtypes = [
        ctypes.c_int, ctypes.c_uint32, ctypes.c_uint32,
        ctypes.c_uint32, ctypes.c_uint64,
    ]
    drm.drmModeFreeObjectProperties.argtypes = [ctypes.POINTER(DrmObjectProperties)]
    drm.drmModeFreeProperty.argtypes = [ctypes.POINTER(DrmProperty)]
    drm.drmModeGetConnector.restype = ctypes.POINTER(DrmConnector)
    drm.drmModeGetConnector.argtypes = [ctypes.c_int, ctypes.c_uint32]
    drm.drmModeFreeConnector.argtypes = [ctypes.POINTER(DrmConnector)]
    fd = os.open("/dev/dri/card0", os.O_RDWR | os.O_CLOEXEC)
    resources = None
    try:
        if drm.drmSetMaster(fd) != 0:
            error = ctypes.get_errno()
            raise OSError(error, os.strerror(error))
        resources = drm.drmModeGetResources(fd)
        if not resources:
            error = ctypes.get_errno()
            raise OSError(error, os.strerror(error))
        dpms_set = False
        for index in range(resources.contents.count_connectors):
            connector_id = resources.contents.connectors[index]
            connector = drm.drmModeGetConnector(fd, connector_id)
            if not connector:
                continue
            try:
                if connector.contents.connection != 1:
                    continue
            finally:
                drm.drmModeFreeConnector(connector)
            properties = drm.drmModeObjectGetProperties(fd, connector_id, 0xC0C0C0C0)
            if not properties:
                continue
            try:
                for prop_index in range(properties.contents.count_props):
                    prop_id = properties.contents.props[prop_index]
                    prop = drm.drmModeGetProperty(fd, prop_id)
                    if not prop:
                        continue
                    try:
                        if prop.contents.name.decode("ascii", "ignore") == "DPMS":
                            if drm.drmModeObjectSetProperty(
                                    fd, connector_id, 0xC0C0C0C0, prop_id, 3) != 0:
                                error = ctypes.get_errno()
                                raise OSError(error, os.strerror(error))
                            print("Requested DPMS Off on connector %u property %u" %
                                  (connector_id, prop_id), flush=True)
                            dpms_set = True
                            break
                    finally:
                        drm.drmModeFreeProperty(prop)
            finally:
                drm.drmModeFreeObjectProperties(properties)
            if dpms_set:
                break
        if not dpms_set:
            raise RuntimeError("DRM connector does not expose a DPMS property")
        if resources:
            drm.drmModeFreeResources(resources)
            resources = None
        drm_master_fd = fd
        drm_master_lib = drm
        return fd
    except Exception:
        if resources:
            drm.drmModeFreeResources(resources)
        os.close(fd)
        raise


def release_display_controller(fd):
    global drm_master_fd, drm_master_lib
    if fd is None:
        return
    if drm_master_lib is not None:
        drm_master_lib.drmDropMaster(fd)
    os.close(fd)
    drm_master_fd = None
    drm_master_lib = None


def monitor_desktop_wake():
    activate_vt(BLANK_VT)
    unbind_fbcon()
    display_fd = None
    try:
        display_fd = disable_display_controller()
    except (AttributeError, OSError, RuntimeError) as error:
        print("Could not power down DRM display: %s" % error, flush=True)
    poller = select.poll()
    devices = {}
    key_down = False

    try:
        while (not stopping and not unit_active("lightdm.service") and
               not unit_active(CAMERA_SERVICE)):
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
                        release_display_controller(display_fd)
                        display_fd = None
                        open(RETURN_MARKER, "w").close()
                        subprocess.run(["systemctl", "start", "lightdm.service"], check=False)
                        return
    finally:
        close_devices(poller, devices)
        release_display_controller(display_fd)


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
