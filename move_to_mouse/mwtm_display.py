import ctypes
from ctypes import wintypes

try:
    import win32api
except Exception:
    print("Missing dependency: pywin32. Install with: pip install pywin32")
    raise

_QDC_ONLY_ACTIVE_PATHS = 0x00000002
_SDC_USE_SUPPLIED_DISPLAY_CONFIG = 0x00000020
_SDC_APPLY = 0x00000080
_SDC_NO_OPTIMIZATION = 0x00000100
_SDC_SAVE_TO_DATABASE = 0x00000200
_SDC_ALLOW_CHANGES = 0x00000400
_DISPLAYCONFIG_MODE_INFO_TYPE_SOURCE = 1


class _POINTL(ctypes.Structure):
    _fields_ = [("x", wintypes.LONG), ("y", wintypes.LONG)]


class _DISPLAYCONFIG_RATIONAL(ctypes.Structure):
    _fields_ = [("Numerator", wintypes.UINT), ("Denominator", wintypes.UINT)]


class _DISPLAYCONFIG_2DREGION(ctypes.Structure):
    _fields_ = [("cx", wintypes.UINT), ("cy", wintypes.UINT)]


class _DISPLAYCONFIG_VIDEO_SIGNAL_INFO(ctypes.Structure):
    _fields_ = [
        ("pixelRate", ctypes.c_uint64),
        ("hSyncFreq", _DISPLAYCONFIG_RATIONAL),
        ("vSyncFreq", _DISPLAYCONFIG_RATIONAL),
        ("activeSize", _DISPLAYCONFIG_2DREGION),
        ("totalSize", _DISPLAYCONFIG_2DREGION),
        ("videoStandard", wintypes.UINT),
        ("scanLineOrdering", wintypes.UINT),
    ]


class _DISPLAYCONFIG_TARGET_MODE(ctypes.Structure):
    _fields_ = [("targetVideoSignalInfo", _DISPLAYCONFIG_VIDEO_SIGNAL_INFO)]


class _DISPLAYCONFIG_SOURCE_MODE(ctypes.Structure):
    _fields_ = [
        ("width", wintypes.UINT),
        ("height", wintypes.UINT),
        ("pixelFormat", wintypes.UINT),
        ("position", _POINTL),
    ]


class _DISPLAYCONFIG_DESKTOP_IMAGE_INFO(ctypes.Structure):
    _fields_ = [
        ("PathSourceSize", _POINTL),
        ("DesktopImageRegion", wintypes.RECT),
        ("DesktopImageClip", wintypes.RECT),
    ]


class _DISPLAYCONFIG_MODE_INFO_UNION(ctypes.Union):
    _fields_ = [
        ("targetMode", _DISPLAYCONFIG_TARGET_MODE),
        ("sourceMode", _DISPLAYCONFIG_SOURCE_MODE),
        ("desktopImageInfo", _DISPLAYCONFIG_DESKTOP_IMAGE_INFO),
    ]


class _DISPLAYCONFIG_MODE_INFO(ctypes.Structure):
    _anonymous_ = ("_u",)
    _fields_ = [
        ("infoType", wintypes.UINT),
        ("id", wintypes.UINT),
        ("adapterId", ctypes.c_uint64),
        ("_u", _DISPLAYCONFIG_MODE_INFO_UNION),
    ]


class _DISPLAYCONFIG_PATH_SOURCE_INFO(ctypes.Structure):
    _fields_ = [
        ("adapterId", ctypes.c_uint64),
        ("id", wintypes.UINT),
        ("modeInfoIdx", wintypes.UINT),
        ("statusFlags", wintypes.UINT),
    ]


class _DISPLAYCONFIG_PATH_TARGET_INFO(ctypes.Structure):
    _fields_ = [
        ("adapterId", ctypes.c_uint64),
        ("id", wintypes.UINT),
        ("modeInfoIdx", wintypes.UINT),
        ("outputTechnology", wintypes.UINT),
        ("rotation", wintypes.UINT),
        ("scaling", wintypes.UINT),
        ("refreshRate", _DISPLAYCONFIG_RATIONAL),
        ("scanLineOrdering", wintypes.UINT),
        ("targetAvailable", wintypes.BOOL),
        ("statusFlags", wintypes.UINT),
    ]


class _DISPLAYCONFIG_PATH_INFO(ctypes.Structure):
    _fields_ = [
        ("sourceInfo", _DISPLAYCONFIG_PATH_SOURCE_INFO),
        ("targetInfo", _DISPLAYCONFIG_PATH_TARGET_INFO),
        ("flags", wintypes.UINT),
    ]


