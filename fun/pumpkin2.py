# > pumpkin.py
# Daniel F. Smith, 2011.
# Updated 2025 with github AI to update obsolete goocanvas library.

import sys
import os
import logging

try:
    import gi

    gi.require_version("GooCanvas", "2.0")
    gi.require_version("Gtk", "3.0")
    from gi.repository import GooCanvas as gc
    from gi.repository import Gtk, Gdk, GLib
except Exception as e:
    sys.stderr.write("Error: PyGObject / GooCanvas 2 not available.\n")
    sys.stderr.write(
        "On Debian/Ubuntu: sudo apt install gir1.2-goocanvas-2.0 python3-gi python3-gi-cairo python3-cairo\n"
    )
    sys.stderr.write(
        "On Fedora: sudo dnf install python3-gobject python3-cairo goocanvas2\n"
    )
    sys.stderr.write("Details: %s\n" % e)
    sys.exit(1)

# logging: enable debug by setting environment variable PUMPKIN_DEBUG=1
logger = logging.getLogger(__name__)
logging.basicConfig(
    level=logging.DEBUG if os.environ.get("PUMPKIN_DEBUG") else logging.WARNING
)

import cairo
from math import sin, cos, pi, fabs
import random


class Pumpkin:

    root = None
    body = None
    leye = None
    reye = None
    nose = None
    mouth = None

    ltheta = 0
    rtheta = 0

    step = 0
    stepmax = 1200
    excited = 0
    linked = 0

    @staticmethod
    def objtopumpkin(obj):
        logger.debug("objtopumpkin: starting search")
        while obj is not None:
            logger.debug("obj feature: %r", getattr(obj, "feature", None))
            if getattr(obj, "feature", None) == "pumpkin":
                return obj
            obj = obj.get_parent()
        return None

    def on_drag(self, pk, target, event):
        if pk is None:
            return False
        state = getattr(event, "state", 0)
        if state & Gdk.ModifierType.BUTTON1_MASK:
            has, x, y, scale, angle = pk.get_simple_transform()
            pk.set_simple_transform(event.x_root, event.y_root, scale, angle)
            return True
        if state & Gdk.ModifierType.BUTTON3_MASK:
            has, transform = pk.get_transform()
            # transform is a cairo.Matrix with fields xx, yx, xy, yy, x0, y0
            a = fabs(event.x_root - transform.x0) / 100
            d = fabs(event.y_root - transform.y0) / 100
            pk.set_transform(
                cairo.Matrix(
                    a, transform.yx, transform.xy, d, transform.x0, transform.y0
                )
            )
            return True
        return False

    def on_click(self, pk, target, ev):
        if ev.button == 1:
            self.excited += 100
            logger.debug("pumpkin excited: %r", self)
            return True
        elif ev.button == 2:
            # remove this pumpkin from its parent
            parent = self.root.get_parent()
            try:
                parent.remove_child(self.root)
                logger.debug("removed pumpkin %r from parent %r", self, parent)
            except Exception:
                try:
                    self.root.remove()
                    logger.debug("removed pumpkin %r via root.remove()", self)
                except Exception:
                    logger.exception("failed to remove pumpkin %r", self)
            return True

    def pumpkinate(self, pk):
        self.step += 1
        if self.excited > 0:
            self.step += 19 if (self.excited > 57) else (self.excited // 3)
            self.excited -= 1
        if self.step > self.stepmax:
            self.step = 0
            self.stepmax = self.stepmax + random.randint(-200, 200)
            if self.stepmax > 1200:
                self.stepmax = 1200
            if self.stepmax < 200:
                self.stepmax = 200
            if random.randint(0, 10) == 0:
                self.excited += 100

        theta = 2 * pi * self.step / self.stepmax
        # pk.translate(0, sin(theta))

        if (
            self.linked == 0
            and fabs(self.ltheta - self.rtheta) < 3
            and random.randint(0, 4) == 0
        ):
            self.linked = 200
        self.ltheta += 5 * sin(theta)
        self.rtheta = (
            self.ltheta if (self.linked > 0) else self.rtheta + 360.0 / self.stepmax
        )
        if self.linked > 0:
            self.linked -= 1

        has, x, y, scale, angle = self.leye.get_simple_transform()
        self.leye.set_simple_transform(x, y, 1 + 0.1 * sin(6 * theta), self.ltheta)

        has, x, y, scale, angle = self.reye.get_simple_transform()
        self.reye.set_simple_transform(x, y, 1 + 0.1 * sin(theta), self.rtheta)

        has, x, y, scale, angle = self.mouth.get_simple_transform()
        self.mouth.set_simple_transform(x, y, 1 + 0.1 * sin(8 * theta), angle)

        return True

    def eye(self, iris, angle, size):
        eye = gc.CanvasGroup()
        eye.feature = "eye"
        ovoid = gc.CanvasEllipse(
            parent=eye, radius_x=size, radius_y=size, fill_color="black"
        )
        ovoid.feature = "eye socket"
        pupil = gc.CanvasEllipse(
            parent=eye,
            radius_x=iris,
            radius_y=iris,
            fill_color="orange",
            stroke_pattern=None,
        )
        pupil.feature = "pupil"
        pupil.translate(
            cos(angle) * (size - 0.7 * iris), sin(angle) * (size - 0.7 * iris)
        )
        return eye

    def __init__(self, x, y, width):
        root = gc.CanvasGroup()
        root.feature = "pumpkin"
        root.translate(x, y)
        root.scale(width / 100.0, width / 100.0)

        # Use a simple fill color (GooCanvas 2 expects a CairoPattern wrapper for gradients)
        body = gc.CanvasEllipse(
            parent=root, radius_x=100, radius_y=60, fill_color="orange"
        )
        body.feature = "body"

        leye = self.eye(10, 0, 20)
        leye.translate(-50, -20)
        root.add_child(leye, -1)

        reye = self.eye(10, 0, 20)
        reye.translate(+50, -20)
        root.add_child(reye, -1)

        mouth = gc.CanvasPath(
            parent=root,
            data="m -57 0 "
            "c 36.61252,28.83832  86.57620, 16.04709 113.58829,   2.9232"
            "  -9.21564,-8.84904 -20.49553,-15.43958 -37.58436, -17.12176 "
            "l  3.75843,11.27531  -14.61614,  2.08801  -3.75843,-12.5281"
            "  -8.76968, 1.25281   -1.25281, 10.44010 -15.4513,   1.67041 "
            "   2.08801,-10.44009"
            "c -14.75273,-1.12880 -29.20501,4.78653 -38.00196,10.44009"
            "z",
            stroke_color=None,
            fill_color="black",
        )
        mouth.translate(0, 20)
        mouth.feature = "mouth"

        self.root = root
        self.body = body
        self.leye = leye
        self.reye = reye
        self.mouth = mouth
        self.stepmax = 1200
        self.step = 0

        root.connect("motion-notify-event", self.on_drag)
        root.connect("button-press-event", self.on_click)
        GLib.timeout_add(20, self.pumpkinate, root)


def pumpframe():
    f = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=4)
    f.set_border_width(0)

    canvas = gc.Canvas()
    canvas.set_size_request(500, 500)
    canvas.set_bounds(0, 0, 1600, 1200)
    r = canvas.get_root_item()
    gc.CanvasRect(
        parent=r,
        x=0,
        y=0,
        width=1600,
        height=1200,
        stroke_color=None,
        fill_color="black",
    )

    r.add_child(Pumpkin(300, 300, 200).root, -1)

    f.pack_start(canvas, True, True, 0)
    f.pumpkin_canvas = r
    return f


def setwindowmax(w, state):
    if state is True:
        w.set_decorated(False)
        w.fullscreen()
        w.fullscreen = True
    else:
        w.fullscreen = False
        w.unfullscreen()
        w.set_decorated(True)


def on_maximize(w, ev):
    if not (ev.changed_mask & Gdk.WindowState.MAXIMIZED):
        return False
    if ev.new_window_state & Gdk.WindowState.MAXIMIZED:
        setwindowmax(w, True)
    return True


def on_click(w, ev, frame):
    if ev.type == Gdk.EventType._2BUTTON_PRESS:
        setwindowmax(w, not getattr(w, "fullscreen", False))
        return True
    if ev.button == 1:
        canvas = getattr(frame, "pumpkin_canvas", None)
        canvas.add_child(Pumpkin(ev.x, ev.y, 200).root, -1)
        return True
    return False


def main():
    v = pumpframe()
    w = Gtk.Window()
    w.connect("destroy", Gtk.main_quit)
    w.connect("window-state-event", on_maximize)
    w.connect("button-press-event", on_click, v)
    w.add(v)
    w.show_all()

    Gtk.main()


if __name__ == "__main__":
    main()
