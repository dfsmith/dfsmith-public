# Move windows to mouse (Windows 11)

Small Python utility that moves all top-level windows to the monitor containing
the mouse cursor. Includes an option to add a Desktop background context-menu
entry so you can right-click the desktop and run it.

Prerequisites
- Windows 10/11
- Python 3.8+
- Install dependencies with modern packaging: `python -m pip install -e .`
- If you use `CairoSVG` to build icon assets, install the native Cairo runtime
  DLL on Windows.

Windows Cairo runtime install
- CairoSVG is only required to build the icon assets, not to run the main
  utility.
- The generated icons and generator are included in the source distribution, so
  users can run the script without Cairo if the assets are already present.
- The preferred Windows install method is via winget:
  `winget install wingtk.gvsbuild.GTK4` This package includes GTK4, Cairo,
  PyGObject, Pycairo, and related dependencies.
- If winget is not available, use the GTK runtime installers:
  1. Download the GTK runtime for Windows from
     https://www.gtk.org/download/windows/.
  2. Install it to a user-local folder such as `%LOCALAPPDATA%\Programs\GTK`.
  3. Add the GTK `bin` folder to your user PATH, for example:
     `setx PATH "%LOCALAPPDATA%\Programs\GTK\bin;%PATH%"`
  4. Restart your terminal and verify with:
     `python -c "import ctypes.util; print(ctypes.util.find_library('cairo'))"`
- If `cairo-2.dll` is not found, `CairoSVG` will fail even when `cairocffi` is
  installed.

Usage
- Run once from a console:

      python move_windows_to_mouse.py

- Install Desktop background context-menu (per-user, no admin):

      python move_windows_to_mouse.py install

This creates an entry in HKCU so right-clicking the desktop and choosing "Move
windows to mouse" will launch the script. The registry command uses the current
`python` executable path; if you prefer no console window, change the command to
use `pythonw.exe`.

- Remove the context-menu entry:

      python move_windows_to_mouse.py uninstall

Build instructions
- Install the package in editable mode:

      python -m pip install -e .

- Generate icon assets from the SVG source:

      python build_icon.py

- Build the MSIX package for distribution:

      python build_msix.py

Notes
- The script enumerates visible, non-minimized top-level windows and repositions
  them to the same relative place on the target monitor.
- Some special windows (taskbar, shell windows, child windows) are skipped.
- If you want a single-click native app, consider packaging with `pyinstaller`
  and pointing the context-menu command at the bundled exe.

Files
- `move_windows_to_mouse.py`: main script
- `build_icon.py`: generates SVG, PNG, and ICO icon assets
- `build_msix.py`: builds an MSIX package for Windows deployment
- `build_msix_dfsmith.py`: local MSIX build helper for the author
- `pyproject.toml`: modern package metadata and dependencies
- `README.md`: this documentation
- `Assets/`: generated icon assets and source SVG files
