#!/usr/bin/env python3
"""Move windows to the monitor containing the mouse pointer.

Example:
    python move_windows_to_mouse.py              # see move_all (default)
    python move_windows_to_mouse.py move_all     # move windows and sound to display
    python move_windows_to_mouse.py move_windows # only move windows to the display
    python move_windows_to_mouse.py move_sound   # only move default audio output to the display
    python move_windows_to_mouse.py install      # add Windows context menu entry
    python move_windows_to_mouse.py uninstall    # remove context menu entry

Install dependencies (includes build tools):
    python -m pip install .
"""

import argparse

from mwtm_display import Display
from mwtm_audio import move_default_audio_to_display
from mwtm_window import move_windows_to_display
from mwtm_registry import ContextMenuRegistry


def main():
    parser = argparse.ArgumentParser(
        description="Move windows to the display containing the mouse pointer.",
        epilog=(
            "Commands:\n"
            "  move_all     Move windows, set primary display, and move audio.\n"
            "  move_windows Move windows and set primary display.\n"
            "  move_sound   Move default audio output.\n"
            "  install      Add the script to the desktop background context menu.\n"
            "  uninstall    Remove the context menu entry.\n"
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument(
        "cmd",
        nargs="?",
        choices=["install", "uninstall", "move_windows", "move_sound", "move_all"],
        default="move_all",
        help="Command to run (default: move_all).",
    )
    parser.add_argument(
        "--debug-audio", action="store_true", help="Print audio endpoint scoring."
    )
    parser.add_argument(
        "--verbose-windows",
        action="store_true",
        help="Print detailed window move output.",
    )
    args = parser.parse_args()

    # Handle context menu subcommands.
    do_move_windows = args.cmd in ("move_windows", "move_all")
    do_move_sound = args.cmd in ("move_sound", "move_all")
    do_setup = args.cmd in ("install", "uninstall")

    if do_setup:
        if args.cmd == "install":
            import os, sys

            ContextMenuRegistry.install(os.path.abspath(sys.argv[0]))
        elif args.cmd == "uninstall":
            ContextMenuRegistry.uninstall()
        return

    # Move windows to the target display under the pointer.
    pt = Display.pointer_position()
    target_coords = Display.rect_from_point(pt)
    target_device_name = Display.device_name(target_coords)
    target_name = Display.name_from_device(target_device_name)

    print(f"Selected display: {target_name}")

    # For move_all, run audio before set_primary so target monitor geometry
    # is still valid for display-to-audio endpoint matching.
    if do_move_sound:
        audio_target_name, move_sound_error = move_default_audio_to_display(
            target_device_name, debug=args.debug_audio
        )
        if audio_target_name:
            print(f"Default audio moved to {audio_target_name}.")
        else:
            print(f"Failed to move default audio for {target_name}: {move_sound_error}")

    if do_move_windows:
        count = move_windows_to_display(target_coords, verbose=args.verbose_windows)
        print(f"Moved {count} windows to display at {target_coords}.")
        primary_display_name, set_primary_error = Display.set_primary(target_coords)
        if primary_display_name:
            print(f"Primary display set to {primary_display_name}.")
        else:
            print(
                f"Failed to set primary display to {target_name}: "
                f"{set_primary_error}"
            )


if __name__ == "__main__":
    main()