class Display:
    @staticmethod
    def set_primary(target_rect: tuple[int, int, int, int]) -> tuple[str | None, str | None]:
        """Set the target monitor to be the primary display.

        Returns (display_name, None) on success, otherwise (None, error_message).
        """
        def _fmt_error(code: int) -> str:
            try:
                return f"error {code}: {ctypes.FormatError(code).strip()}"
            except Exception:
                return f"error {code}: unknown error"

        shift_x = target_rect[0]
        shift_y = target_rect[1]
        target_name = Display.name(target_rect)
        # The target is already primary when its origin is at (0, 0).
        if shift_x == 0 and shift_y == 0:
            return target_name, None

        user32 = ctypes.windll.user32
        num_paths = wintypes.UINT(0)
        num_modes = wintypes.UINT(0)
        ret = user32.GetDisplayConfigBufferSizes(
            _QDC_ONLY_ACTIVE_PATHS,
            ctypes.byref(num_paths),
            ctypes.byref(num_modes),
        )
        if ret != 0:
            return None, f"GetDisplayConfigBufferSizes failed: {_fmt_error(ret)}"

        paths = (_DISPLAYCONFIG_PATH_INFO * num_paths.value)()
        modes = (_DISPLAYCONFIG_MODE_INFO * num_modes.value)()

        ret = user32.QueryDisplayConfig(
            _QDC_ONLY_ACTIVE_PATHS,
            ctypes.byref(num_paths),
            paths,
            ctypes.byref(num_modes),
            modes,
            None,
        )
        if ret != 0:
            return None, f"QueryDisplayConfig(data) failed: {_fmt_error(ret)}"

        # Shift source modes so the target monitor lands at (0, 0).
        for i in range(num_modes.value):
            if modes[i].infoType == _DISPLAYCONFIG_MODE_INFO_TYPE_SOURCE:
                modes[i].sourceMode.position.x -= shift_x
                modes[i].sourceMode.position.y -= shift_y

        ret = user32.SetDisplayConfig(
            num_paths.value,
            paths,
            num_modes.value,
            modes,
            _SDC_APPLY
            | _SDC_USE_SUPPLIED_DISPLAY_CONFIG
            | _SDC_SAVE_TO_DATABASE
            | _SDC_ALLOW_CHANGES,
        )
        if ret != 0:
            return None, f"SetDisplayConfig failed: {_fmt_error(ret)}"

        return target_name, None

    @staticmethod
    def pointer_position() -> tuple[int, int]:
        return win32api.GetCursorPos()

    @staticmethod
    def rect_from_point(pt: tuple[int, int]) -> tuple[int, int, int, int]:
        hmon = win32api.MonitorFromPoint(pt)
        info = win32api.GetMonitorInfo(hmon)
        monitor = info["Monitor"]
        return (monitor[0], monitor[1], monitor[2], monitor[3])

    @staticmethod
    def device_name(target_rect: tuple[int, int, int, int]) -> str:
        hmon = win32api.MonitorFromPoint((target_rect[0], target_rect[1]))
        info = win32api.GetMonitorInfo(hmon)
        return info.get("Device", "").strip("\x00")

    @staticmethod
    def adapter_name(device_name: str) -> str:
        """Return the adapter description (e.g. 'NVIDIA GeForce RTX 4090') for a device path."""
        i = 0
        while True:
            try:
                dd = win32api.EnumDisplayDevices(None, i)
            except Exception:
                break
            i += 1
            if getattr(dd, "DeviceName", "").strip() == device_name.strip():
                return getattr(dd, "DeviceString", "").strip()
        return ""

    @staticmethod
    def _wmi_name(device_id: str) -> str:
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
            svc = locator.ConnectServer(".", "root\\wmi")
            for item in svc.ExecQuery(
                "SELECT InstanceName, UserFriendlyName, ManufacturerName FROM WmiMonitorID"
            ):
                if (
                    monitor_id.lower()
                    not in str(getattr(item, "InstanceName", "") or "").lower()
                ):
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
            svc = locator.ConnectServer(".", "root\\cimv2")
            sanitized = monitor_id.replace("'", "''")
            for item in svc.ExecQuery(
                f"SELECT Name, Description FROM Win32_DesktopMonitor WHERE PNPDeviceID LIKE '%{sanitized}%'"
            ):
                for attr in ("Name", "Description"):
                    value = str(getattr(item, attr, "") or "").strip()
                    if value and value.lower() != "generic pnp monitor":
                        return value
        except Exception:
            pass

        return ""

    @staticmethod
    def _friendly_name_for_device(device_name: str) -> str:
        if not device_name:
            return ""
        i = 0
        while True:
            try:
                dd = win32api.EnumDisplayDevices(device_name, i)
            except Exception:
                break
            name = getattr(dd, "DeviceString", "").strip()
            if name and name.lower() != "generic pnp monitor":
                return name
            # Fall back to WMI when display APIs return generic labels.
            wmi_name = Display._wmi_name(getattr(dd, "DeviceID", "").strip())
            if wmi_name and wmi_name.lower() != "generic pnp monitor":
                return wmi_name
            i += 1
        return ""

    @staticmethod
    def name_from_device(device_name: str) -> str:
        display_name = Display._friendly_name_for_device(device_name)
        adapter = Display.adapter_name(device_name)

        if display_name:
            if adapter and adapter.lower() not in display_name.lower():
                return f"{display_name} ({adapter})"
            return display_name
        if device_name and adapter:
            return f"{device_name} ({adapter})"
        if device_name:
            return device_name
        return "Unknown monitor"

    @staticmethod
    def name(target_rect: tuple[int, int, int, int]) -> str:
        device_name = Display.device_name(target_rect)
        return Display.name_from_device(device_name)
