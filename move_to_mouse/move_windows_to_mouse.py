#!/usr/bin/env python3
"""
Move all top-level windows to the monitor containing the mouse cursor.
Also supports installing/uninstalling a Desktop background context-menu item
that launches this script.

Usage:
  python move_windows_to_mouse.py            # run action
  python move_windows_to_mouse.py install    # add context-menu (HKCU)
  python move_windows_to_mouse.py uninstall  # remove context-menu

Note: Requires pywin32. Use `pip install -r requirements.txt`.
"""
import os
import sys
from typing import Tuple, List
import subprocess

try:
    import win32gui
    import win32con
    import win32api
    import win32com.client
    import winreg
except Exception:
    print("Missing dependency: pywin32. Install with: pip install pywin32")
    raise


def get_monitor_rect_from_point(pt: Tuple[int, int]) -> Tuple[int, int, int, int]:
    hmon = win32api.MonitorFromPoint(pt)
    info = win32api.GetMonitorInfo(hmon)
    return tuple(info["Monitor"])


def enum_top_level_windows() -> List[int]:
    hwnds = []

    def _cb(hwnd, extra):
        hwnds.append(hwnd)
        return True

    win32gui.EnumWindows(_cb, None)
    return hwnds


def is_valid_window(hwnd: int) -> bool:
    if not win32gui.IsWindowVisible(hwnd):
        return False
    if win32gui.IsIconic(hwnd):
        return False
    desktop = win32gui.GetDesktopWindow()
    if hwnd == desktop:
        return False
    # check for task bar
    if hwnd == win32gui.FindWindow("Shell_TrayWnd", None):
        print(f"Taskbar window skipped: {hwnd}")
        return False
    style = win32gui.GetWindowLong(hwnd, win32con.GWL_STYLE)
    if style & win32con.WS_CHILD:
        return False
    try:
        cls = win32gui.GetClassName(hwnd)
        if cls in ("Shell_TrayWnd", "Button", "Progman"):
            return False
    except Exception:
        pass
    return True


def rect_center(rect: Tuple[int, int, int, int]) -> Tuple[float, float]:
    l, t, r, b = rect
    return ((l + r) / 2.0, (t + b) / 2.0)


def move_windows_to_monitor(target_mon: Tuple[int, int, int, int]) -> int:
    hwnds = enum_top_level_windows()
    moved = 0
    for hwnd in hwnds:
        try:
            if not is_valid_window(hwnd):
                continue
            print(f"Moving window {hwnd}: {win32gui.GetWindowText(hwnd)}")
            rect = win32gui.GetWindowRect(hwnd)
            w = rect[2] - rect[0]
            h = rect[3] - rect[1]
            center = rect_center(rect)

            cur_mon = win32api.MonitorFromPoint((int(center[0]), int(center[1])))
            cur_info = win32api.GetMonitorInfo(cur_mon)
            cur_rect = tuple(cur_info["Monitor"])

            rel_x = center[0] - cur_rect[0]
            rel_y = center[1] - cur_rect[1]

            new_center_x = target_mon[0] + rel_x
            new_center_y = target_mon[1] + rel_y

            new_left = int(new_center_x - w / 2)
            new_top = int(new_center_y - h / 2)

            mon_left, mon_top, mon_right, mon_bottom = target_mon
            max_left = mon_right - w
            max_top = mon_bottom - h
            if new_left < mon_left:
                new_left = mon_left
            if new_top < mon_top:
                new_top = mon_top
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


