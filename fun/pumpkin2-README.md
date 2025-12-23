Pumpkin2.py (GooCanvas 2)
======================

A program that draws and animates "pumpkins" so that a projector can put faces on real pumpkins.

Updated using github AI from the original pumpkin.py.  Uses GooCanvas 2 and GTK3 via PyGObject.

Requirements
------------
- Python 3
- PyGObject (python3-gi)
- GooCanvas 2 GIR (gir1.2-goocanvas-2.0)
- Pycairo / python3-cairo

Install (Debian / Ubuntu)
-------------------------
sudo apt update
sudo apt install -y python3-gi python3-gi-cairo gir1.2-goocanvas-2.0 python3-cairo

Run
---
python3 pumpkin2.py

Enable debug logging
--------------------
Set the environment variable PUMPKIN_DEBUG=1 before running to see debug logs from the program:

PUMPKIN_DEBUG=1 python3 pumpkin2.py

Notes
-----
- This script uses GooCanvas 2 (via GI) and GTK 3. It no longer supports legacy python-goocanvas or pygtk fallbacks.
- If you get an error about the `GooCanvas` namespace not being available, install the GIR package for your distro (see the install section above).
