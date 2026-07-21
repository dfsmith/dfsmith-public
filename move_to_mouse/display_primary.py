import ctypes
from ctypes import wintypes

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


def set_primary_monitor(target_rect: tuple[int, int, int, int]) -> None:
    """Set the target monitor to be the primary display."""
    shift_x = target_rect[0]
    shift_y = target_rect[1]
    if shift_x == 0 and shift_y == 0:
        return

    user32 = ctypes.windll.user32
    num_paths = wintypes.UINT(0)
    num_modes = wintypes.UINT(0)
    ret = user32.QueryDisplayConfig(
        _QDC_ONLY_ACTIVE_PATHS,
        ctypes.byref(num_paths), None,
        ctypes.byref(num_modes), None,
        None,
    )
    if ret != 0:
        raise ctypes.WinError(ret)

    paths = (_DISPLAYCONFIG_PATH_INFO * num_paths.value)()
    modes = (_DISPLAYCONFIG_MODE_INFO * num_modes.value)()

    ret = user32.QueryDisplayConfig(
        _QDC_ONLY_ACTIVE_PATHS,
        ctypes.byref(num_paths), paths,
        ctypes.byref(num_modes), modes,
        None,
    )
    if ret != 0:
        raise ctypes.WinError(ret)

    for i in range(num_modes.value):
        if modes[i].infoType == _DISPLAYCONFIG_MODE_INFO_TYPE_SOURCE:
            modes[i].sourceMode.position.x -= shift_x
            modes[i].sourceMode.position.y -= shift_y

    ret = user32.SetDisplayConfig(
        num_paths.value, paths,
        num_modes.value, modes,
        _SDC_APPLY | _SDC_USE_SUPPLIED_DISPLAY_CONFIG | _SDC_SAVE_TO_DATABASE | _SDC_ALLOW_CHANGES,
    )
    if ret != 0:
        raise ctypes.WinError(ret)

    print(f"Primary display set to monitor at {target_rect}.")
