try:
    import win32gui
    import win32con
    import win32api
except Exception:
    print("Missing dependency: pywin32. Install with: pip install pywin32")
    raise


def _enum_top_level_windows() -> list[int]:
    hwnds = []

    def _cb(hwnd, extra):
        hwnds.append(hwnd)
        return True

    win32gui.EnumWindows(_cb, None)
    return hwnds


def _is_valid_window(hwnd: int) -> bool:
    if not win32gui.IsWindowVisible(hwnd):
        return False
    if win32gui.IsIconic(hwnd):
        return False
    if hwnd == win32gui.GetDesktopWindow():
        return False
    style = win32gui.GetWindowLong(hwnd, win32con.GWL_STYLE)
    if style & win32con.WS_CHILD:
        return False
    try:
        if win32gui.GetClassName(hwnd) in [
            "Shell_TrayWnd",
            "Shell_SecondaryTrayWnd",
            "Button",
            "Progman",
        ]:
            return False
    except Exception:
        pass
    return True


def _rect_center(rect: tuple[int, int, int, int]) -> tuple[float, float]:
    l, t, r, b = rect
    return ((l + r) / 2.0, (t + b) / 2.0)


def move_windows_to_display(
    target_display: tuple[int, int, int, int], verbose: bool = False
) -> int:
    hwnds = _enum_top_level_windows()
    moved = 0
    for hwnd in hwnds:
        try:
            if not _is_valid_window(hwnd):
                continue
            if verbose:
                print(f"Moving window {hwnd}: {win32gui.GetWindowText(hwnd)}")
            rect = win32gui.GetWindowRect(hwnd)
            w = rect[2] - rect[0]
            h = rect[3] - rect[1]
            center = _rect_center(rect)

            cur_disp = win32api.MonitorFromPoint((int(center[0]), int(center[1])))
            cur_info = win32api.GetMonitorInfo(cur_disp)
            cur_monitor = cur_info["Monitor"]
            cur_rect = (cur_monitor[0], cur_monitor[1], cur_monitor[2], cur_monitor[3])

            rel_x = center[0] - cur_rect[0]
            rel_y = center[1] - cur_rect[1]

            # Preserve the window's relative position when mapping to target display.
            new_center_x = target_display[0] + rel_x
            new_center_y = target_display[1] + rel_y

            new_left = int(new_center_x - w / 2)
            new_top = int(new_center_y - h / 2)

            left, top, right, bottom = target_display
            max_left = right - w
            max_top = bottom - h
            # Clamp so the full window remains on the target display.
            if new_left < left:
                new_left = left
            if new_top < top:
                new_top = top
            if new_left > max_left:
                new_left = max_left
            if new_top > max_top:
                new_top = max_top

            flags = win32con.SWP_NOZORDER | win32con.SWP_NOACTIVATE
            win32gui.SetWindowPos(hwnd, None, new_left, new_top, w, h, flags)
            moved += 1
        except Exception:
            continue
    return moved
