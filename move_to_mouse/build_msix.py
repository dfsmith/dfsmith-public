#!/usr/bin/env python3
"""Build an MSIX package for move_windows_to_mouse.

This script builds a single-file executable via PyInstaller and then wraps it
into an MSIX package using the Windows SDK `makeappx.exe` tool.

Example:
  python build_msix.py
  python build_msix.py --publisher "CN=MyCompany" --version 1.2.3.0
  python build_msix.py --skip-exe-build --exe dist\move_windows_to_mouse.exe

Requirements:
  pip install pyinstaller
  Windows SDK with makeappx.exe on PATH
"""

import argparse
import os
import shutil
import subprocess
import sys
import tempfile
import zlib
from pathlib import Path

APP_NAME = "MoveWindowsToMouse"
SCRIPT_NAME = "move_windows_to_mouse.py"
DEFAULT_EXE = Path("dist") / f"{APP_NAME}.exe"
DEFAULT_PACKAGE = Path(f"{APP_NAME}.msix")
DEFAULT_PUBLISHER = "CN=MoveWindowsToMouse"


def find_tool(name: str) -> Path | None:
    for dir_path in os.environ.get("PATH", "").split(os.pathsep):
        candidate = Path(dir_path) / name
        if candidate.exists():
            return candidate
    return None


def make_png(width: int, height: int, rgba: tuple[int, int, int, int], inset: int = 0) -> bytes:
    if inset < 0:
        inset = 0
    row = bytes(rgba) * width
    data = b"".join(b"\x00" + row for _ in range(height))
    compressed = zlib.compress(data)

    def chunk(chunk_type: bytes, chunk_data: bytes) -> bytes:
        return (
            len(chunk_data).to_bytes(4, "big")
            + chunk_type
            + chunk_data
            + zlib.crc32(chunk_type + chunk_data).to_bytes(4, "big")
        )

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", width.to_bytes(4, "big") + height.to_bytes(4, "big") + b"\x08\x06\x00\x00\x00")
    png += chunk(b"IDAT", compressed)
    png += chunk(b"IEND", b"")
    return png


def build_executable(args: argparse.Namespace) -> Path:
    exe_path = Path(args.exe or DEFAULT_EXE).resolve()
    if args.skip_exe_build:
        if not exe_path.exists():
            raise FileNotFoundError(f"Executable not found: {exe_path}")
        return exe_path

    try:
        import PyInstaller  # noqa: F401
    except ImportError:
        raise RuntimeError("PyInstaller is required: pip install pyinstaller")

    build_dir = Path("build")
    dist_dir = Path("dist")
    if build_dir.exists():
        shutil.rmtree(build_dir)
    if dist_dir.exists():
        shutil.rmtree(dist_dir)

    command = [
        sys.executable,
        "-m",
        "PyInstaller",
        "--noconfirm",
        "--onefile",
        "--name",
        APP_NAME,
    ]
    icon_path = Path("Assets") / "MoveWindowsToMouse.ico"
    if icon_path.exists():
        command.extend(["--icon", str(icon_path)])
    command.append(SCRIPT_NAME)
    if args.no_console:
        command.append("--windowed")

    print("Building executable with PyInstaller...")
    subprocess.run(command, check=True)

    if not exe_path.exists():
        raise FileNotFoundError(f"PyInstaller did not produce executable: {exe_path}")
    return exe_path


def render_manifest(args: argparse.Namespace, exe_name: str) -> str:
    return f"""<?xml version=\"1.0\" encoding=\"utf-8\"?>
<Package
  xmlns=\"http://schemas.microsoft.com/appx/manifest/foundation/windows10\"
  xmlns:uap=\"http://schemas.microsoft.com/appx/manifest/uap/windows10\"
  xmlns:rescap=\"http://schemas.microsoft.com/appx/manifest/foundation/windows10/restrictedcapabilities\"
  IgnorableNamespaces=\"uap mp rescap\">
  <Identity
    Name=\"{args.identity}\"
    Publisher=\"{args.publisher}\"
    Version=\"{args.version}\" />
  <Properties>
    <DisplayName>{args.display_name}</DisplayName>
    <PublisherDisplayName>{args.publisher_display_name}</PublisherDisplayName>
    <Description>{args.description}</Description>
    <Logo>Assets\Square44x44Logo.png</Logo>
  </Properties>
  <Dependencies>
    <TargetDeviceFamily Name=\"Windows.Desktop\" MinVersion=\"10.0.17763.0\" MaxVersionTested=\"10.0.99999.0\" />
  </Dependencies>
  <Resources>
    <Resource Language=\"en-us\" />
  </Resources>
  <Applications>
    <Application Id=\"App\" Executable=\"{exe_name}\" EntryPoint=\"Windows.FullTrustApplication\">
      <uap:VisualElements
        DisplayName=\"{args.display_name}\"
        Square44x44Logo=\"Assets\Square44x44Logo.png\"
        Square150x150Logo=\"Assets\Square150x150Logo.png\"
        Description=\"{args.description}\"
        BackgroundColor=\"transparent\">
        <uap:SplashScreen Image=\"Assets\Square44x44Logo.png\" />
      </uap:VisualElements>
    </Application>
  </Applications>
  <Capabilities>
    <rescap:Capability Name=\"runFullTrust\" />
  </Capabilities>
</Package>
"""


