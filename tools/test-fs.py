#!/usr/bin/env python3
"""Filesystem tests; run inside the original VMware Ubuntu guest."""
import argparse
import pathlib
import subprocess
import time
import struct

ROOT = pathlib.Path(__file__).resolve().parents[1]
CACHE = pathlib.Path.home() / ".cache/ecnu-oslab/lab789"

def run(test, marker, harts=2, buffers=8, timeout=30, image=None, input_bytes=b""):
    options = [f"TEST={test}", f"CPUNUM={harts}", f"NBUFFER={buffers}"]
    subprocess.run(["make", "-s", "-j2", "build", *options], cwd=ROOT, check=True)
    target = ROOT / subprocess.check_output(["make", "-s", "print-target", *options], cwd=ROOT, text=True).strip()
    CACHE.mkdir(parents=True, exist_ok=True)
    image = image or CACHE / f"{test}-{harts}-{time.time_ns()}.img"
    subprocess.run(["make", "-s", "disk", f"DISKIMG={image}", *options], cwd=ROOT, check=True)
    command = ["qemu-system-riscv64", "-machine", "virt", "-bios", "default", "-kernel",
               str(target / "kernel/kernel-qemu.elf"), "-m", "130M", "-smp", str(harts),
               "-nographic", "-drive", f"file={image},if=none,format=raw,id=fs0",
               "-device", "virtio-blk-device,drive=fs0,bus=virtio-mmio-bus.0"]
    p = subprocess.Popen(command, cwd=ROOT, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    try:
        output, _ = p.communicate(input=input_bytes, timeout=timeout)
    except subprocess.TimeoutExpired:
        p.terminate()
        output, _ = p.communicate(timeout=5)
    text = output.decode(errors="replace")
    (target / "test.log").write_text(text)
    assert marker in text and "panic:" not in text and "_FAIL" not in text, text
    if test == "lab7":
        with image.open("rb") as f:
            f.seek(5000 * 4096)
            assert f.read(4096) == bytes((i * 17 + 3) % 256 for i in range(4096))
    print(f"PASS {test}, harts={harts}, buffers={buffers}, image={image}", flush=True)
    return text, image

def bitmap_test():
        options = ["TEST=lab7_bitmap", "NBUFFER=8"]
        subprocess.run(["make", "-s", "-j2", "build", *options], cwd=ROOT, check=True)
        target = ROOT / subprocess.check_output(["make", "-s", "print-target", *options], cwd=ROOT, text=True).strip()
        subprocess.run(["make", "-s", str(target.relative_to(ROOT) / "mkfs"), *options], cwd=ROOT, check=True)
        image = CACHE / f"bitmap-{time.time_ns()}.img"
        subprocess.run([str(target / "mkfs"), str(image), "65537", "65537"], check=True)
        with image.open("r+b") as f:
            sb = struct.unpack("<12I", f.read(48))
            for start in (sb[4], sb[8]):
                for block in range(3):
                    data = bytearray(b"\xff" * 4096)
                    for bit in (32767, 32768, 65536):
                        if bit // 32768 == block:
                            data[bit % 32768 // 8] &= ~(1 << (bit % 8))
                    f.seek((start + block) * 4096)
                    f.write(data)
        run("lab7_bitmap", "LAB7_BITMAP_PASS", image=image, timeout=8)

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("lab", choices=["lab7", "bitmap"])
    args = parser.parse_args()
    if args.lab == "lab7":
        for harts in (1, 2):
            run(args.lab, args.lab.upper()+"_PASS", harts, timeout=8)
        run("lab7", "LAB7_PASS", buffers=16384, timeout=8)
    bitmap_test()