def set_default_audio_to_monitor(pt: Tuple[int, int]):
    """Attempt to set the system default audio playback device to the device
    associated with the monitor containing point `pt`.

    This function tries multiple fallbacks:
      - PowerShell with the AudioDeviceCmdlets module (preferred)
      - NirCmd `setdefaultsounddevice` if available

    If neither approach is available it prints a helpful message and
    returns False.
    """
    try:
        hmon = win32api.MonitorFromPoint(pt)
        info = win32api.GetMonitorInfo(hmon)
        mon_device = info.get("Device", "")
    except Exception:
        mon_device = ""
    mon = win32api.EnumDisplayDevices(mon_device, 0)
    print(f"Monitor device: {mon.DeviceName} ({mon.DeviceString}) {mon.DeviceID}")
    mon_key = mon.DeviceID
    driver_parts = mon_key.split("\\")
    print(f"Driver parts: {driver_parts}")
    key = "\\".join(["SYSTEM", "CurrentControlSet", "Enum", "DISPLAY", driver_parts[1]])
    current_driver_key = "\\".join(driver_parts[2:])

    friendly_name = None
    with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, key) as enum_key:
        print("registry values for monitor device:")
        instance_idx = 0
        while True:
            try:
                instance = winreg.EnumKey(enum_key, instance_idx)
                print(f"Instance: {instance}")
                # read the Driver key for this instance
                with winreg.OpenKey(
                    winreg.HKEY_LOCAL_MACHINE, "\\".join([key, instance])
                ) as driver_key:
                    try:
                        v = winreg.QueryValueEx(driver_key, "Driver")[0]
                        print(f"  Driver: {v=} {current_driver_key=}")
                        if v == current_driver_key:
                            friendly_name = winreg.QueryValueEx(
                                driver_key, "FriendlyName"
                            )[0]
                    except Exception:
                        print("  no Driver value")

                instance_idx += 1
            except OSError:
                break

    print(f"Monitor friendly name from registry: {mon.DeviceName=} {friendly_name=}")
    # Remove "Generic Monitor (x)" wrapper if present to get the actual model name for better matching
    if friendly_name and friendly_name.startswith("Generic Monitor ("):
        try:
            friendly_name = friendly_name.split("(", 1)[1].split(")", 1)[0].strip()
            print(f"Extracted friendly name: {friendly_name}")
        except Exception:
            pass

    # Read the names of audio render output devices and match to the driver key
    matched_audio_name = None
    matched_audio_key = None
    with winreg.OpenKey(
        winreg.HKEY_LOCAL_MACHINE,
        "\\".join(["SYSTEM", "CurrentControlSet", "Enum", "SWD", "MMDEVAPI"]),
    ) as audio_enum:
        print("Audio render endpoints:")
        audio_idx = 0
        while True:
            try:
                audio_id = winreg.EnumKey(audio_enum, audio_idx)
                print(f"Audio device id: {audio_id}")
                with winreg.OpenKey(audio_enum, audio_id) as audio_key:
                    #print(f"  registry values for audio device: {audio_key=}")
                    try:
                        audio_friendly_name = winreg.QueryValueEx(
                            audio_key, "FriendlyName"
                        )[0]
                        if friendly_name in audio_friendly_name:
                            print(
                                f"Matched audio device: {audio_id} with FriendlyName {audio_friendly_name}"
                            )
                            matched_audio_name = audio_friendly_name
                            matched_audio_key = audio_id
                        else:
                            #print(f"no match: {friendly_name=} not in {audio_friendly_name=}")
                            pass
                    except Exception:
                        print(f"nope {audio_enum=} {audio_id=}")
                        pass
                audio_idx += 1
            except OSError:
                break

    print(f"Matched audio device: {matched_audio_key=} {matched_audio_name=}")

    # Switch audio device using win32com.client
    if matched_audio_key:
        mm = win32com.client.Dispatch("Windows.MediaDeviceManager")

        playback_devices = mm.GetAudioRenderSelectorList()
        print(f"Playback devices: {playback_devices=}")
        return False

        try:
            mm.SetDefaultAudioPlaybackDevice(matched_audio_key)
            print(f"Switched default audio playback to {matched_audio_name}")
            return True

        except Exception as e:
            print(f"Failed to switch audio with Windows.MediaDeviceManager: {e}")
            pass
    return False

def install_context_menu(key_path: str, script_path: str):
    cmd_key_path = key_path + r"\command"
    try:
        with winreg.CreateKey(winreg.HKEY_CURRENT_USER, key_path) as k:
            winreg.SetValueEx(k, None, 0, winreg.REG_SZ, "Move windows to mouse")
        cmd = f'"{sys.executable}" "{script_path}"'
        with winreg.CreateKey(winreg.HKEY_CURRENT_USER, cmd_key_path) as ck:
            winreg.SetValueEx(ck, None, 0, winreg.REG_SZ, cmd)
        print("Context menu item installed (HKCU).")
    except Exception as e:
        print("Failed to install context menu:", e)


def _delete_tree(root, subkey):
    try:
        with winreg.OpenKey(root, subkey, 0, winreg.KEY_READ) as k:
            i = 0
            subs = []
            while True:
                try:
                    subs.append(winreg.EnumKey(k, i))
                    i += 1
                except OSError:
                    break
        for s in subs:
            _delete_tree(root, subkey + "\\" + s)
        winreg.DeleteKey(root, subkey)
    except FileNotFoundError:
        pass


def uninstall_context_menu(key_path: str):
    try:
        _delete_tree(winreg.HKEY_CURRENT_USER, key_path)
        print("Context menu item removed (HKCU).")
    except Exception as e:
        print("Failed to remove context menu:", e)


def main():
    if len(sys.argv) > 1:
        key_path = r"Software\Classes\Directory\Background\shell\MoveWindowsToMouse"
        cmd = sys.argv[1].lower()
        if cmd in ("install", "register"):
            script_path = os.path.abspath(sys.argv[0])
            install_context_menu(key_path, script_path)
            return
        if cmd in ("uninstall", "remove"):
            uninstall_context_menu(key_path)
            return

    pt = win32api.GetCursorPos()
    target = get_monitor_rect_from_point(pt)
    moved = move_windows_to_monitor(target)
    print(f"Moved {moved} windows to monitor at {target}.")
    # Attempt to switch default audio playback device to the monitor with the mouse
    set_default_audio_to_monitor(pt)

if __name__ == "__main__":
    main()
    import time

    # time.sleep(10)
