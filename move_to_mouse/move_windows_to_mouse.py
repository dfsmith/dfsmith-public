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
import argparse
import ctypes
import difflib
import os
import re
import sys
from ctypes import wintypes
from typing import Tuple, List

try:
    import win32gui
    import win32con
    import win32api
    import winreg
except Exception:
    print("Missing dependency: pywin32. Install with: pip install pywin32")
    raise

# --------------------------------------------------------------------------- #
# ctypes structures for ChangeDisplaySettingsEx (primary monitor switching)
# --------------------------------------------------------------------------- #
_ENUM_CURRENT_SETTINGS = -1
_CDS_UPDATEREGISTRY = 0x00000001
_CDS_NORESET = 0x10000000
_DM_POSITION = 0x00000020
_DISPLAY_DEVICE_ACTIVE = 0x00000001


class _POINTL(ctypes.Structure):
    _fields_ = [("x", wintypes.LONG), ("y", wintypes.LONG)]


class _DEVMODEW(ctypes.Structure):
    """DEVMODEW layout for display-device usage (display-device path through the union)."""
    _fields_ = [
        ("dmDeviceName",        ctypes.c_wchar * 32),   # 64 B
        ("dmSpecVersion",       wintypes.WORD),
        ("dmDriverVersion",     wintypes.WORD),
        ("dmSize",              wintypes.WORD),
        ("dmDriverExtra",       wintypes.WORD),
        ("dmFields",            wintypes.DWORD),         # offset 72
        # display-device union (16 B, same size as 8 printer SHORTs):
        ("dmPosition",          _POINTL),                # offset 76
        ("dmDisplayOrientation", wintypes.DWORD),
        ("dmDisplayFixedOutput", wintypes.DWORD),
        # shared fields:
        ("dmColor",             wintypes.SHORT),
        ("dmDuplex",            wintypes.SHORT),
        ("dmYResolution",       wintypes.SHORT),
        ("dmTTOption",          wintypes.SHORT),
        ("dmCollate",           wintypes.SHORT),
        ("dmFormName",          ctypes.c_wchar * 32),    # 64 B
        ("dmLogPixels",         wintypes.WORD),
        ("dmBitsPerPel",        wintypes.DWORD),
        ("dmPelsWidth",         wintypes.DWORD),
        ("dmPelsHeight",        wintypes.DWORD),
        ("dmDisplayFlags",      wintypes.DWORD),
        ("dmDisplayFrequency",  wintypes.DWORD),
        ("dmICMMethod",         wintypes.DWORD),
        ("dmICMIntent",         wintypes.DWORD),
        ("dmMediaType",         wintypes.DWORD),
        ("dmDitherType",        wintypes.DWORD),
        ("dmReserved1",         wintypes.DWORD),
        ("dmReserved2",         wintypes.DWORD),
        ("dmPanningWidth",      wintypes.DWORD),
        ("dmPanningHeight",     wintypes.DWORD),         # total 220 B
    ]


class _DISPLAY_DEVICEW(ctypes.Structure):
    _fields_ = [
        ("cb",           wintypes.DWORD),
        ("DeviceName",   ctypes.c_wchar * 32),
        ("DeviceString", ctypes.c_wchar * 128),
        ("StateFlags",   wintypes.DWORD),
        ("DeviceID",     ctypes.c_wchar * 128),
        ("DeviceKey",    ctypes.c_wchar * 128),
    ]


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


_TASKBAR_CLASSES = frozenset({"Shell_TrayWnd", "Shell_SecondaryTrayWnd", "Button", "Progman"})


def is_valid_window(hwnd: int) -> bool:
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
        if win32gui.GetClassName(hwnd) in _TASKBAR_CLASSES:
            return False
    except Exception:
        pass
    return True


def rect_center(rect: Tuple[int, int, int, int]) -> Tuple[float, float]:
    l, t, r, b = rect
    return ((l + r) / 2.0, (t + b) / 2.0)


def move_windows_to_monitor(target_mon: Tuple[int, int, int, int], verbose: bool = False) -> int:
    hwnds = enum_top_level_windows()
    moved = 0
    for hwnd in hwnds:
        try:
            if not is_valid_window(hwnd):
                continue
            if verbose:
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


