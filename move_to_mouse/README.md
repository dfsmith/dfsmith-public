# Move windows to mouse (Windows 11)

Small Python utility that moves all top-level windows to the monitor containing the mouse cursor. Includes an option to add a Desktop background context-menu entry so you can right-click the desktop and run it.

Prerequisites
- Windows 10/11
- Python 3.8+
- Install dependency: `pip install -r requirements.txt`

Usage
- Run once from a console:

  python move_windows_to_mouse.py

- Install Desktop background context-menu (per-user, no admin):

  python move_windows_to_mouse.py install

This creates an entry in HKCU so right-clicking the desktop and choosing "Move windows to mouse" will launch the script. The registry command uses the current `python` executable path; if you prefer no console window, change the command to use `pythonw.exe`.

- Remove the context-menu entry:

  python move_windows_to_mouse.py uninstall

Notes
- The script enumerates visible, non-minimized top-level windows and repositions them to the same relative place on the target monitor.
- Some special windows (taskbar, shell windows, child windows) are skipped.
- If you want a single-click native app, consider packaging with `pyinstaller` and pointing the context-menu command at the bundled exe.

Files
- `move_windows_to_mouse.py`: main script
- `requirements.txt`: dependencies
