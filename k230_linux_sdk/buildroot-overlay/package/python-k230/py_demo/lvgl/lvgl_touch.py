#!/usr/bin/env python3
"""
LVGL Touch Blackboard Demo for K230
- Black background, draw green line following touch
- Real-time XY coordinate display in center
"""

import lvgl as lv


def main():
    lv.init()
    lv.init_k230()

    print("[lvgl] initialized")

    scr = lv.screen_active()
    scr.set_style_bg_color(lv.color_black(), lv.SELECTOR.DEFAULT)

    coord_label = lv.label(scr)
    coord_label.set_text("X:0  Y:0")
    coord_label.set_style_text_color(lv.color_white(), lv.SELECTOR.DEFAULT)
    coord_label.align(lv.ALIGN.CENTER, 0, 0)

    indev = lv.indev_get_next(None)
    prev_point = [None]

    def on_press(event):
        if not indev:
            return
        pt = lv.indev_get_point(indev)
        if not pt:
            return

        x = int(pt[0])
        y = int(pt[1])

        coord_label.set_text(f"X:{x}  Y:{y}")

        if prev_point[0] is not None:
            line = lv.line(scr)
            line.set_points([(prev_point[0], prev_point[1]), (x, y)])
            line.set_style_line_color(lv.color_make(0, 255, 0), lv.SELECTOR.DEFAULT)
            line.set_style_line_width(4, lv.SELECTOR.DEFAULT)
        else:
            dot = lv.obj(scr)
            dot.set_size(6, 6)
            dot.set_pos(x - 3, y - 3)
            dot.set_style_bg_color(lv.color_make(0, 255, 0), lv.SELECTOR.DEFAULT)
            dot.set_style_bg_opa(lv.OPA_COVER, lv.SELECTOR.DEFAULT)
            dot.set_style_radius(lv.RADIUS_CIRCLE, lv.SELECTOR.DEFAULT)
            dot.set_style_border_width(0, lv.SELECTOR.DEFAULT)

        prev_point[0] = x
        prev_point[1] = y

    def on_release(event):
        prev_point[0] = None
        prev_point[1] = None

    scr.add_event_cb(on_press, lv.EVENT.PRESSING, None)
    scr.add_event_cb(on_release, lv.EVENT.RELEASED, None)

    print("LVGL touch blackboard running... Press Ctrl+C to exit")
    lv.run()


if __name__ == "__main__":
    main()