def set_primary_monitor(target_rect: Tuple[int, int, int, int]) -> None:
    """
    Make the monitor at *target_rect* the Windows primary display by shifting
    all display positions so that target_rect's top-left becomes (0, 0).
    Windows then automatically moves the taskbar to the new primary monitor.
    """
    shift_x = target_rect[0]
    shift_y = target_rect[1]
    if shift_x == 0 and shift_y == 0:
        return  # already primary

    user32 = ctypes.windll.user32
    i = 0
    while True:
        dd = _DISPLAY_DEVICEW()
        dd.cb = ctypes.sizeof(_DISPLAY_DEVICEW)
        if not user32.EnumDisplayDevicesW(None, i, ctypes.byref(dd), 0):
            break
        i += 1
        if not (dd.StateFlags & _DISPLAY_DEVICE_ACTIVE):
            continue

        dm = _DEVMODEW()
        dm.dmSize = ctypes.sizeof(_DEVMODEW)
        if not user32.EnumDisplaySettingsW(dd.DeviceName, _ENUM_CURRENT_SETTINGS, ctypes.byref(dm)):
            continue

        dm.dmPosition.x -= shift_x
        dm.dmPosition.y -= shift_y
        dm.dmFields |= _DM_POSITION

        user32.ChangeDisplaySettingsExW(
            dd.DeviceName,
            ctypes.byref(dm),
            None,
            _CDS_UPDATEREGISTRY | _CDS_NORESET,
            None,
        )

    # Apply all queued changes at once.
    user32.ChangeDisplaySettingsExW(None, None, None, 0, None)
    print(f"Primary display set to monitor at {target_rect}.")


# --------------------------------------------------------------------------- #
# Audio: switch default playback device to the selected monitor's adapter
# --------------------------------------------------------------------------- #

def _get_monitor_device_name(target_rect: Tuple[int, int, int, int]) -> str:
    """Return the display device path (e.g. '\\\\.\\DISPLAY1') for the monitor."""
    hmon = win32api.MonitorFromPoint((target_rect[0], target_rect[1]))
    info = win32api.GetMonitorInfo(hmon)
    return info.get("Device", "").strip("\x00")


def _get_adapter_string(device_name: str) -> str:
    """Return the adapter description (e.g. 'NVIDIA GeForce RTX 4090') for a device path."""
    user32 = ctypes.windll.user32
    i = 0
    while True:
        dd = _DISPLAY_DEVICEW()
        dd.cb = ctypes.sizeof(_DISPLAY_DEVICEW)
        if not user32.EnumDisplayDevicesW(None, i, ctypes.byref(dd), 0):
            break
        i += 1
        if dd.DeviceName.rstrip("\x00").strip() == device_name.strip():
            return dd.DeviceString.rstrip("\x00").strip()
    return ""


def _get_monitor_registry_name(device_key: str) -> str:
    if not device_key:
        return ""

    key_path = device_key.rstrip("\x00").strip()
    for prefix in (r"\\Registry\\Machine\\", r"\\Registry\\Machine\\"):
        if key_path.startswith(prefix):
            key_path = key_path[len(prefix):]
            break

    try:
        with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, key_path) as key:
            for value_name in ("FriendlyName", "DeviceDesc", "Mfg"):
                try:
                    value, _ = winreg.QueryValueEx(key, value_name)
                except OSError:
                    continue
                if isinstance(value, str) and value:
                    return value
    except OSError:
        pass

    # Some monitor keys store the friendly name under Device Parameters.
    try:
        with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, key_path + r"\Device Parameters") as key:
            for value_name in ("FriendlyName", "DeviceDesc", "Mfg"):
                try:
                    value, _ = winreg.QueryValueEx(key, value_name)
                except OSError:
                    continue
                if isinstance(value, str) and value:
                    return value
    except OSError:
        pass

    return ""


def _parse_monitor_device_id(device_id: str) -> str:
    if not device_id:
        return ""
    try:
        parts = device_id.rstrip("\x00").split("\\")
    except Exception:
        return ""
    if len(parts) >= 2 and parts[0].upper() == "MONITOR":
        return parts[1]
    return ""


