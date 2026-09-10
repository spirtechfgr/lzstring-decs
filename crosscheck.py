#!/usr/bin/env python3
"""
Cross-checks the known answer tests of decs: for every kat/*.bin, decompresses with
decscli and with the lzstring package of pip (a port of the JavaScript original) and
compares; then parses the text with Python's json module, requires an object at top
level, and requires that re-dumping it minimized, DEL unescaped, gives the text back.

Usage: py crosscheck.py decscli [katdir]   (default kat next to this script)
Needs: py -m pip install lzstring
Exit status 0 iff every check passes.
"""
#
#   This file is released under CC0 1.0 Universal; the full text is in LICENSE.
#
#   To the extent possible under law, the author has dedicated all copyright and related
#   and neighboring rights to this file to the public domain worldwide.
#
#   You may copy, modify, distribute and perform the work, even for commercial purposes, all
#   without asking permission.

import glob
import json
import os
import subprocess
import sys

import lzstring


def main(inArgs):
    """Checks every file; prints the failures and a summary."""
    if not inArgs:
        sys.exit(__doc__)
    vDir   = inArgs[1] if len(inArgs) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), "kat")
    vPaths = sorted(glob.glob(os.path.join(vDir, "*.bin")))
    vLz    = lzstring.LZString()
    vBad   = 0
    for vPath in vPaths:
        vName  = os.path.basename(vPath)
        vText  = subprocess.run([inArgs[0], vPath], capture_output=True, check=True).stdout.decode("ascii")
        vUnits = [ord(vC) for vC in open(vPath, "rb").read().decode("utf-16-le")]   # the port wants integers
        vRef   = vLz.decompressFromUTF16(vUnits) or ""                              # None for the empty text
        if vRef != vText:
            vBad += 1
            print("FAIL %s: lzstring gives %d characters, decscli %d" % (vName, len(vRef), len(vText)))
        if vText == "":
            continue
        try:
            vObj = json.loads(vText)
        except ValueError as vE:
            vBad += 1
            print("FAIL %s: not JSON: %s" % (vName, vE))
            continue
        if not isinstance(vObj, dict):
            vBad += 1
            print("FAIL %s: not an object" % vName)
        if json.dumps(vObj, ensure_ascii=True, separators=(",", ":")).replace("\\u007f", "\x7f") != vText:
            vBad += 1
            print("FAIL %s: not minimized, or escapes differ" % vName)
    print("%d files, %d failure(s)" % (len(vPaths), vBad))
    return 1 if vBad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
