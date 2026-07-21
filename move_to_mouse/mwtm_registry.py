import sys

try:
    import winreg
except Exception:
    print("Missing dependency: pywin32. Install with: pip install pywin32")
    raise


class ContextMenuRegistry:
    key_path = r"Software\Classes\Directory\Background\shell\MoveWindowsToMouse"

    @staticmethod
    def install(script_path: str) -> None:
        key_path = ContextMenuRegistry.key_path
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

    @staticmethod
    def _delete_tree(root, subkey) -> None:
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
                ContextMenuRegistry._delete_tree(root, subkey + "\\" + s)
            winreg.DeleteKey(root, subkey)
        except FileNotFoundError:
            pass

    @staticmethod
    def uninstall() -> None:
        key_path = ContextMenuRegistry.key_path
        try:
            ContextMenuRegistry._delete_tree(winreg.HKEY_CURRENT_USER, key_path)
            print("Context menu item removed (HKCU).")
        except Exception as e:
            print("Failed to remove context menu:", e)
