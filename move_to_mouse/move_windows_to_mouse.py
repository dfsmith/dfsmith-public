#!/usr/bin/env python3
"""Move windows to the monitor containing the mouse cursor.

Example:
  python move_windows_to_mouse.py            # run action
  python move_windows_to_mouse.py install    # add Windows context menu entry
  python move_windows_to_mouse.py uninstall  # remove context menu entry

Install dependencies:
  pip install -r requirements.txt
"""
import argparse
import ctypes
import difflib
import os
import re
import sys
from ctypes import wintypes
from display_primary import set_primary_monitor


try:
    import win32gui
    import win32con
    import win32api
    import winreg
except Exception:
    print("Missing dependency: pywin32. Install with: pip install pywin32")
    raise

# --------------------------------------------------------------------------- #

# Use win32api.EnumDisplayDevices via pywin32 instead of a ctypes struct.


def get_monitor_rect_from_point(pt: tuple[int, int]) -> tuple[int, int, int, int]:
    hmon = win32api.MonitorFromPoint(pt)
    info = win32api.GetMonitorInfo(hmon)
    monitor = info["Monitor"]
    return (monitor[0], monitor[1], monitor[2], monitor[3])


def enum_top_level_windows() -> list[int]:
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


def rect_center(rect: tuple[int, int, int, int]) -> tuple[float, float]:
    l, t, r, b = rect
    return ((l + r) / 2.0, (t + b) / 2.0)


def move_windows_to_monitor(target_mon: tuple[int, int, int, int], verbose: bool = False) -> int:
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
            cur_monitor = cur_info["Monitor"]
            cur_rect = (cur_monitor[0], cur_monitor[1], cur_monitor[2], cur_monitor[3])

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




# --------------------------------------------------------------------------- #
# Audio: switch default playback device to the selected monitor's adapter
# --------------------------------------------------------------------------- #

def _get_monitor_device_name(target_rect: tuple[int, int, int, int]) -> str:
    hmon = win32api.MonitorFromPoint((target_rect[0], target_rect[1]))
    info = win32api.GetMonitorInfo(hmon)
    return info.get("Device", "").strip("\x00")


def _get_adapter_string(device_name: str) -> str:
    """Return the adapter description (e.g. 'NVIDIA GeForce RTX 4090') for a device path."""
    i = 0
    while True:
        try:
            dd = win32api.EnumDisplayDevices(None, i)
        except Exception:
            break
        i += 1
        if getattr(dd, 'DeviceName', '').strip() == device_name.strip():
            return getattr(dd, 'DeviceString', '').strip()
    return ""


def _get_monitor_wmi_name(device_id: str) -> str:
    parts = (device_id or "").rstrip("\x00").split("\\")
    if len(parts) < 2 or parts[0].upper() != "MONITOR":
        return ""
    monitor_id = parts[1]

    def decode(value):
        if not value:
            return ""
        if isinstance(value, (list, tuple)):
            return "".join(chr(x) for x in value if x and x < 0x110000).strip()
        return str(value).strip()

    try:
        import win32com.client
        locator = win32com.client.Dispatch("WbemScripting.SWbemLocator")
        svc = locator.ConnectServer('.', 'root\\wmi')
        for item in svc.ExecQuery("SELECT InstanceName, UserFriendlyName, ManufacturerName FROM WmiMonitorID"):
            if monitor_id.lower() not in str(getattr(item, 'InstanceName', '') or '').lower():
                continue
            for attr in ("UserFriendlyName", "ManufacturerName"):
                value = decode(getattr(item, attr, None))
                if value and value.lower() != "generic pnp monitor":
                    return value
    except Exception:
        pass

    try:
        import win32com.client
        locator = win32com.client.Dispatch("WbemScripting.SWbemLocator")
        svc = locator.ConnectServer('.', 'root\\cimv2')
        sanitized = monitor_id.replace("'", "''")
        for item in svc.ExecQuery(
            f"SELECT Name, Description FROM Win32_DesktopMonitor WHERE PNPDeviceID LIKE '%{sanitized}%'"
        ):
            for attr in ("Name", "Description"):
                value = str(getattr(item, attr, '') or '').strip()
                if value and value.lower() != "generic pnp monitor":
                    return value
    except Exception:
        pass

    return ""


def _get_monitor_friendly_name(target_rect: tuple[int, int, int, int]) -> str:
    device_name = _get_monitor_device_name(target_rect)
    if not device_name:
        return ""
    i = 0
    while True:
        try:
            dd = win32api.EnumDisplayDevices(device_name, i)
        except Exception:
            break
        name = getattr(dd, 'DeviceString', '').strip()
        if name and name.lower() != 'generic pnp monitor':
            return name
        wmi_name = _get_monitor_wmi_name(getattr(dd, 'DeviceID', '').strip())
        if wmi_name and wmi_name.lower() != 'generic pnp monitor':
            return wmi_name
        i += 1
    return ""


