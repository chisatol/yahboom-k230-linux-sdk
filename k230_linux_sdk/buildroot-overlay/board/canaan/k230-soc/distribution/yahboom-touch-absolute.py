#!/usr/bin/python3
import ctypes
import fcntl
import glob
import math
import os
import select
import signal
import struct
import sys
import time


EV_SYN = 0
EV_KEY = 1
EV_ABS = 3
SYN_REPORT = 0
BTN_TOUCH = 330
ABS_X = 0
ABS_Y = 1
ABS_MT_POSITION_X = 53
ABS_MT_POSITION_Y = 54
ABS_MT_SLOT = 47
ABS_MT_TRACKING_ID = 57
INPUT_EVENT = struct.Struct("@llHHi")
INPUT_ABSINFO = struct.Struct("@iiiiii")
GESTURE_SLOP_PIXELS = 20
RIGHT_CLICK_DELAY = 0.65
DRAG_DELAY = 0.50
SCROLL_STEP_PIXELS = 32
running = True


def stop(_signum, _frame):
    global running
    running = False


def ev_ioc_read(number, size):
    return (2 << 30) | (size << 16) | (ord("E") << 8) | number


def get_absinfo(fd, axis):
    data = bytearray(INPUT_ABSINFO.size)
    fcntl.ioctl(fd, ev_ioc_read(0x40 + axis, len(data)), data, True)
    return INPUT_ABSINFO.unpack(data)


def map_position(x, y, width, height, x_min, x_max, y_min, y_max):
    x_span = max(1, x_max - x_min)
    y_span = max(1, y_max - y_min)
    screen_x = width - 1 - round((y - y_min) * (width - 1) / y_span)
    screen_y = height - 1 - round((x - x_min) * (height - 1) / x_span)
    return (min(width - 1, max(0, screen_x)),
            min(height - 1, max(0, screen_y)))


class ContactCoordinates:
    def __init__(self, x_code, y_code):
        self.x_code = x_code
        self.y_code = y_code
        self.x = None
        self.y = None

    def update(self, code, value):
        if code == self.x_code:
            self.x = value
        elif code == self.y_code:
            self.y = value

    def position(self):
        if self.x is None or self.y is None:
            return None
        return self.x, self.y


def classify_tap(duration, max_distance):
    if max_distance >= GESTURE_SLOP_PIXELS:
        return None
    return 3 if duration >= RIGHT_CLICK_DELAY else 1


def should_start_drag(duration, max_distance):
    return duration >= DRAG_DELAY and max_distance >= GESTURE_SLOP_PIXELS


def scroll_steps(delta, remainder):
    total = remainder + delta
    steps = int(total / SCROLL_STEP_PIXELS)
    return steps, total - steps * SCROLL_STEP_PIXELS


def click(xtst, x11, display, button):
    xtst.XTestFakeButtonEvent(display, button, 1, 0)
    xtst.XTestFakeButtonEvent(display, button, 0, 0)
    x11.XFlush(display)


def move_pointer(xtst, display, screen, x, y):
    xtst.XTestFakeMotionEvent(display, screen, x, y, 0)


def find_touch_device():
    for name_path in glob.glob("/sys/class/input/event*/device/name"):
        try:
            name = open(name_path, encoding="utf-8").read().strip().lower()
        except OSError:
            continue
        if "ft5" in name or "touchscreen" in name or "touch screen" in name:
            event = name_path.split("/")[-3]
            path = "/dev/input/" + event
            if os.path.exists(path):
                return path, name
    return None, None


def load_xtest():
    x11 = ctypes.CDLL("libX11.so.6")
    xtst = ctypes.CDLL("libXtst.so.6")
    x11.XOpenDisplay.argtypes = [ctypes.c_char_p]
    x11.XOpenDisplay.restype = ctypes.c_void_p
    x11.XDefaultScreen.argtypes = [ctypes.c_void_p]
    x11.XDefaultScreen.restype = ctypes.c_int
    x11.XDisplayWidth.argtypes = [ctypes.c_void_p, ctypes.c_int]
    x11.XDisplayWidth.restype = ctypes.c_int
    x11.XDisplayHeight.argtypes = [ctypes.c_void_p, ctypes.c_int]
    x11.XDisplayHeight.restype = ctypes.c_int
    x11.XFlush.argtypes = [ctypes.c_void_p]
    xtst.XTestFakeMotionEvent.argtypes = [
        ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_ulong,
    ]
    xtst.XTestFakeMotionEvent.restype = ctypes.c_int
    xtst.XTestFakeButtonEvent.argtypes = [
        ctypes.c_void_p, ctypes.c_uint, ctypes.c_int, ctypes.c_ulong,
    ]
    display = x11.XOpenDisplay(None)
    if not display:
        raise RuntimeError("cannot open X display")
    screen = x11.XDefaultScreen(display)
    return (x11, xtst, display, screen,
            x11.XDisplayWidth(display, screen),
            x11.XDisplayHeight(display, screen))


