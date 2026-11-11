import os
import re
import shutil
import sys

src, out = sys.argv[1], sys.argv[2]
mel, drum, section, name = {}, {}, None, None
for line in open(os.path.join(src, "crude.cfg"), encoding="latin-1"):
    s = line.strip()
    if s.startswith("bank 0"):
        section = mel
    elif s.startswith("drumset 0"):
        section = drum
    m = re.match(r"# Program .*\(gus ([A-Za-z0-9_]+)", s)
    if m:
        name = m.group(1).lower()[:8]
        continue
    m = re.match(r"(\d+)\s+(\S+\.pat)", s)
    if m and section is not None and name:
        path = os.path.join(src, m.group(2))
        if os.path.isfile(path):
            section[int(m.group(1))] = (name, path)
    if not s.startswith("#"):
        name = None
os.makedirs(os.path.join(out, "MIDI"), exist_ok=True)
lines = ["[Melodic Patches]"]
for prog in sorted(mel):
    n, p = mel[prog]
    shutil.copyfile(p, os.path.join(out, "MIDI", n.upper() + ".PAT"))
    lines.append(f"{prog}={n}")
lines += ["", "[Drum Patches]"]
for note in sorted(drum):
    n, p = drum[note]
    shutil.copyfile(p, os.path.join(out, "MIDI", n.upper() + ".PAT"))
    lines.append(f"{note}={n}")
with open(os.path.join(out, "ULTRASND.INI"), "w", newline="\r\n") as f:
    f.write("\n".join(lines) + "\n")
print(f"{len(mel)} melodic, {len(drum)} drum patches")
