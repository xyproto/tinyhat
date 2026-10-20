#!/usr/bin/env python3
import os, shutil, socket, subprocess, sys, time

TOP = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
QMT = os.path.join(TOP, "build", "qmt")


def monitor(sock, cmd):
    s = socket.socket(socket.AF_UNIX)
    s.connect(sock)
    s.send(cmd.encode() + b"\n")
    time.sleep(1)
    s.close()


KEYS = {" ": "spc", "\n": "ret", "-": "minus", "=": "equal", "/": "slash", ".": "dot", ",": "comma",
        ";": "semicolon", "'": "apostrophe", "\\": "backslash", "_": "shift-minus", "|": "shift-backslash",
        ">": "shift-dot", "<": "shift-comma", "(": "shift-9", ")": "shift-0", "&": "shift-7", ":": "shift-semicolon"}
REPORT = ("doas sh -c '(echo TH-FAILED; systemctl --failed --no-legend --plain; echo TH-MEMORY; free -m; "
          "echo TH-PROCESSES; ps -eo rss=,comm= --sort=-rss | head -15; echo TH-END) >/dev/ttyS0 2>&1'\n")


def type_text(sock, text):
    s = socket.socket(socket.AF_UNIX)
    s.connect(sock)
    for ch in text:
        key = KEYS.get(ch) or ("shift-" + ch.lower() if ch.isupper() else ch)
        s.send(f"sendkey {key}\n".encode())
        time.sleep(0.04)
    time.sleep(0.5)
    s.close()


def health(serial):
    text = open(serial, errors="replace").read() if os.path.exists(serial) else ""
    m = text.rfind("TH-FAILED")
    if m < 0 or "TH-END" not in text[m:]:
        return None
    block = text[m:text.index("TH-END", m)].replace("\r", "")
    sections = {}
    for name in ("TH-FAILED", "TH-MEMORY", "TH-PROCESSES"):
        start = block.index(name) + len(name)
        ends = [block.index(n) for n in ("TH-MEMORY", "TH-PROCESSES") if n in block and block.index(n) > start]
        sections[name] = [l for l in block[start:min(ends) if ends else len(block)].splitlines() if l.strip()]
    return sections


def desktop_up(ppm):
    try:
        data = open(ppm, "rb").read()
        parts = data.split(b"\n", 3)
        w = int(parts[1].split()[0])
        o = (12 * w + 3) * 3
        r, g, b = parts[3][o], parts[3][o + 1], parts[3][o + 2]
    except (OSError, IndexError, ValueError):
        return False
    return g > 120 and b > 110 and r < 80


def main():
    img = sys.argv[1]
    limit = int(sys.argv[2]) if len(sys.argv) > 2 else 240
    os.makedirs(QMT, exist_ok=True)
    disk = os.path.join(QMT, "qemutest.img")
    sock = os.path.join(QMT, "qemutest.mon")
    serial = os.path.join(QMT, "qemutest.serial")
    shot = os.path.join(QMT, "qemutest.ppm")
    subprocess.run(["cp", "--sparse=always", "-f", img, disk], check=True)
    os.truncate(disk, 4 << 30)
    for f in (sock, shot, serial):
        if os.path.exists(f):
            os.remove(f)
    qemu = "qemu-system-x86_64" if "64" in os.path.basename(img) else "qemu-system-i386"
    proc = subprocess.Popen([qemu, "-enable-kvm", "-cpu", "host", "-smp", "2", "-m", "1024",
                             "-drive", f"if=none,id=stick,file={disk},format=raw",
                             "-device", "qemu-xhci", "-device", "usb-storage,drive=stick,bootindex=0",
                             "-device", "usb-tablet", "-vga", "std",
                             "-audiodev", "none,id=snd", "-device", "intel-hda", "-device", "hda-duplex,audiodev=snd",
                             "-nic", "user,model=e1000", "-display", "none",
                             "-serial", f"file:{serial}", "-monitor", f"unix:{sock},server,nowait"])
    ok = False
    report = None
    start = time.time()
    try:
        while time.time() - start < limit and proc.poll() is None:
            time.sleep(3)
            if not os.path.exists(sock):
                continue
            monitor(sock, f"screendump {shot}")
            if desktop_up(shot):
                time.sleep(5)
                monitor(sock, f"screendump {shot}")
                ok = True
                up = int(time.time() - start)
                monitor(sock, "sendkey ctrl-alt-r")
                time.sleep(3)
                type_text(sock, REPORT)
                for _ in range(20):
                    time.sleep(1)
                    report = health(serial)
                    if report:
                        break
                break
        if proc.poll() is None:
            monitor(sock, "quit")
    finally:
        try:
            proc.wait(timeout=20)
        except subprocess.TimeoutExpired:
            proc.terminate()
            proc.wait()
    if os.path.exists(shot) and shutil.which("magick"):
        subprocess.run(["magick", shot, shot[:-4] + ".png"])
    if ok:
        print(f"qemutest: {os.path.basename(img)} reached the desktop in {up} s (screenshot and serial log in build/qmt)")
        if report is None:
            print("qemutest: no health report on the serial port")
        else:
            mem = next((l.split() for l in report["TH-MEMORY"] if l.startswith("Mem:")), None)
            if mem:
                print(f"qemutest: memory used {mem[2]} MiB of {mem[1]} MiB")
            print("qemutest: largest processes (RSS KiB):")
            for line in report["TH-PROCESSES"][:12]:
                print("  " + line.strip())
            if report["TH-FAILED"]:
                print("qemutest: failed units:")
                for line in report["TH-FAILED"]:
                    print("  " + line.strip())
                ok = False
            else:
                print("qemutest: no failed units")
    else:
        print(f"qemutest: {os.path.basename(img)} did not reach the desktop within {limit} s (last screen in build/qmt)")
    sys.exit(0 if ok else 1)


main()