def get_monitor_name(target_rect: tuple[int, int, int, int]) -> str:
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


def _set_default_audio_device(device_id: str, debug: bool = False) -> None:
    """Set the Windows default audio playback endpoint.

    Try the direct PolicyConfig COM path first, but keep the PowerShell/
    SoundVolumeView fallback in place when direct COM is unavailable or fails.
    """
    errors: list[str] = []
    if debug:
        print(f"  [audio] trying direct PolicyConfig COM for device id={device_id}")
    try:
        _set_default_audio_endpoint_com(device_id, debug=debug)
        return
    except Exception as exc:
        errors.append(f"Direct PolicyConfig COM: {exc}")
        if debug:
            print(f"  [audio] PolicyConfig COM exception: {exc}")

    try:
        from audio_fallback import set_default_audio_device_fallback
        set_default_audio_device_fallback(device_id)
        return
    except Exception as exc:
        errors.append(f"Fallback audio method: {exc}")

    raise RuntimeError("Failed to set default audio device.\n" + "\n".join(errors))


def _set_default_audio_endpoint_com(device_id: str, debug: bool = False) -> None:
    """Attempt to set the default endpoint via PolicyConfig COM."""
    from ctypes import c_ulong, c_wchar_p
    from comtypes import CLSCTX_INPROC_SERVER, GUID, CoCreateInstance

    try:
        from pycaw.api.policyconfig import IPolicyConfig
    except ImportError:
        raise RuntimeError(
            "pycaw.api.policyconfig is required for direct PolicyConfig COM access. "
            "Install pycaw and try again."
        )

    _CLSID_PCC = GUID("{870af99c-171d-4f9e-af0d-e63df40c2bc9}")
    try:
        policy = CoCreateInstance(_CLSID_PCC, IPolicyConfig, clsctx=CLSCTX_INPROC_SERVER)
    except Exception as exc:
        raise RuntimeError(f"CoCreateInstance PolicyConfigClient failed: {exc}") from exc
    if policy is None:
        raise RuntimeError("CoCreateInstance PolicyConfigClient returned None")

    slot_errors: list[str] = []
    for role in (0, 1, 2):
        try:
            hr = policy.SetDefaultEndpoint(device_id, c_ulong(role))
        except Exception as exc:
            slot_errors.append(f"role={role} exception={exc}")
            if debug:
                print(f"  [audio] PolicyConfig role={role} exception: {exc}")
            continue

        if hr == 0:
            if debug:
                print(f"  [audio] PolicyConfig SetDefaultEndpoint succeeded for role={role}")
            return

        slot_errors.append(f"role={role} hr=0x{hr:08x}")
        if debug:
            print(f"  [audio] PolicyConfig role={role} hr=0x{hr:08x}")

    raise RuntimeError(
        f"PolicyConfig SetDefaultEndpoint failed: {'; '.join(slot_errors)}"
    )