def _decode_wmi_string(value) -> str:
    if not value:
        return ""
    if isinstance(value, (list, tuple)):
        return "".join(chr(x) for x in value if x and x < 0x110000).strip()
    return str(value).strip()


def _get_monitor_wmi_name(device_id: str) -> str:
    parsed_id = _parse_monitor_device_id(device_id)
    if not parsed_id:
        return ""

    try:
        import win32com.client
        locator = win32com.client.Dispatch("WbemScripting.SWbemLocator")
        svc = locator.ConnectServer('.', 'root\\wmi')
        query = f"SELECT InstanceName, UserFriendlyName, ManufacturerName FROM WmiMonitorID"
        for item in svc.ExecQuery(query):
            instance = str(getattr(item, 'InstanceName', '') or '')
            if parsed_id.lower() not in instance.lower():
                continue
            name = _decode_wmi_string(getattr(item, 'UserFriendlyName', None))
            if name and name.lower() != 'generic pnp monitor':
                return name
            manufacturer = _decode_wmi_string(getattr(item, 'ManufacturerName', None))
            if manufacturer:
                return manufacturer
    except Exception:
        pass

    try:
        import win32com.client
        locator = win32com.client.Dispatch("WbemScripting.SWbemLocator")
        svc = locator.ConnectServer('.', 'root\\cimv2')
        sanitized = parsed_id.replace("'", "''")
        query = f"SELECT Name, Description FROM Win32_DesktopMonitor WHERE PNPDeviceID LIKE '%{sanitized}%'"
        for item in svc.ExecQuery(query):
            name = str(getattr(item, 'Name', '') or '').strip()
            if name and name.lower() != 'generic pnp monitor':
                return name
            desc = str(getattr(item, 'Description', '') or '').strip()
            if desc and desc.lower() != 'generic pnp monitor':
                return desc
    except Exception:
        pass

    return ""


def _get_monitor_friendly_name(target_rect: Tuple[int, int, int, int]) -> str:
    device_name = _get_monitor_device_name(target_rect)
    if not device_name:
        return ""

    user32 = ctypes.windll.user32
    i = 0
    while True:
        dd = _DISPLAY_DEVICEW()
        dd.cb = ctypes.sizeof(_DISPLAY_DEVICEW)
        if not user32.EnumDisplayDevicesW(device_name, i, ctypes.byref(dd), 0):
            break

        name = dd.DeviceString.rstrip("\x00").strip()
        device_key = dd.DeviceKey.rstrip("\x00").strip()
        device_id = dd.DeviceID.rstrip("\x00").strip()

        if name and name.lower() != "generic pnp monitor":
            return name
        registry_name = _get_monitor_registry_name(device_key)
        if registry_name and registry_name.lower() != "generic pnp monitor":
            return registry_name
        wmi_name = _get_monitor_wmi_name(device_id)
        if wmi_name and wmi_name.lower() != "generic pnp monitor":
            return wmi_name
        parsed_id = _parse_monitor_device_id(device_id)
        if parsed_id:
            return parsed_id

        i += 1

    return ""


def get_monitor_name(target_rect: Tuple[int, int, int, int]) -> str:
    monitor_name = _get_monitor_friendly_name(target_rect)
    device_name = _get_monitor_device_name(target_rect)
    adapter = _get_adapter_string(device_name)

    if monitor_name:
        if adapter and adapter.lower() not in monitor_name.lower():
            return f"{monitor_name} ({adapter})"
        return monitor_name
    if device_name and adapter:
        return f"{device_name} ({adapter})"
    if device_name:
        return device_name
    return "Unknown monitor"


