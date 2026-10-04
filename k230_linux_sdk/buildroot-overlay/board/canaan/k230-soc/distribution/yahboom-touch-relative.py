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
INPUT_EVENT = struct.Struct("@llHHi")
INPUT_ABSINFO = struct.Struct("@iiiiii")
GESTURE_SLOP_PIXELS = 20
RIGHT_CLICK_DELAY = 0.65
DRAG_DELAY = 0.50
SCROLL_STEP = 36
SCROLL_EDGE = 42
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


def map_delta(dx, dy, width, height, x_span, y_span):
    return (-dy * (width - 1) / max(1, y_span),
            -dx * (height - 1) / max(1, x_span))


class ContactCoordinates:
    def __init__(self, x_code=ABS_MT_POSITION_X, y_code=ABS_MT_POSITION_Y):
        self.x_code = x_code
        self.y_code = y_code
        self.pending_x = None
        self.pending_y = None
        self.origin_x = None
        self.origin_y = None

    def update(self, code, value):
        if code == self.x_code:
            self.pending_x = value
        elif code == self.y_code:
            self.pending_y = value

    def begin(self):
        if self.pending_x is None or self.pending_y is None:
            return None
        self.origin_x = self.pending_x
        self.origin_y = self.pending_y
        return self.origin_x, self.origin_y

    def offset(self):
        if (self.pending_x is None or self.pending_y is None or
                self.origin_x is None or self.origin_y is None):
            return None
        return self.pending_x - self.origin_x, self.pending_y - self.origin_y

    def reset(self):
        self.pending_x = self.pending_y = None
        self.origin_x = self.origin_y = None


def classify_tap(duration, max_distance, scroll_started):
    if scroll_started or max_distance >= GESTURE_SLOP_PIXELS:
        return None
    return 3 if duration >= RIGHT_CLICK_DELAY else 1


def should_start_drag(duration, max_distance):
    return duration >= DRAG_DELAY and max_distance >= GESTURE_SLOP_PIXELS


def is_scroll_edge(screen_x, width):
    return screen_x >= width - SCROLL_EDGE


def click(xtst, x11, display, button):
    xtst.XTestFakeButtonEvent(display, button, 1, 0)
    xtst.XTestFakeButtonEvent(display, button, 0, 0)
    x11.XFlush(display)


def move_pointer(xtst, display, screen, dx, dy):
    xtst.XTestFakeRelativeMotionEvent(display, screen, dx, dy, 0)


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
    xtst.XTestFakeRelativeMotionEvent.argtypes = [
        ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_ulong,
    ]
    xtst.XTestFakeRelativeMotionEvent.restype = ctypes.c_int
    xtst.XTestFakeButtonEvent.argtypes = [
        ctypes.c_void_p, ctypes.c_uint, ctypes.c_int, ctypes.c_ulong,
    ]
    display = x11.XOpenDisplay(None)
    if not display:
        raise RuntimeError("cannot open X display")
    screen = x11.XDefaultScreen(display)
    return x11, xtst, display, screen, x11.XDisplayWidth(display, screen), x11.XDisplayHeight(display, screen)


def main():
    global running
    signal.signal(signal.SIGTERM, stop)
    signal.signal(signal.SIGINT, stop)
    x11, xtst, display, screen, width, height = load_xtest()
    touch_fd = -1
    pressed = False
    coordinates = None
    fraction_x = fraction_y = 0.0
    touch_active = False
    touch_started = 0.0
    touch_screen_x = 0
    max_distance = 0.0
    edge_scroll = False
    applied_x = applied_y = 0.0
    applied_scroll = 0.0
    scroll_remainder = 0.0
    scroll_started = False
    dragging = False

    try:
        while running and touch_fd < 0:
            path, name = find_touch_device()
            if path:
                try:
                    touch_fd = os.open(path, os.O_RDONLY | os.O_NONBLOCK | os.O_CLOEXEC)
                    print("Relative touch enabled on %s (%s)" % (path, name), flush=True)
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
        x_span = max(1, x_max - x_min)
        y_span = max(1, y_max - y_min)

        poller = select.poll()
        poller.register(touch_fd, select.POLLIN | select.POLLERR | select.POLLHUP)
        while running:
            for _fd, mask in poller.poll(20):
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
                    elif event_type == EV_KEY and code == BTN_TOUCH:
                        pressed = value != 0
                    elif event_type == EV_SYN and code == SYN_REPORT:
                        if pressed and not touch_active:
                            start_position = coordinates.begin()
                            if start_position is None:
                                continue
                            touch_active = True
                            touch_started = time.monotonic()
                            fraction_x = fraction_y = 0.0
                            applied_x = applied_y = 0.0
                            applied_scroll = scroll_remainder = 0.0
                            max_distance = 0.0
                            # Contact location is used only to detect the scroll edge.
                            touch_screen_x, _ = map_position(
                                *start_position, width, height,
                                x_min, x_max, y_min, y_max,
                            )
                            edge_scroll = is_scroll_edge(touch_screen_x, width)
                            scroll_started = False
                            dragging = False
                        elif pressed and touch_active:
                            offset = coordinates.offset()
                            if offset is None:
                                continue
                            target_x, target_y = map_delta(
                                *offset, width, height, x_span, y_span
                            )
                            max_distance = max(max_distance, math.hypot(target_x, target_y))
                            if edge_scroll:
                                target_scroll = -target_y
                                scroll_remainder += target_scroll - applied_scroll
                                applied_scroll = target_scroll
                                while scroll_remainder >= SCROLL_STEP:
                                    click(xtst, x11, display, 4)
                                    scroll_remainder -= SCROLL_STEP
                                    scroll_started = True
                                while scroll_remainder <= -SCROLL_STEP:
                                    click(xtst, x11, display, 5)
                                    scroll_remainder += SCROLL_STEP
                                    scroll_started = True
                            else:
                                fraction_x += target_x - applied_x
                                fraction_y += target_y - applied_y
                                applied_x, applied_y = target_x, target_y
                                move_x = int(fraction_x)
                                move_y = int(fraction_y)
                                fraction_x -= move_x
                                fraction_y -= move_y
                                if move_x or move_y:
                                    move_pointer(xtst, display, screen, move_x, move_y)
                                if (not dragging and
                                        should_start_drag(time.monotonic() - touch_started, max_distance)):
                                    xtst.XTestFakeButtonEvent(display, 1, 1, 0)
                                    dragging = True
                            x11.XFlush(display)
                        elif not pressed:
                            if touch_active:
                                if dragging:
                                    xtst.XTestFakeButtonEvent(display, 1, 0, 0)
                                else:
                                    button = classify_tap(
                                        time.monotonic() - touch_started,
                                        max_distance,
                                        scroll_started,
                                    )
                                    if button:
                                        click(xtst, x11, display, button)
                            touch_active = False
                            coordinates.reset()
                            fraction_x = fraction_y = 0.0
                            x11.XFlush(display)
    except (OSError, RuntimeError) as error:
        print("Relative touch failed: %s" % error, file=sys.stderr, flush=True)
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
