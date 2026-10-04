import importlib.util
from pathlib import Path
from unittest import mock
import unittest


MODULE_PATH = (
    Path(__file__).resolve().parents[2]
    / "buildroot-overlay/board/canaan/k230-soc/distribution/yahboom-touch-absolute.py"
)
SPEC = importlib.util.spec_from_file_location("yahboom_touch_absolute", MODULE_PATH)
TOUCH = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(TOUCH)


class AbsoluteTouchTests(unittest.TestCase):
    def test_rotated_touch_corners_map_to_screen_corners(self):
        self.assertEqual(TOUCH.map_position(0, 0, 640, 480, 0, 480, 0, 640), (639, 479))
        self.assertEqual(TOUCH.map_position(480, 640, 640, 480, 0, 480, 0, 640), (0, 0))

    def test_short_tap_is_left_and_stationary_long_press_is_right(self):
        self.assertEqual(TOUCH.classify_tap(0.15, 0), 1)
        self.assertEqual(TOUCH.classify_tap(0.8, 0), 3)
        self.assertIsNone(TOUCH.classify_tap(0.8, 20))

    def test_drag_requires_hold_and_motion_threshold(self):
        self.assertFalse(TOUCH.should_start_drag(0.49, 25))
        self.assertFalse(TOUCH.should_start_drag(0.6, 19))
        self.assertTrue(TOUCH.should_start_drag(0.5, 20))

    def test_two_finger_scroll_accumulates_small_motion(self):
        steps, remainder = TOUCH.scroll_steps(20, 0)
        self.assertEqual((steps, remainder), (0, 20))
        steps, remainder = TOUCH.scroll_steps(20, remainder)
        self.assertEqual((steps, remainder), (1, 8))
        steps, remainder = TOUCH.scroll_steps(-40, remainder)
        self.assertEqual((steps, remainder), (-1, 0))

    def test_click_uses_requested_x_button(self):
        x11, xtst = mock.Mock(), mock.Mock()
        TOUCH.click(xtst, x11, "display", 3)
        self.assertEqual(xtst.XTestFakeButtonEvent.call_args_list, [
            mock.call("display", 3, 1, 0),
            mock.call("display", 3, 0, 0),
        ])

    def test_pointer_motion_is_absolute(self):
        xtst = mock.Mock()
        TOUCH.move_pointer(xtst, "display", 0, 123, 234)
        xtst.XTestFakeMotionEvent.assert_called_once_with("display", 0, 123, 234, 0)


if __name__ == "__main__":
    unittest.main()
