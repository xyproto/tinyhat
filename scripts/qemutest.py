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


def desktop_up(ppm):
    try:
        data = open(ppm, "rb").read()
        parts = data.split(b"\n", 3)
        w = int(parts[1].split()[0])
        o = (12 * w + 40) * 3
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
    shot = os.path.join(QMT, "qemutest.ppm")
    subprocess.run(["cp", "--sparse=always", "-f", img, disk], check=True)
    os.truncate(disk, 4 << 30)
    for f in (sock, shot):
        if os.path.exists(f):
            os.remove(f)
    qemu = "qemu-system-x86_64" if "64" in os.path.basename(img) else "qemu-system-i386"
    proc = subprocess.Popen([qemu, "-enable-kvm", "-cpu", "host", "-smp", "2", "-m", "1024",
                             "-drive", f"if=none,id=stick,file={disk},format=raw",
                             "-device", "qemu-xhci", "-device", "usb-storage,drive=stick,bootindex=0",
                             "-device", "usb-tablet", "-vga", "std",
                             "-audiodev", "none,id=snd", "-device", "intel-hda", "-device", "hda-duplex,audiodev=snd",
                             "-nic", "user,model=e1000", "-display", "none",
                             "-monitor", f"unix:{sock},server,nowait"])
    ok = False
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
    secs = int(time.time() - start)
    if ok:
        print(f"qemutest: {os.path.basename(img)} reached the desktop in {secs} s (screenshot in build/qmt)")
    else:
        print(f"qemutest: {os.path.basename(img)} did not reach the desktop within {limit} s (last screen in build/qmt)")
    sys.exit(0 if ok else 1)


main()
