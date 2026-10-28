import subprocess
import sys

ISO = __import__("os").environ.get("DRAKAN_ISO", "")
Z = r"C:\Program Files\7-Zip\7z.exe"
files = sys.argv[1:]
for f in files:
    B = chr(92)
    w = "ENGLISH" + B + f.replace("/", B)
    p = subprocess.run([Z, "e", "-so", ISO, w], capture_output=True)
    d = p.stdout[:48]
    print(f, len(p.stdout), d[:4], d.hex())