def package_msix(manifest_path: Path, staging_dir: Path, output_path: Path, makeappx_path: Path) -> None:
    command = [str(makeappx_path), "pack", "/d", str(staging_dir), "/p", str(output_path)]
    print("Packaging MSIX:", " ".join(command))
    subprocess.run(command, check=True)


def _build_signtool_command(target_path: Path, args: argparse.Namespace) -> list[str]:
    signtool = find_tool("signtool.exe")
    if signtool is None:
        raise FileNotFoundError("signtool.exe not found on PATH. Install the Windows SDK or specify /p to sign manually.")
    if not args.certificate:
        raise ValueError("A certificate path is required to sign the package or executable. Pass --certificate <pfx>.")
    command = [
        str(signtool),
        "sign",
        "/fd",
        "SHA256",
        "/a",
        "/f",
        str(args.certificate),
    ]
    if args.timestamp:
        command.extend(["/tr", args.timestamp, "/td", "SHA256"])
    command.append(str(target_path))
    return command


def sign_executable(exe_path: Path, args: argparse.Namespace) -> None:
    if args.skip_sign:
        print("Skipping executable signing.")
        return
    if args.skip_exe_sign:
        print("Skipping executable signing.")
        return
    command = _build_signtool_command(exe_path, args)
    print("Signing executable:", " ".join(command))
    subprocess.run(command, check=True)


def sign_msix(package_path: Path, args: argparse.Namespace) -> None:
    if args.skip_sign:
        print("Skipping MSIX signing.")
        return
    command = _build_signtool_command(package_path, args)
    print("Signing MSIX:", " ".join(command))
    subprocess.run(command, check=True)


def create_asset_files(staging_dir: Path) -> None:
    assets_dir = staging_dir / "Assets"
    assets_dir.mkdir(parents=True, exist_ok=True)
    source_assets = Path("Assets")
    fallback_assets = [
        ("Square44x44Logo.png", 44, 44, (0, 120, 215, 255), 0),
        ("Square150x150Logo.png", 150, 150, (0, 120, 215, 255), 20),
    ]

    for name, width, height, color, inset in fallback_assets:
        dest = assets_dir / name
        source = source_assets / name
        if source.exists():
            shutil.copy2(source, dest)
        else:
            dest.write_bytes(make_png(width, height, color, inset))

    ico_source = source_assets / "MoveWindowsToMouse.ico"
    if ico_source.exists():
        shutil.copy2(ico_source, assets_dir / ico_source.name)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Build an MSIX package for move_windows_to_mouse.")
    parser.add_argument("--exe", help="Path to the executable to package. Defaults to dist/MoveWindowsToMouse.exe.")
    parser.add_argument("--output", default=DEFAULT_PACKAGE, help="Output MSIX path.")
    parser.add_argument("--identity", default="MoveWindowsToMouse.App", help="App identity name.")
    parser.add_argument("--publisher", default=DEFAULT_PUBLISHER, help="Package publisher string.")
    parser.add_argument("--display-name", default="Move Windows to Mouse", help="App display name.")
    parser.add_argument("--publisher-display-name", default="Move Windows to Mouse", help="Publisher display name.")
    parser.add_argument("--version", default="1.0.0.0", help="Package version.")
    parser.add_argument("--description", default="Move windows to the monitor containing the mouse cursor.", help="Package description.")
    parser.add_argument("--skip-exe-build", action="store_true", help="Skip building the executable with PyInstaller.")
    parser.add_argument("--no-console", action="store_true", help="Build the executable without a console window.")
    parser.add_argument("--skip-sign", action="store_true", help="Skip signing the executable and MSIX.")
    parser.add_argument("--skip-exe-sign", action="store_true", help="Skip signing the executable before packaging.")
    parser.add_argument("--certificate", type=Path, help="Code signing PFX certificate to sign the executable and MSIX.")
    parser.add_argument("--timestamp", default="http://timestamp.digicert.com", help="Timestamp server for signing.")
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    makeappx = find_tool("makeappx.exe")
    if makeappx is None:
        raise FileNotFoundError("makeappx.exe not found on PATH. Install the Windows 10 SDK and add it to PATH.")

    exe_path = build_executable(args)
    output_path = Path(args.output).resolve()

    if not args.skip_sign:
        sign_executable(exe_path, args)

    with tempfile.TemporaryDirectory() as staging_name:
        staging_dir = Path(staging_name)
        shutil.copy2(exe_path, staging_dir / exe_path.name)
        create_asset_files(staging_dir)

        manifest_path = staging_dir / "AppxManifest.xml"
        manifest_path.write_text(render_manifest(args, exe_path.name), encoding="utf-8")

        package_msix(manifest_path, staging_dir, output_path, makeappx)

        if not args.skip_sign:
            sign_msix(output_path, args)

    print(f"MSIX package created: {output_path}")


if __name__ == "__main__":
    main()
