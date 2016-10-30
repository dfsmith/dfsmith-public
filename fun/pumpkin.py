# > pumpkin.py
# Daniel F. Smith, 2011

import goocanvas as gc
import cairo
import gtk
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

        def objtopumpkin(obj):
                print "find"
                while(obj != None):
                        print obj.get_data("feature")
                        if (obj.get_data("feature") == "pumpkin"):
                                return obj
                        obj = obj.get_parent()
                return None

        def on_drag(self, pk, target, event):
                if (pk == None):
                        return False
                if (event.state & gtk.gdk.BUTTON1_MASK):
                        (x, y, scale, angle) = pk.get_simple_transform()
                        pk.set_simple_transform((event.x_root),
                                                (event.y_root),
                                                scale, angle)
                        return True
                if (event.state & gtk.gdk.BUTTON3_MASK):
                        a,b,c,d,x,y = pk.get_transform()
                        a = fabs(event.x_root - x)/100
                        d = fabs(event.y_root - y)/100
                        pk.set_transform(cairo.Matrix(a,b,c,d,x,y))
                        return True
                return False

        def on_click(self, pk, target, ev):
                if (ev.button == 1):
                        self.excited += 100
                        return True
                elif (ev.button == 2):
                        self.root.remove()
                        return True

        def pumpkinate(self, pk):
                self.step += 1
                if (self.excited > 0):
                        self.step += 19 if (self.excited > 57) else self.excited/3
                        self.excited -= 1
                if (self.step > self.stepmax):
                        self.step = 0
                        self.stepmax = self.stepmax + random.randint(-200,200)
                        if (self.stepmax > 1200):
                                self.stepmax = 1200
                        if (self.stepmax < 200):
                                self.stepmax = 200
                        if (random.randint(0,10) == 0):
                                self.excited += 100

                theta = 2*pi*self.step / self.stepmax
                #pk.translate(0, sin(theta))

                if (self.linked==0 and fabs(self.ltheta - self.rtheta) < 3 and random.randint(0,4)==0):
                        self.linked = 200
                self.ltheta += 5*sin(theta)
                self.rtheta = self.ltheta if (self.linked>0) else self.rtheta + 360.0/self.stepmax
                if (self.linked > 0):
                        self.linked -= 1
                
                (x, y, scale, angle) = self.leye.get_simple_transform()
                self.leye.set_simple_transform(x, y, 1+0.1*sin(6*theta), self.ltheta)

                (x, y, scale, angle) = self.reye.get_simple_transform()
                self.reye.set_simple_transform(x, y, 1+0.1*sin(theta), self.rtheta)
                
                (x, y, scale, angle) = self.mouth.get_simple_transform()
                self.mouth.set_simple_transform(x, y, 1+0.1*sin(8*theta), angle)
                
                return True

        def eye(self, iris, angle, size):
                eye = gc.Group()
                eye.set_data("feature", "eye")
                ovoid = gc.Ellipse(parent=eye, radius_x=size, radius_y=size, fill_color="black")
                ovoid.set_data("feature", "eye socket")
                pupil = gc.Ellipse(parent=eye, radius_x=iris, radius_y=iris, fill_color="orange", stroke_pattern=None)
                ovoid.set_data("feature", "pupil")
                pupil.translate(cos(angle)*(size-0.7*iris),sin(angle)*(size-0.7*iris))
                return eye

        def __init__(self, x, y, width):
                root = gc.Group()
                root.set_data("feature", "pumpkin")
                root.translate(x,y)
                root.scale(width/100.0, width/100.0)
                
                pattern = cairo.RadialGradient(0, 0, 100, 0, 0, 75)
                pattern.add_color_stop_rgb(0, 0, 0, 0)
                pattern.add_color_stop_rgb(1, 1, 0.647, 0)
                body = gc.Ellipse(parent=root, radius_x=100, radius_y=60, fill_pattern=pattern)
                body.set_data("feature", "body")
                
                leye = self.eye(10,0,20)
                leye.translate(-50,-20)
                root.add_child(leye)

                reye = self.eye(10,0,20)
                reye.translate(+50,-20)
                root.add_child(reye)

                mouth = gc.Path(parent=root,
                                data="m -57 0 "
                                     "c 36.61252,28.83832  86.57620, 16.04709 113.58829,   2.9232"
                                     "  -9.21564,-8.84904 -20.49553,-15.43958 -37.58436, -17.12176 "
                                     "l  3.75843,11.27531  -14.61614,  2.08801  -3.75843,-12.5281"
                                     "  -8.76968, 1.25281   -1.25281, 10.44010 -15.4513,   1.67041 "
                                     "   2.08801,-10.44009"
                                     "c -14.75273,-1.12880 -29.20501,4.78653 -38.00196,10.44009"
                                     "z",
                                stroke_color=None, fill_color="black")
                mouth.translate(0,20)
                mouth.set_data("feature", "mouth")
                
                self.root = root
                self.body = body
                self.leye = leye
                self.reye = reye
                self.mouth = mouth
                self.stepmax = 1200
                self.step = 0

                root.connect("motion_notify_event", self.on_drag)
                root.connect("button_press_event", self.on_click)
                gtk.timeout_add(20, self.pumpkinate, root)
                                
def pumpframe():
	f = gtk.VBox(False,4)
	f.set_border_width(0)
	
	#f = gtk.Frame()
	#vbox.add(f)
	
	canvas = gc.Canvas()
	canvas.set_size_request(500, 500)
	canvas.set_bounds(0, 0, 1600, 1200)
	r = canvas.get_root_item()
	gc.Rect(parent=r, x=0, y=0, width=1600, height=1200, stroke_color=None, fill_color="black")
	
	r.add_child(Pumpkin(300,300,200).root)

	f.add(canvas)
	f.set_data("pumpkin_canvas", r)
	return f

def setwindowmax(w, state):
        if (state == True):
                w.set_decorated(False)
                w.fullscreen()
                w.set_data("fullscreen", True)
        else:
                w.set_data("fullscreen", False)
                w.unfullscreen()
                w.set_decorated(True)

def on_maximize(w, ev):
        if (not (ev.changed_mask & gtk.gdk.WINDOW_STATE_MAXIMIZED)):
                return False
        if (ev.new_window_state & gtk.gdk.WINDOW_STATE_MAXIMIZED):
                setwindowmax(w, True)
        return True

def on_click(w, ev, frame):
        if (ev.type == gtk.gdk._2BUTTON_PRESS):
                setwindowmax(w, not w.get_data("fullscreen"))
                return True
        if (ev.button == 1):
                canvas = frame.get_data("pumpkin_canvas")
                canvas.add_child(Pumpkin(ev.x, ev.y, 200).root)
                return True
        return False

def main():
	v = pumpframe()
	w = gtk.Window()
	w.connect("destroy", gtk.main_quit)
	w.connect("window_state_event", on_maximize)
	w.connect("button_press_event", on_click, v)
	w.add(v)
	w.show_all()
	
	gtk.main()

if __name__ == "__main__":
	main()
