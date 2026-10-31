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
        ">": "shift-dot", "<": "shift-comma", "(": "shift-9", ")": "shift-0", "&": "shift-7", ":": "shift-semicolon",
        "?": "shift-slash", "+": "shift-equal", '"': "shift-apostrophe", "$": "shift-4"}
REPORT = ("doas sh -c '(echo TH-FAILED; systemctl --failed --no-legend --plain; echo TH-MEMORY; free -m; "
          "echo TH-PROCESSES; ps -eo rss=,comm= --sort=-rss | head -15; echo TH-END) >/dev/{tty} 2>&1'\n")
SEARCH = "https://html.duckduckgo.com/html/?q=tiny+hat+linux"
LOBSTERS = "https://lobste.rs"
NETSURFY = ("sh -c 'netsurfy -v \"" + SEARCH + "\" 2>/tmp/th-ns1.log & sleep 30; kill $!; "
            "netsurfy -v " + LOBSTERS + " 2>/tmp/th-ns2.log & sleep 30; kill $!; "
            "doas sh -c \"(echo TH-NETSURFY; grep -c ip3/ /tmp/th-ns1.log; grep -c done.https://lobste.rs /tmp/th-ns2.log; "
            "echo TH-NETSURFY-END) >/dev/ttyS0\"' &\n")
OVMF_X64 = "/usr/share/edk2/x64/OVMF_CODE.4m.fd"
OVMF_X64_VARS = "/usr/share/edk2/x64/OVMF_VARS.4m.fd"
AAVMF = "/usr/share/edk2/aarch64/QEMU_EFI.fd"


def kind(img):
    name = os.path.basename(img)
    if "rpi4" in name:
        return "rpi4"
    if "aarch64" in name:
        return "aarch64"
    return "x32" if "64" in name else "i686"


def pi_boot_files(disk):
    """The kernel, device tree, initramfs and command line from the FAT boot partition of a Raspberry Pi image."""
    out = {}
    for name in ("kernel8.img", "bcm2711-rpi-4-b.dtb", "initramfs.img", "cmdline.txt"):
        dest = disk[:-4] + "-" + name
        subprocess.run(["mcopy", "-o", "-n", "-i", f"{disk}@@1M", f"::{name}", dest], check=True)
        out[name] = dest
    return out


def qemu_command(img, disk, uefi, serial, sock):
    k = kind(img)
    common = ["-display", "none", "-serial", f"file:{serial}", "-monitor", f"unix:{sock},server,nowait"]
    if k == "rpi4":
        f = pi_boot_files(disk)
        cmdline = open(f["cmdline.txt"]).read().strip().replace("console=serial0,115200", "console=ttyAMA0,115200")
        return ["qemu-system-aarch64", "-M", "raspi4b", "-kernel", f["kernel8.img"], "-dtb", f["bcm2711-rpi-4-b.dtb"],
                "-initrd", f["initramfs.img"], "-append", cmdline,
                "-drive", f"if=sd,file={disk},format=raw"] + common
    if k == "aarch64":
        return ["qemu-system-aarch64", "-M", "virt", "-cpu", "max", "-smp", "2", "-m", "1024", "-bios", AAVMF,
                "-drive", f"if=none,id=stick,file={disk},format=raw",
                "-device", "qemu-xhci", "-device", "usb-storage,drive=stick,bootindex=0",
                "-device", "usb-kbd", "-device", "usb-tablet", "-device", "virtio-gpu-pci",
                "-nic", "user,model=virtio-net-pci"] + common
    qemu = "qemu-system-x86_64" if k == "x32" else "qemu-system-i386"
    firmware = []
    if uefi:
        vars_copy = disk[:-4] + ".vars"
        shutil.copyfile(OVMF_X64_VARS, vars_copy)
        firmware = ["-machine", "q35", "-drive", f"if=pflash,format=raw,readonly=on,file={OVMF_X64}",
                    "-drive", f"if=pflash,format=raw,file={vars_copy}"]
    return [qemu, "-enable-kvm", "-cpu", "host", "-smp", "2", "-m", "1024"] + firmware + [
        "-drive", f"if=none,id=stick,file={disk},format=raw",
        "-device", "qemu-xhci", "-device", "usb-storage,drive=stick,bootindex=0",
        "-device", "usb-tablet", "-vga", "std",
        "-audiodev", "none,id=snd", "-device", "intel-hda", "-device", "hda-duplex,audiodev=snd",
        "-nic", "user,model=e1000"] + common


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


