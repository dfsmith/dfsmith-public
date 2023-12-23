#!/usr/bin/env python3
"""
Fun with Novation Launchpad S midi controller.
See
   Programmer's Reference Manual "Launchpad S" by Ben Supper, 
   2013 Focusrite Audio Engineering Ltd.
for details
"""
import mido
import math
import time


class LaunchPadS:

    def rg(red, green, blink: bool = False):
        return red << 0 | green << 4 | (0b0001000 if blink else 0b00001100)
    color_codes = {
        "black":        rg(0, 0),
        "red":          rg(3, 0),
        "red_dull":     rg(1, 0),
        "red_blink":    rg(2, 0, True),
        "green":        rg(0, 3),
        "green_dull":   rg(0, 1),
        "green_blink":  rg(0, 2, True),
        "yellow":       rg(3, 3),
        "yellow_dull":  rg(1, 1),
        "yellow_blink": rg(2, 2, True),
    }
    port_name = "Launchpad S"

    # special return values for process_input(): normally returns (button, current_value)
    unknown = (-1, -1)  # something went wrong
    quit = (-1, 0)     # the quit button was pressed
    multi = (-1, 1)    # multiple button states were altered: rescan
    control = (-1, 2)  # a control message was handled
    release = (-2, 0)  # a button was released: button number in [1]

    def __init__(self, index: int = 0):
        indevs = [p for p in mido.get_input_names() if LaunchPadS.port_name in p]
        outdevs = [p for p in mido.get_output_names()
                   if LaunchPadS.port_name in p]
        if len(indevs) < index or len(outdevs) < index:

            raise RuntimeError(
                f"LaunchPad S at index {index} not found: {indevs}/{outdevs}"
            )
        self.inport = mido.open_input(indevs[index])
        self.outport = mido.open_output(outdevs[index])
        self.button = [0] * (8 * 16)

        # build awkward brightness table
        def gamma(intensity: float) -> float: return math.pow(intensity, 1.0/3)
        self.intensity_table = [-1] * 256
        max_intensity = gamma(16.0/3)
        max_index = 0
        for num in range(1, 16):
            for den in range(3, 18):
                intensity = float(num) / den
                if num < 9:
                    code = 16*(num-1)+(den-3)
                else:
                    code = 128+16*(num-9)+(den-3)
                index = math.floor((len(self.intensity_table)-1)
                                   * gamma(intensity)/max_intensity)
                # print(f"{num} {den} {intensity} {gamma(intensity)}/{max} 0x{code:x} {index}")
                if (num, den) == (1, 5):
                    self.default_intensity = index
                if self.intensity_table[index] < 0:
                    self.intensity_table[index] = code
                max_index = max(index, max_index)
        self.current_intensity = self.default_intensity

        code = self.intensity_table[max_index]
        for idx in range(len(self.intensity_table)-1, -1, -1):
            if self.intensity_table[idx] < 0:
                self.intensity_table[idx] = code
            code = self.intensity_table[idx]

        # for i, b in enumerate(self.intensity_table):
        #    print(f"{i} 0x{b:x}")

        self.color_cycle = [LaunchPadS.color_codes[c]
                            for c in ["black", "green", "yellow", "red", "red_blink"]]
        self.wipe_cycle = [LaunchPadS.color_codes[c]
                           for c in ["black", "green_dull", "green", "yellow", "red", "red_dull"]]
        self.reset()

    def reset(self):
        self.clear()
        self.flashmode()
        # Ordered X-Y layout.
        self.send([0xB0, 0x00, 0x01])

    def flashmode(self, on: bool = True):
        # Double buffer flash mode.
        self.send([0xB0, 0x00, 0x28 if on else 0x20])

    def clear(self):
        # Clear buttons
        self.send([0xB0, 0x00, 0x00])

    def send(self, bytes: list[int]):
        self.outport.send(mido.Message.from_bytes(bytes))

    def put(self, msg: mido.Message):
        self.outport.send(msg)

    def get(self) -> mido.Message:
        return self.inport.receive()

    def scroll(self, color: int, msg: str):
        self.flashmode(False)
        init = [0xF0, 0x00, 0x20, 0x29, 0x09]
        self.send(init + [color] + [ord(c) for c in msg] + [0xF7])
        while True:
            done = self.get()
            if done.is_cc() and done.value == 3:
                break
        self.flashmode()

    def brightness(self, intensity: int = -1):
        if intensity == -1:
            idx = self.default_intensity
        else:
            idx = max(0, min(intensity, len(self.intensity_table)-1))
        code = self.intensity_table[idx]
        controller = 0x1E if code < 128 else 0x1F
        self.send([0xB0, controller, code & 0x7F])
        self.current_intensity = idx

    def fade(self, intensity: int = -1):
        if intensity == -1:
            intensity = self.default_intensity
        dir = +1 if intensity > self.current_intensity else -1
        delay = 2.0 / (dir*(intensity - self.current_intensity))
        for idx in range(self.current_intensity + dir, intensity+dir, dir):
            self.brightness(idx)
            time.sleep(delay)

    def pulse(self):
        self.fade(255)
        self.fade(0)
        self.fade()

    def next_color(self, color_idx: int, palette: [int]) -> int:
        return color_idx+1 if color_idx+1 < len(palette) else 0

    def idx_set(self, idx: int, color: int):
        self.put(mido.Message("note_on", note=idx, velocity=color))

    def xy_set(self, x: int, y: int, color: int):
        self.idx_set(y*16+x, color)

    def button_set(self, idx: int, color_idx: int, palette: int = 0):
        cycle_code = [
            self.color_cycle,
            self.wipe_cycle,
        ][palette]
        if idx < 0 or idx > len(self.button):
            raise RuntimeError(
                f"Button index out of range(0:{len(self.button)-1}): {idx}")
        self.button[idx] = color_idx % len(cycle_code)
        self.idx_set(idx, cycle_code[self.button[idx]])
        return self.button[idx]

    def button_get(self, idx) -> int:
        return self.button[idx]

    def button_inc(self, idx) -> int:
        return self.button_set(idx, self.next_color(self.button_get(idx), self.color_cycle))

    def button_pattern(self, pattern: str):
        row = 0
        for raster in pattern.split('\n'):
            if raster.isspace():
                continue
            col = 0
            for color in raster:
                if color.isspace():
                    continue
                self.button_set(16*row+col, ".1234".index(color))
                col += 1
            row += 1

    def show_image(self, image: [[int]]):
        for row, raster in enumerate(image):
            for col, color in enumerate(raster):
                self.xy_set(col, row, color)

    def happy(self):
        self.button_pattern(
            """........
               ..1..1..
               .121121.
               ..1..1..
               ........
               .1....1.
               ..1111..
               ........
            """
        )

    def wipe(self):
        for w in range(-len(self.wipe_cycle), 8+8+len(self.wipe_cycle)):
            for x in range(8):
                for y in range(8):
                    col = x+y - w
                    if col < 0:
                        continue
                    if col >= len(self.wipe_cycle):
                        break
                    self.button_set(16*y+x, col, 1)
            time.sleep(0.01)

    def refresh(self):
        for idx, col in enumerate(self.button):
            print(f"{idx} {col}")
            self.button_set(idx, col)

    def process_input(self) -> (int, int):
        msg = self.get()
        # print(msg)
        if msg.is_cc() and msg.value != 0:
            function = msg.control - 0x68
            actions = {
                0: (lambda: self.wipe()),
                1: (lambda: self.happy()),
                2: (lambda: self.scroll(LaunchPadS.color_codes["green"], "send")),
                3: (lambda: self.brightness(0)),
                4: (lambda: self.brightness()),
                5: (lambda: self.brightness(255)),
                6: (lambda: self.pulse()),
                7: (lambda: LaunchPadS.quit),
            }
            if function in actions:
                rc = actions[function]()
                return rc if rc else LaunchPadS.multi
            return LaunchPadS.control
        elif msg.type == "note_on":
            if msg.velocity > 0:
                idx = msg.note
                col = idx % 16
                color_idx = self.button_inc(idx)
                if col == 8:
                    row = idx // 16
                    for c in range(0, 8):
                        self.button_set(row*16+c, color_idx)
                        time.sleep(0.05)
                    return LaunchPadS.multi
                return (idx, color_idx)
            else:
                return (LaunchPadS.release[0], msg.note)
        return LaunchPadS.unknown

    def show_video(filename: str, frametime_ms: int = 1):
        import cv2
        import numpy as np

        cap = cv2.VideoCapture(filename)
        frames = int(cap.get(cv2.CAP_PROP_FRAME_COUNT))
        width = int(cap.get(cv2.CAP_PROP_FRAME_WIDTH))
        height = int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT))
        print((frames, width, height))

        buf = np.empty((height, width, 3), np.dtype("uint8"))
        frame_num = 0
        cap.set(cv2.CAP_PROP_POS_FRAMES, 5000)
        while frame_num < min(frames, 2500):
            ret, frame = cap.read()
            miniframe = cv2.resize(frame, (8, 8), interpolation=cv2.INTER_AREA)
            image = [[0]*8 for i in range(8)]
            for sy in range(8):
                for sx in range(8):
                    bgr = miniframe[sy][sx]
                    image[sy][sx] = LaunchPadS.rg(
                        (4*bgr[2])//256, (4*bgr[1])//256)
                    miniframe[sy][sx][0] = 0
            # superframe=cv2.resize(miniframe,(480,480),interpolation=cv2.INTER_NEAREST)
            pad.show_image(image)
            cv2.imshow('video', frame)
            cv2.waitKey(frametime_ms)
            frame_num += 1


if __name__=="__main__":
    pad = LaunchPadS()

    pad.brightness(0)
    pad.happy()
    pad.fade(255)
    pad.fade()
    pad.wipe()

    while True:
        button, val = pad.process_input()
        print([button, val])
        if (button, val) == LaunchPadS.quit:
            break

    pad.show_video(
        r"D:\dfsmith\nextCloud\Movies\Comedy\South Park Bigger Longer and Uncut (1999).m4v")