def _set_default_audio_device(device_id: str) -> None:
    """
    Set the Windows default audio playback endpoint via the undocumented
    IPolicyConfig COM interface (works on Vista through Windows 11).
    """
    try:
        import comtypes
        import comtypes.client  # noqa: F401  (ensures COM runtime is initialised)
    except ImportError:
        raise ImportError("comtypes is required – install pycaw: pip install pycaw")

    # IPolicyConfig – undocumented COM interface present in AudioSes.dll.
    # CLSID: {870AF99C-171D-4F9E-AF0D-E63DF40C2BC9}
    # IID:   {F8679F50-850A-41CF-9C72-430F290290C8}
    class _IPolicyConfig(comtypes.IUnknown):
        _iid_ = comtypes.GUID("{F8679F50-850A-41CF-9C72-430F290290C8}")
        _methods_ = [
            # placeholder entries keep the vtable indices correct:
            comtypes.STDMETHOD(comtypes.HRESULT, "GetMixFormat"),
            comtypes.STDMETHOD(comtypes.HRESULT, "GetDeviceFormat"),
            comtypes.STDMETHOD(comtypes.HRESULT, "ResetDeviceFormat"),
            comtypes.STDMETHOD(comtypes.HRESULT, "SetDeviceFormat"),
            comtypes.STDMETHOD(comtypes.HRESULT, "GetProcessingPeriod"),
            comtypes.STDMETHOD(comtypes.HRESULT, "SetProcessingPeriod"),
            comtypes.STDMETHOD(comtypes.HRESULT, "GetShareMode"),
            comtypes.STDMETHOD(comtypes.HRESULT, "SetShareMode"),
            comtypes.STDMETHOD(comtypes.HRESULT, "GetPropertyValue"),
            comtypes.STDMETHOD(comtypes.HRESULT, "SetPropertyValue"),
            comtypes.STDMETHOD(
                comtypes.HRESULT,
                "SetDefaultEndpoint",
                [wintypes.LPWSTR, ctypes.c_uint],
            ),
            comtypes.STDMETHOD(comtypes.HRESULT, "SetEndpointVisibility"),
        ]

    policy = comtypes.CoCreateInstance(
        comtypes.GUID("{870AF99C-171D-4F9E-AF0D-E63DF40C2BC9}"),
        interface=_IPolicyConfig,
        clsctx=comtypes.CLSCTX_ALL,
    )
    # Apply for all three roles: eConsole=0, eMultimedia=1, eCommunications=2
    for role in range(3):
        policy.SetDefaultEndpoint(device_id, role)


