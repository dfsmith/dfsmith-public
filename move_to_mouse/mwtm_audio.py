import difflib
import re

from mwtm_display import Display


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
        from mwtm_audio_fallback import set_default_audio_device_fallback

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
        policy = CoCreateInstance(
            _CLSID_PCC, IPolicyConfig, clsctx=CLSCTX_INPROC_SERVER
        )
    except Exception as exc:
        raise RuntimeError(
            f"CoCreateInstance PolicyConfigClient failed: {exc}"
        ) from exc
    if policy is None:
        raise RuntimeError("CoCreateInstance PolicyConfigClient returned None")

    set_default_endpoint = getattr(policy, "SetDefaultEndpoint", None)
    if not callable(set_default_endpoint):
        raise RuntimeError("PolicyConfig object does not expose SetDefaultEndpoint")

    # Set all three roles so app defaults track the device consistently.
    slot_errors: list[str] = []
    for role in (0, 1, 2):
        try:
            hr = set_default_endpoint(device_id, c_ulong(role))
        except Exception as exc:
            slot_errors.append(f"role={role} exception={exc}")
            if debug:
                print(f"  [audio] PolicyConfig role={role} exception: {exc}")
            continue

        if hr == 0:
            if debug:
                print(
                    f"  [audio] PolicyConfig SetDefaultEndpoint succeeded for role={role}"
                )
            return

        slot_errors.append(f"role={role} hr=0x{hr:08x}")
        if debug:
            print(f"  [audio] PolicyConfig role={role} hr=0x{hr:08x}")

    raise RuntimeError(
        f"PolicyConfig SetDefaultEndpoint failed: {'; '.join(slot_errors)}"
    )


def move_default_audio_to_display(
    target_rect: tuple[int, int, int, int], debug: bool = False
) -> tuple[str | None, str | None]:
    """
    Switch the Windows default audio output to the endpoint associated with
    the display adapter driving the monitor at *target_rect*.

    Matching is done by looking for an audio endpoint whose friendly name
    contains a distinctive word from the adapter description (e.g. "NVIDIA").

    Returns (endpoint_name, None) on success, otherwise (None, error_message).
    """
    try:
        from pycaw.pycaw import AudioUtilities
    except ImportError:
        return None, "pycaw not installed; run: pip install pycaw"

    device_name = Display.device_name(target_rect)
    adapter = Display.adapter_name(device_name)
    if not adapter:
        return None, f"Could not identify display adapter for monitor at {target_rect}."

    try:
        endpoints = AudioUtilities.GetAllDevices()
    except Exception as exc:
        return None, f"Failed to enumerate audio devices: {exc}"

    monitor_name = Display.name(target_rect)

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
        monitor_score += (
            difflib.SequenceMatcher(None, monitor_norm, fname_norm).ratio() * 20.0
        )

        adapter_score = 0.0
        if any(word in fname_tokens for word in adapter_tokens):
            adapter_score += 20.0
        if device_name and device_name.lower() in fname_norm:
            adapter_score += 30.0

        total_score = monitor_score + adapter_score
        scored.append((monitor_score, adapter_score, total_score, ep))

    scored.sort(
        # Prefer monitor-name match first, then total score, then shorter labels.
        key=lambda item: (item[0], item[2], -len(item[3].FriendlyName or "")),
        reverse=True,
    )

    if debug:
        print(f"Audio endpoint scoring for monitor '{monitor_name}':")
        for monitor_score, adapter_score, total_score, ep in scored:
            print(
                f"  {total_score:.1f}: {ep.FriendlyName!r} (id={ep.id}) "
                f"(monitor={monitor_score:.1f}, adapter={adapter_score:.1f})"
            )

    best = None
    if scored:
        # Strong monitor-name evidence wins even if adapter-only score is lower.
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
            score = difflib.SequenceMatcher(
                None, monitor_norm, normalize(fname)
            ).ratio()
            scores.append((score, ep))
        scores.sort(key=lambda item: item[0], reverse=True)
        if scores and scores[0][0] > 0.55:
            best = scores[0][1]

    # Use PnP GUID matches to break ties between similar endpoint names.
    try:
        pnp_guids = set()
        try:
            import win32com.client

            locator = win32com.client.Dispatch("WbemScripting.SWbemLocator")
            svc = locator.ConnectServer(".", "root\\cimv2")
            # Collect GUIDs for PnP endpoints matching monitor/adapter tokens.
            query = "SELECT PNPDeviceID, Name FROM Win32_PnPEntity WHERE PNPClass='AudioEndpoint'"
            for item in svc.ExecQuery(query):
                name = str(getattr(item, "Name", "") or "").lower()
                # Tokenize PnP names with the same normalization rules.
                name_tokens = set(re.sub(r"[^a-z0-9]+", " ", name).split())
                if (monitor_tokens and (name_tokens & monitor_tokens)) or (
                    adapter_tokens and (name_tokens & adapter_tokens)
                ):
                    pid = str(getattr(item, "PNPDeviceID", "") or "")
                    # Extract GUID-like brace groups from the PnP ID.
                    for m in re.finditer(r"\{([0-9A-Fa-f\-]{36})\}", pid):
                        pnp_guids.add(m.group(1).lower())
        except Exception:
            pnp_guids = set()

        if pnp_guids:
            if debug:
                print(f"  [audio] PnP GUIDs for adapter/monitor: {sorted(pnp_guids)}")
            guid_matches = []
            for monitor_score, adapter_score, total_score, ep in scored:
                eid = (ep.id or "").lower()
                for g in pnp_guids:
                    if g in eid:
                        # Keep score ordering among endpoints that share a matching GUID.
                        guid_matches.append((total_score, ep))
                        break
            if guid_matches:
                guid_matches.sort(key=lambda t: t[0], reverse=True)
                best = guid_matches[0][1]
                if debug:
                    print(
                        f"  [audio] Prefer endpoint by PnP GUID: {best.FriendlyName!r} (id={best.id})"
                    )
    except Exception:
        pass

    if best is None:
        available = [ep.FriendlyName for ep in endpoints]
        return None, (
            f"No audio endpoint matched monitor '{monitor_name}' or adapter '{adapter}'. "
            f"Available: {available}"
        )

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
    if debug:
        print(f"Audio default before: {_id_to_name(before_id)!r}")
        print(f"Audio target:         {best.FriendlyName!r}  (id={best.id})")

    try:
        _set_default_audio_device(best.id, debug=debug)
    except Exception as exc:
        return None, f"Failed to set default audio device: {exc}"

    return best.FriendlyName or best.id, None
