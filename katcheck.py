#!/usr/bin/env python3
"""
Runs the known answer tests of decs through a build of decscli.

Each kat/<sha256>_<bits>.bin must decompress to ASCII text with that SHA-256, being JSON
with an object at top level, except the file of the empty text; its first
2*floor((bits+14)/15) bytes must decompress to the same text, and 2 bytes fewer must be
refused with diagnostic 29301, or 29300 when that leaves no input at all.

Usage: py katcheck.py decscli [katdir]   (default kat next to this script)
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
import hashlib
import json
import os
import subprocess
import sys

kEmpty = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"


def run(inCli, inPath, inBytes=None):
    """Runs the build on a file; returns (stdout bytes, stderr text)."""
    vP = subprocess.run([inCli, inPath] + ([str(inBytes)] if inBytes is not None else []), capture_output=True)
    return vP.stdout, vP.stderr.decode().strip()


def check(inCli, inPath):
    """Checks one file; returns a failure message or None."""
    vSha, vBits = os.path.basename(inPath)[:-4].split("_")
    vOut, vErr = run(inCli, inPath)
    if vErr:
        return "diagnostic " + vErr
    if hashlib.sha256(vOut).hexdigest() != vSha:
        return "SHA-256 differs"
    if vSha != kEmpty and not isinstance(json.loads(vOut.decode("ascii")), dict):
        return "not a JSON object"
    vCut = 2 * ((int(vBits) + 14) // 15)   # bytes holding the payload
    if run(inCli, inPath, vCut) != (vOut, ""):
        return "truncated to %d bytes: text differs" % vCut
    vErr = "29300" if vCut == 2 else "29301"   # no input at all is a size error
    if run(inCli, inPath, vCut - 2)[1] != vErr:
        return "truncated to %d bytes: %s expected" % (vCut - 2, vErr)
    return None


def main(inArgs):
    """Checks every file; prints the failures and a summary."""
    if not inArgs:
        sys.exit(__doc__)
    vDir   = inArgs[1] if len(inArgs) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), "kat")
    vPaths = sorted(glob.glob(os.path.join(vDir, "*.bin")))
    vBad   = 0
    for vPath in vPaths:
        vFail = check(inArgs[0], vPath)
        if vFail:
            vBad += 1
            print("FAIL %s: %s" % (os.path.basename(vPath), vFail))
    print("%d files, %d failure(s)" % (len(vPaths), vBad))
    return 1 if vBad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