def set_default_audio_for_monitor(target_rect: Tuple[int, int, int, int], debug: bool = False) -> bool:
    """
    Switch the Windows default audio output to the endpoint associated with
    the display adapter driving the monitor at *target_rect*.

    Matching is done by looking for an audio endpoint whose friendly name
    contains a distinctive word from the adapter description (e.g. "NVIDIA").

    Returns True if a device was found and set.
    """
    try:
        from pycaw.pycaw import AudioUtilities
    except ImportError:
        print("pycaw not installed; skipping audio switch. Run: pip install pycaw")
        return False

    device_name = _get_monitor_device_name(target_rect)
    adapter = _get_adapter_string(device_name)
    if not adapter:
        print(f"Could not identify display adapter for monitor at {target_rect}.")
        return False

    try:
        endpoints = AudioUtilities.GetAllDevices()
    except Exception as exc:
        print(f"Failed to enumerate audio devices: {exc}")
        return False

    monitor_name = get_monitor_name(target_rect)
    device_name = _get_monitor_device_name(target_rect)
    adapter_words = [w for w in adapter.split() if len(w) > 3]

    def normalize(text: str) -> str:
        text = str(text or "").lower().strip()
        text = re.sub(r"[^a-z0-9]+", " ", text)
        return " ".join(text.split())

    def token_set(text: str) -> set[str]:
        return set(normalize(text).split())

    monitor_norm = normalize(monitor_name)
    monitor_tokens = token_set(monitor_name)
    adapter_tokens = token_set(adapter)

    scored = []
    for ep in endpoints:
        fname = ep.FriendlyName or ""
        fname_norm = normalize(fname)
        fname_tokens = token_set(fname)

        monitor_score = 0.0
        if monitor_norm and monitor_norm in fname_norm:
            monitor_score += 200.0
        if monitor_norm and fname_norm in monitor_norm:
            monitor_score += 150.0
        if monitor_tokens and fname_tokens:
            overlap = len(monitor_tokens & fname_tokens)
            monitor_score += overlap * 40.0
            monitor_score += (overlap / max(1, len(monitor_tokens))) * 60.0
        monitor_score += difflib.SequenceMatcher(None, monitor_norm, fname_norm).ratio() * 20.0

        adapter_score = 0.0
        if any(word in fname_tokens for word in adapter_tokens):
            adapter_score += 20.0
        if device_name and device_name.lower() in fname_norm:
            adapter_score += 30.0

        total_score = monitor_score + adapter_score
        scored.append((monitor_score, adapter_score, total_score, ep))

    scored.sort(key=lambda item: (item[0], item[2], -len(item[3].FriendlyName or "")), reverse=True)

    if debug:
        print(f"Audio endpoint scoring for monitor '{monitor_name}':")
        for monitor_score, adapter_score, total_score, ep in scored:
            print(
                f"  {total_score:.1f}: {ep.FriendlyName!r} "
                f"(monitor={monitor_score:.1f}, adapter={adapter_score:.1f})"
            )

    best = None
    if scored:
        monitor_best = [item for item in scored if item[0] > 0]
        if monitor_best:
            best = monitor_best[0][3]
        elif not monitor_name:
            best = scored[0][3] if scored[0][2] >= 30 else None
        elif scored[0][2] >= 80:
            best = scored[0][3]

    if best is None and monitor_name:
        scores = []
        for ep in endpoints:
            fname = ep.FriendlyName or ""
            score = difflib.SequenceMatcher(None, monitor_norm, normalize(fname)).ratio()
            scores.append((score, ep))
        scores.sort(key=lambda item: item[0], reverse=True)
        if scores and scores[0][0] > 0.55:
            best = scores[0][1]

    if best is None:
        available = [ep.FriendlyName for ep in endpoints]
        print(
            f"No audio endpoint matched monitor '{monitor_name}' or adapter '{adapter}'. "
            f"Available: {available}"
        )
        return False

    try:
        _set_default_audio_device(best.id)
        print(f"Default audio output → {best.FriendlyName}")
        return True
    except Exception as exc:
        print(f"Failed to set default audio device: {exc}")
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
    parser = argparse.ArgumentParser(description="Move windows to the monitor containing the mouse cursor.")
    parser.add_argument("cmd", nargs="?", choices=["install", "register", "uninstall", "remove"], help="Install or uninstall desktop context-menu entry.")
    parser.add_argument("--debug-audio", action="store_true", help="Print audio endpoint scoring.")
    parser.add_argument("--verbose-windows", action="store_true", help="Print detailed window move output.")
    args = parser.parse_args()

    key_path = r"Software\Classes\Directory\Background\shell\MoveWindowsToMouse"
    if args.cmd in ("install", "register"):
        script_path = os.path.abspath(sys.argv[0])
        install_context_menu(key_path, script_path)
        return
    if args.cmd in ("uninstall", "remove"):
        uninstall_context_menu(key_path)
        return

    pt = win32api.GetCursorPos()
    target = get_monitor_rect_from_point(pt)
    monitor_name = get_monitor_name(target)
    print(f"Selected monitor: {monitor_name}")

    # Switch audio output before the coordinate system shifts.
    set_default_audio_for_monitor(target, debug=args.debug_audio)

    # Make the target monitor primary (shifts all display coordinates).
    shift_x, shift_y = target[0], target[1]
    set_primary_monitor(target)

    # Adjust target rect to post-shift coordinates (target is now at origin).
    if shift_x != 0 or shift_y != 0:
        target = (
            target[0] - shift_x,
            target[1] - shift_y,
            target[2] - shift_x,
            target[3] - shift_y,
        )

    moved = move_windows_to_monitor(target, verbose=args.verbose_windows)

    # Adjust target rect to post-shift coordinates (target is now at origin).
    if shift_x != 0 or shift_y != 0:
        target = (
            target[0] - shift_x,
            target[1] - shift_y,
            target[2] - shift_x,
            target[3] - shift_y,
        )

    moved = move_windows_to_monitor(target)
    print(f"Moved {moved} windows to monitor at {target}.")


if __name__ == "__main__":
    main()