def main():
    global running
    signal.signal(signal.SIGTERM, stop)
    signal.signal(signal.SIGINT, stop)
    x11, xtst, display, screen, width, height = load_xtest()
    touch_fd = -1
    dragging = False
    try:
        while running and touch_fd < 0:
            path, name = find_touch_device()
            if path:
                try:
                    touch_fd = os.open(path, os.O_RDONLY | os.O_NONBLOCK | os.O_CLOEXEC)
                    print("Absolute touch enabled on %s (%s)" % (path, name), flush=True)
                except OSError as error:
                    print("Cannot open %s: %s" % (path, error), flush=True)
            if touch_fd < 0:
                time.sleep(1)
        if touch_fd < 0:
            return 1

        try:
            _, x_min, x_max, *_ = get_absinfo(touch_fd, ABS_MT_POSITION_X)
            _, y_min, y_max, *_ = get_absinfo(touch_fd, ABS_MT_POSITION_Y)
            x_code, y_code = ABS_MT_POSITION_X, ABS_MT_POSITION_Y
        except OSError:
            _, x_min, x_max, *_ = get_absinfo(touch_fd, ABS_X)
            _, y_min, y_max, *_ = get_absinfo(touch_fd, ABS_Y)
            x_code, y_code = ABS_X, ABS_Y

        coordinates = ContactCoordinates(x_code, y_code)
        poller = select.poll()
        poller.register(touch_fd, select.POLLIN | select.POLLERR | select.POLLHUP)
        pressed = False
        active = False
        dragging = False
        started = 0.0
        start_x = start_y = 0
        max_distance = 0.0
        current_slot = 0
        slots = {}
        scrolling = False
        suppress_until_release = False
        last_scroll_y = None
        scroll_contact_ids = None
        scroll_remainder = 0

        while running:
            for _fd, mask in poller.poll(50):
                if mask & (select.POLLERR | select.POLLHUP):
                    running = False
                    break
                try:
                    data = os.read(touch_fd, INPUT_EVENT.size * 64)
                except BlockingIOError:
                    continue
                for offset in range(0, len(data) - INPUT_EVENT.size + 1, INPUT_EVENT.size):
                    _, _, event_type, code, value = INPUT_EVENT.unpack_from(data, offset)
                    if event_type == EV_ABS:
                        coordinates.update(code, value)
                        if code == ABS_MT_SLOT:
                            current_slot = value
                            slots.setdefault(current_slot, {"tracking": -1})
                        elif code == ABS_MT_TRACKING_ID:
                            slot = slots.setdefault(current_slot, {"tracking": -1})
                            slot["tracking"] = value
                        elif code in (ABS_MT_POSITION_X, ABS_MT_POSITION_Y):
                            slot = slots.setdefault(current_slot, {"tracking": -1})
                            slot[code] = value
                    elif event_type == EV_KEY and code == BTN_TOUCH:
                        pressed = value != 0
                    elif event_type == EV_SYN and code == SYN_REPORT:
                        raw_position = coordinates.position()
                        contacts = [slot for slot in slots.values()
                                    if slot.get("tracking", -1) >= 0 and
                                    ABS_MT_POSITION_X in slot and
                                    ABS_MT_POSITION_Y in slot]
                        if len(contacts) >= 2:
                            if dragging:
                                xtst.XTestFakeButtonEvent(display, 1, 0, 0)
                                x11.XFlush(display)
                            active = False
                            dragging = False
                            suppress_until_release = True
                            contact_ids = frozenset(
                                slot_id for slot_id, slot in slots.items()
                                if slot.get("tracking", -1) >= 0 and
                                ABS_MT_POSITION_X in slot and
                                ABS_MT_POSITION_Y in slot
                            )
                            screen_ys = [map_position(
                                contact[ABS_MT_POSITION_X], contact[ABS_MT_POSITION_Y],
                                width, height, x_min, x_max, y_min, y_max,
                            )[1] for contact in contacts]
                            center_y = sum(screen_ys) / len(screen_ys)
                            if not scrolling or contact_ids != scroll_contact_ids:
                                scrolling = True
                                scroll_contact_ids = contact_ids
                                last_scroll_y = center_y
                                scroll_remainder = 0
                            elif last_scroll_y is not None:
                                steps, scroll_remainder = scroll_steps(
                                    last_scroll_y - center_y, scroll_remainder
                                )
                                button = 5 if steps > 0 else 4
                                for _ in range(abs(steps)):
                                    click(xtst, x11, display, button)
                            last_scroll_y = center_y
                            continue
                        if scrolling:
                            scrolling = False
                            last_scroll_y = None
                            scroll_contact_ids = None
                        if suppress_until_release:
                            if not pressed and not contacts:
                                suppress_until_release = False
                            continue
                        if pressed and not active:
                            if raw_position is None:
                                continue
                            active = True
                            dragging = False
                            started = time.monotonic()
                            max_distance = 0.0
                            start_x, start_y = map_position(
                                *raw_position, width, height,
                                x_min, x_max, y_min, y_max,
                            )
                            move_pointer(xtst, display, screen, start_x, start_y)
                            x11.XFlush(display)
                        elif pressed and active and raw_position is not None:
                            x, y = map_position(
                                *raw_position, width, height,
                                x_min, x_max, y_min, y_max,
                            )
                            distance = math.hypot(x - start_x, y - start_y)
                            max_distance = max(max_distance, distance)
                            move_pointer(xtst, display, screen, x, y)
                            if not dragging and should_start_drag(
                                    time.monotonic() - started, max_distance):
                                xtst.XTestFakeButtonEvent(display, 1, 1, 0)
                                dragging = True
                            x11.XFlush(display)
                        elif not pressed and active:
                            if dragging:
                                xtst.XTestFakeButtonEvent(display, 1, 0, 0)
                                x11.XFlush(display)
                            else:
                                button = classify_tap(
                                    time.monotonic() - started, max_distance
                                )
                                if button:
                                    click(xtst, x11, display, button)
                            active = False
                            dragging = False
                            max_distance = 0.0
    except (OSError, RuntimeError) as error:
        print("Absolute touch failed: %s" % error, file=sys.stderr, flush=True)
        return 1
    finally:
        if dragging:
            xtst.XTestFakeButtonEvent(display, 1, 0, 0)
            x11.XFlush(display)
        if touch_fd >= 0:
            os.close(touch_fd)
        x11.XCloseDisplay(display)
    return 0


if __name__ == "__main__":
    sys.exit(main())