def set_default_audio_for_monitor(target_rect: tuple[int, int, int, int], debug: bool = False) -> bool:
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
    # adapter_words was unused; tokenized adapter is stored in adapter_tokens

    def normalize(text: str) -> str:
        text = str(text or "").lower().strip()
        text = re.sub(r"[^a-z0-9]+", " ", text)
        return " ".join(text.split())

    monitor_norm = normalize(monitor_name)
    monitor_tokens = set(monitor_norm.split())
    adapter_tokens = set(normalize(adapter).split())

    scored = []
    for ep in endpoints:
        fname = ep.FriendlyName or ""
        fname_norm = normalize(fname)
        fname_tokens = set(fname_norm.split())

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
                f"  {total_score:.1f}: {ep.FriendlyName!r} (id={ep.id}) "
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

    # Prefer endpoints whose GUID matches a PnP audio device for this monitor/adapter.
    # This helps when multiple logical endpoints have identical friendly names
    # but different MMDevice GUIDs (the case you observed: 3650... vs b070...).
    try:
        pnp_guids = set()
        try:
            import win32com.client
            locator = win32com.client.Dispatch("WbemScripting.SWbemLocator")
            svc = locator.ConnectServer('.', 'root\\cimv2')
            # Collect GUIDs from PnP audio endpoints whose names contain
            # tokens from the monitor name or the adapter description.
            query = "SELECT PNPDeviceID, Name FROM Win32_PnPEntity WHERE PNPClass='AudioEndpoint'"
            for item in svc.ExecQuery(query):
                name = str(getattr(item, 'Name', '') or '').lower()
                # tokenize the PnP name similar to normalize()
                name_tokens = set(re.sub(r"[^a-z0-9]+", " ", name).split())
                if (monitor_tokens and (name_tokens & monitor_tokens)) or (adapter_tokens and (name_tokens & adapter_tokens)):
                    pid = str(getattr(item, 'PNPDeviceID', '') or '')
                    # Extract all GUID-like brace groups and add them.
                    for m in re.finditer(r"\{([0-9A-Fa-f\-]{36})\}", pid):
                        pnp_guids.add(m.group(1).lower())
        except Exception:
            pnp_guids = set()

        if pnp_guids:
            if debug:
                print(f"  [audio] PnP GUIDs for adapter/monitor: {sorted(pnp_guids)}")
            guid_matches = []
            for monitor_score, adapter_score, total_score, ep in scored:
                eid = (ep.id or '').lower()
                for g in pnp_guids:
                    if g in eid:
                        guid_matches.append((total_score, ep))
                        break
            if guid_matches:
                guid_matches.sort(key=lambda t: t[0], reverse=True)
                best = guid_matches[0][1]
                if debug:
                    print(f"  [audio] Prefer endpoint by PnP GUID: {best.FriendlyName!r} (id={best.id})")
    except Exception:
        pass

    if best is None:
        available = [ep.FriendlyName for ep in endpoints]
        print(
            f"No audio endpoint matched monitor '{monitor_name}' or adapter '{adapter}'. "
            f"Available: {available}"
        )
        return False

    def _get_current_default_id() -> str:
        """Return the device-id of the current default playback endpoint, or ''."""
        try:
            import comtypes
            import comtypes.client  # noqa: F401
            from pycaw.pycaw import IMMDeviceEnumerator, EDataFlow, ERole
            # MMDeviceEnumerator CLSID – always registered on Windows Vista+
            _CLSID_MMDevEnum = comtypes.GUID("{BCDE0395-E52F-467C-8E3D-C4579291692E}")
            enumerator = comtypes.CoCreateInstance(
                _CLSID_MMDevEnum,
                IMMDeviceEnumerator,
                comtypes.CLSCTX_INPROC_SERVER,
            )
            # GetDefaultAudioEndpoint is a COM method; getattr avoids Pylance noise.
            dev = getattr(enumerator, "GetDefaultAudioEndpoint")(
                EDataFlow.eRender.value,
                ERole.eConsole.value,
            )
            return getattr(dev, "GetId")()
        except Exception as exc:
            print(f"  [debug] cannot read current default audio device: {exc}")
            return ""

    def _id_to_name(dev_id: str) -> str:
        for ep in endpoints:
            if ep.id == dev_id:
                return ep.FriendlyName or dev_id
        return dev_id or "(unknown)"

    before_id = _get_current_default_id()
    print(f"Audio default before: {_id_to_name(before_id)!r}")
    print(f"Audio target:         {best.FriendlyName!r}  (id={best.id})")

    try:
        _set_default_audio_device(best.id, debug=debug)
    except Exception as exc:
        print(f"Failed to set default audio device: {exc}")
        return False

    # Verification is done inside the C# exe (STA, correct COM context).
    # The exe prints SUCCESS or SUCCESS_UNCONFIRMED with before/after device IDs.
    return True


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
    parser = argparse.ArgumentParser(
        description="Move windows to the monitor containing the mouse cursor.",
        epilog=(
            "Commands:\n"
            "  install   Add the script to the desktop background context menu.\n"
            "  uninstall Remove the context menu entry.\n"
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument(
        "cmd",
        nargs="?",
        choices=["install", "uninstall"],
        help="Install or uninstall the desktop context-menu entry.",
    )
    parser.add_argument("--debug-audio", action="store_true", help="Print audio endpoint scoring.")
    parser.add_argument("--verbose-windows", action="store_true", help="Print detailed window move output.")
    args = parser.parse_args()

    key_path = r"Software\Classes\Directory\Background\shell\MoveWindowsToMouse"
    if args.cmd == "install":
        script_path = os.path.abspath(sys.argv[0])
        install_context_menu(key_path, script_path)
        return
    if args.cmd == "uninstall":
        uninstall_context_menu(key_path)
        return

    # Move the windows to the display with the mouse pointer.
    pt = win32api.GetCursorPos()
    target = get_monitor_rect_from_point(pt)
    monitor_name = get_monitor_name(target)
    print(f"Selected monitor: {monitor_name}")

    moved = move_windows_to_monitor(target, verbose=args.verbose_windows)
    print(f"Moved {moved} windows to monitor at {target}.")

    set_primary_monitor(target)
    set_default_audio_for_monitor(target, debug=args.debug_audio)


if __name__ == "__main__":
    main()
