import importlib.util
from pathlib import Path
from unittest import mock
import unittest


MODULE_PATH = (
    Path(__file__).resolve().parents[2]
    / "buildroot-overlay/board/canaan/k230-soc/distribution/yahboom-touch-relative.py"
)
SPEC = importlib.util.spec_from_file_location("yahboom_touch_relative", MODULE_PATH)
TOUCH = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(TOUCH)
MODE_PATH = MODULE_PATH.with_name("yahboom-display-mode.py")
MODE_SPEC = importlib.util.spec_from_file_location("yahboom_display_mode", MODE_PATH)
MODE = importlib.util.module_from_spec(MODE_SPEC)
MODE_SPEC.loader.exec_module(MODE)


class TouchMappingTests(unittest.TestCase):
    def test_rotated_touch_corners_map_to_screen_corners(self):
        self.assertEqual(TOUCH.map_position(0, 0, 640, 480, 0, 480, 0, 640), (639, 479))
        self.assertEqual(TOUCH.map_position(480, 640, 640, 480, 0, 480, 0, 640), (0, 0))

    def test_rotated_delta_uses_incremental_motion(self):
        self.assertEqual(TOUCH.map_delta(120, 320, 640, 480, 480, 640), (-319.5, -119.75))

    def test_each_contact_starts_with_a_fresh_coordinate_baseline(self):
        coords = TOUCH.ContactCoordinates()
        coords.update(TOUCH.ABS_MT_POSITION_X, 100)
        coords.update(TOUCH.ABS_MT_POSITION_Y, 200)
        self.assertEqual(coords.begin(), (100, 200))
        coords.update(TOUCH.ABS_MT_POSITION_X, 120)
        coords.update(TOUCH.ABS_MT_POSITION_Y, 230)
        self.assertEqual(coords.offset(), (20, 30))

        coords.reset()
        coords.update(TOUCH.ABS_MT_POSITION_X, 500)
        coords.update(TOUCH.ABS_MT_POSITION_Y, 400)
        self.assertEqual(coords.begin(), (500, 400))
        self.assertEqual(coords.offset(), (0, 0))

    def test_multitouch_axes_ignore_legacy_absolute_emulation(self):
        coords = TOUCH.ContactCoordinates()
        coords.update(TOUCH.ABS_MT_POSITION_X, 100)
        coords.update(TOUCH.ABS_MT_POSITION_Y, 200)
        coords.update(TOUCH.ABS_X, 0)
        coords.update(TOUCH.ABS_Y, 0)
        self.assertEqual(coords.begin(), (100, 200))
        coords.update(TOUCH.ABS_MT_POSITION_X, 120)
        coords.update(TOUCH.ABS_MT_POSITION_Y, 230)
        coords.update(TOUCH.ABS_X, 0)
        coords.update(TOUCH.ABS_Y, 0)
        self.assertEqual(coords.offset(), (20, 30))

    def test_single_touch_absolute_axes_are_supported_as_fallback(self):
        coords = TOUCH.ContactCoordinates(TOUCH.ABS_X, TOUCH.ABS_Y)
        coords.update(TOUCH.ABS_MT_POSITION_X, 900)
        coords.update(TOUCH.ABS_MT_POSITION_Y, 900)
        coords.update(TOUCH.ABS_X, 300)
        coords.update(TOUCH.ABS_Y, 400)
        self.assertEqual(coords.begin(), (300, 400))
        coords.update(TOUCH.ABS_X, 330)
        coords.update(TOUCH.ABS_Y, 420)
        self.assertEqual(coords.offset(), (30, 20))

    def test_multiple_contacts_only_send_relative_motion(self):
        coords = TOUCH.ContactCoordinates()
        xtst = mock.Mock()
        pointer_x, pointer_y = 300, 200
        for origin_x, origin_y in ((100, 200), (400, 300)):
            coords.update(TOUCH.ABS_MT_POSITION_X, origin_x)
            coords.update(TOUCH.ABS_MT_POSITION_Y, origin_y)
            self.assertEqual(coords.begin(), (origin_x, origin_y))
            self.assertEqual(coords.offset(), (0, 0))
            coords.update(TOUCH.ABS_MT_POSITION_X, origin_x + 10)
            coords.update(TOUCH.ABS_MT_POSITION_Y, origin_y + 20)
            dx, dy = TOUCH.map_delta(*coords.offset(), 640, 480, 479, 639)
            TOUCH.move_pointer(xtst, "display", 0, int(dx), int(dy))
            pointer_x += int(dx)
            pointer_y += int(dy)
            coords.reset()
        self.assertEqual((pointer_x, pointer_y), (260, 180))
        self.assertEqual(xtst.XTestFakeRelativeMotionEvent.call_args_list, [
            mock.call("display", 0, -20, -10, 0),
            mock.call("display", 0, -20, -10, 0),
        ])

    def test_xtest_relative_motion_signature_includes_screen(self):
        x11, xtst = mock.MagicMock(), mock.MagicMock()
        with mock.patch.object(TOUCH.ctypes, "CDLL", side_effect=(x11, xtst)):
            TOUCH.load_xtest()
        self.assertEqual(len(xtst.XTestFakeRelativeMotionEvent.argtypes), 5)

    def test_single_finger_click_gestures(self):
        self.assertEqual(TOUCH.classify_tap(0.12, 0, False), 1)
        self.assertEqual(TOUCH.classify_tap(0.8, 0, False), 3)
        self.assertIsNone(TOUCH.classify_tap(0.12, 20, False))
        self.assertIsNone(TOUCH.classify_tap(0.12, 0, True))

    def test_hold_and_move_starts_left_drag(self):
        self.assertFalse(TOUCH.should_start_drag(0.49, 20))
        self.assertFalse(TOUCH.should_start_drag(0.6, 19.9))
        self.assertTrue(TOUCH.should_start_drag(0.5, 20))

    def test_right_edge_is_reserved_for_single_finger_scroll(self):
        self.assertFalse(TOUCH.is_scroll_edge(590, 640))
        self.assertTrue(TOUCH.is_scroll_edge(600, 640))

    def test_key_cycles_only_between_off_and_desktop(self):
        with mock.patch.object(MODE, "set_dpms") as set_dpms, \
                mock.patch.object(MODE, "start_touch") as start_touch:
            self.assertEqual(MODE.advance_mode("off"), "desktop")
            set_dpms.assert_called_once_with("on")
            start_touch.assert_called_once_with()
        with mock.patch.object(MODE, "set_dpms") as set_dpms, \
                mock.patch.object(MODE, "stop_touch") as stop_touch:
            self.assertEqual(MODE.advance_mode("desktop"), "off")
            set_dpms.assert_called_once_with("off")
            stop_touch.assert_called_once_with()

    def test_startup_keeps_display_off(self):
        with mock.patch.object(MODE.subprocess, "run") as run:
            with mock.patch.object(MODE, "set_dpms") as set_dpms:
                MODE.initialize_display()
        self.assertEqual(run.call_count, 4)
        set_dpms.assert_called_once_with("off")


if __name__ == "__main__":
    unittest.main()
