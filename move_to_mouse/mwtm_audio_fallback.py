import os
import shutil
import subprocess
import urllib.request
import zipfile


def set_default_audio_device_fallback(device_id: str) -> None:
    """Fallback methods to set the default audio device when direct COM fails."""
    errors: list[str] = []

    ps_exe = shutil.which("pwsh") or shutil.which("powershell")
    if ps_exe:
        ps_install = r"""
$ErrorActionPreference='Stop'
if (-not (Get-Module -ListAvailable -Name AudioDeviceCmdlets)) {
    Install-Module AudioDeviceCmdlets -Force -Scope CurrentUser -AllowClobber -Repository PSGallery
}
Import-Module AudioDeviceCmdlets -Force
Set-AudioDevice -ID $env:MOVE_AUDIO_DEVICE_ID
Write-Output "SUCCESS"
"""
        env = os.environ.copy()
        env["MOVE_AUDIO_DEVICE_ID"] = device_id
        try:
            print("  [audio] trying AudioDeviceCmdlets fallback (may install on first run)...")
            r = subprocess.run(
                [ps_exe, "-NoProfile", "-Sta", "-Command", ps_install],
                capture_output=True,
                text=True,
                timeout=120,
                env=env,
            )
            if r.returncode == 0 and "SUCCESS" in r.stdout:
                return
            errors.append(
                f"AudioDeviceCmdlets rc={r.returncode}: "
                f"STDOUT: {(r.stdout or '').strip()[:400]} STDERR: {(r.stderr or '').strip()[:400]}"
            )
        except subprocess.TimeoutExpired:
            errors.append("AudioDeviceCmdlets: install/run timed out after 120s")
        except Exception as exc:
            errors.append(f"AudioDeviceCmdlets tier: {exc}")

    svv_dir = os.path.join(os.path.expanduser("~"), ".move_to_mouse")
    svv_exe = os.path.join(svv_dir, "SoundVolumeView.exe")
    svv_url = "https://www.nirsoft.net/utils/soundvolumeview-x64.zip"
    try:
        if not os.path.exists(svv_exe):
            os.makedirs(svv_dir, exist_ok=True)
            print("  [audio] downloading SoundVolumeView (one-time, ~200 KB)...")
            zip_path = svv_exe + ".zip"
            urllib.request.urlretrieve(svv_url, zip_path)
            with zipfile.ZipFile(zip_path, "r") as zf:
                for name in zf.namelist():
                    if name.lower().endswith("soundvolumeview.exe"):
                        with zf.open(name) as src, open(svv_exe, "wb") as dst:
                            dst.write(src.read())
                        break
            os.remove(zip_path)

        if os.path.exists(svv_exe):
            r = subprocess.run(
                [svv_exe, "/SetDefault", device_id, "all"],
                capture_output=True,
                text=True,
                timeout=10,
            )
            if r.returncode == 0:
                return
            errors.append(f"SoundVolumeView rc={r.returncode}: {(r.stderr or r.stdout).strip()[:200]}")
    except Exception as exc:
        errors.append(f"SoundVolumeView tier: {exc}")

    raise RuntimeError("Fallback audio device switch failed.\n" + "\n".join(errors))
