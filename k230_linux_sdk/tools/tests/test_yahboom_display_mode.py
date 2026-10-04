import pathlib
import runpy
import unittest
from unittest import mock


SCRIPT = (pathlib.Path(__file__).resolve().parents[2] /
          "buildroot-overlay/board/canaan/k230-soc/distribution/yahboom-display-mode.py")
DISPLAY = runpy.run_path(str(SCRIPT))


class DisplayModeTests(unittest.TestCase):
    def test_off_to_desktop(self):
        advance = DISPLAY["advance_mode"]
        with mock.patch.dict(advance.__globals__, {
            "set_dpms": mock.Mock(), "start_touch": mock.Mock()
        }) as functions:
            self.assertEqual(advance("off"), "desktop")
            functions["set_dpms"].assert_called_once_with("on")
            functions["start_touch"].assert_called_once_with()

    def test_desktop_to_off(self):
        advance = DISPLAY["advance_mode"]
        with mock.patch.dict(advance.__globals__, {
            "set_dpms": mock.Mock(), "stop_touch": mock.Mock()
        }) as functions:
            self.assertEqual(advance("desktop"), "off")
            functions["set_dpms"].assert_called_once_with("off")
            functions["stop_touch"].assert_called_once_with()


if __name__ == "__main__":
    unittest.main()