def netsurfy_report(serial):
    text = open(serial, errors="replace").read().replace("\r", "") if os.path.exists(serial) else ""
    m = text.rfind("TH-NETSURFY\n")
    if m < 0 or "TH-NETSURFY-END" not in text[m:]:
        return None
    lines = text[m:text.index("TH-NETSURFY-END", m)].split()[1:]
    return [int(n) if n.isdigit() else 0 for n in lines] + [0, 0]


def link_pixels(ppm):
    """How many pixels have the blue of unvisited links, which a page that failed to render lacks."""
    try:
        data = open(ppm, "rb").read()
        pixels = data.split(b"\n", 3)[3]
    except (OSError, IndexError):
        return 0
    return sum(1 for o in range(0, len(pixels) - 2, 3) if pixels[o + 2] > 120 and pixels[o + 2] - max(pixels[o], pixels[o + 1]) > 70)


def desktop_up(ppm):
    try:
        data = open(ppm, "rb").read()
        parts = data.split(b"\n", 3)
        w = int(parts[1].split()[0])
        o = (12 * w + 20) * 3
        r, g, b = parts[3][o], parts[3][o + 1], parts[3][o + 2]
    except (OSError, IndexError, ValueError):
        return False
    return r > 120 and g < 80 and b < 80


def main():
    args = [a for a in sys.argv[1:] if a != "--uefi"]
    uefi = "--uefi" in sys.argv[1:]
    img = args[0]
    k = kind(img)
    limit = int(args[1]) if len(args) > 1 else (1200 if k in ("aarch64", "rpi4") else 240)
    if uefi and k == "i686":
        sys.exit("qemutest: no 32-bit (ia32) UEFI firmware for QEMU on this host, so only BIOS boot can be tested")
    boot = "direct kernel" if k == "rpi4" else "UEFI" if uefi or k == "aarch64" else "BIOS"
    label = f"{os.path.basename(img)} ({boot})"
    tag = f"{os.path.basename(img)[:-4]}-{boot.split()[0].lower()}"
    os.makedirs(QMT, exist_ok=True)
    disk = os.path.join(QMT, f"{tag}.img")
    sock = os.path.join(QMT, f"{tag}.mon")
    serial = os.path.join(QMT, f"{tag}.serial")
    shot = os.path.join(QMT, f"{tag}.ppm")
    web = [os.path.join(QMT, f"{tag}-{name}.ppm") for name in ("search", "lobsters")]
    subprocess.run(["cp", "--sparse=always", "-f", img, disk], check=True)
    os.truncate(disk, 4 << 30)
    for f in [sock, shot, serial] + web:
        if os.path.exists(f):
            os.remove(f)
    proc = subprocess.Popen(qemu_command(img, disk, uefi, serial, sock))
    ok = False
    report = None
    web_report = None
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
                if k == "rpi4":
                    break
                monitor(sock, "sendkey ctrl-alt-r")
                time.sleep(3)
                type_text(sock, REPORT.format(tty="ttyS0" if k in ("i686", "x32") else "ttyAMA0"))
                for _ in range(20):
                    time.sleep(1)
                    report = health(serial)
                    if report:
                        break
                if k in ("i686", "x32"):
                    type_text(sock, NETSURFY)
                    time.sleep(25)
                    monitor(sock, f"screendump {web[0]}")
                    time.sleep(30)
                    monitor(sock, f"screendump {web[1]}")
                    for _ in range(20):
                        time.sleep(1)
                        web_report = netsurfy_report(serial)
                        if web_report:
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
    for f in [shot] + web:
        if os.path.exists(f) and shutil.which("magick"):
            subprocess.run(["magick", f, f[:-4] + ".png"])
    if ok:
        print(f"qemutest: {label} reached the desktop in {up} s (screenshot and serial log in build/qmt)")
        if k == "rpi4":
            print("qemutest: the emulated Raspberry Pi 4 has no USB keyboard, so there is no health report")
        elif report is None:
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
        if k in ("i686", "x32"):
            if web_report is None:
                print("qemutest: no NetSurfy report on the serial port")
                ok = False
            else:
                results, lobsters = web_report[0], web_report[1]
                blue = [link_pixels(f) for f in web]
                print(f"qemutest: NetSurfy found {results} DuckDuckGo results for \"tiny hat linux\" ({blue[0]} link pixels on screen)")
                print(f"qemutest: NetSurfy {'loaded' if lobsters else 'did not load'} {LOBSTERS} ({blue[1]} link pixels on screen)")
                if results < 3 or not lobsters or min(blue) < 300:
                    print("qemutest: NetSurfy failed (screenshots in build/qmt)")
                    ok = False
    else:
        print(f"qemutest: {label} did not reach the desktop within {limit} s (last screen in build/qmt)")
    sys.exit(0 if ok else 1)


main()
