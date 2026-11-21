#!/usr/bin/env python3
"""Filesystem tests; run inside the original VMware Ubuntu guest."""
import argparse
import pathlib
import subprocess
import time
import struct
import os

ROOT = pathlib.Path(__file__).resolve().parents[1]
CACHE = pathlib.Path(os.environ.get("OSLAB_FS_CACHE", str(pathlib.Path.home() / ".cache/ecnu-oslab/lab789")))

def run(test, marker, harts=2, buffers=8, timeout=30, image=None, input_bytes=b"", fs_test=0):
    options = [f"TEST={test}", f"CPUNUM={harts}", f"NBUFFER={buffers}", f"FS_TEST={fs_test}"]
    subprocess.run(["make", "-s", "-j2", "build", *options], cwd=ROOT, check=True)
    target = ROOT / subprocess.check_output(["make", "-s", "print-target", *options], cwd=ROOT, text=True).strip()
    CACHE.mkdir(parents=True, exist_ok=True)
    image = image or CACHE / f"{test}-{harts}-{time.time_ns()}.img"
    subprocess.run(["make", "-s", "disk", f"DISKIMG={image}", *options], cwd=ROOT, check=True)
    command = ["qemu-system-riscv64", "-machine", "virt", "-bios", "default", "-kernel",
               str(target / "kernel/kernel-qemu.elf"), "-m", "130M", "-smp", str(harts),
               "-nographic", "-drive", f"file={image},if=none,format=raw,id=fs0",
               "-device", "virtio-blk-device,drive=fs0,bus=virtio-mmio-bus.0"]
    log_path = target / "test.log"
    with log_path.open("wb") as log:
        p = subprocess.Popen(command, cwd=ROOT, stdin=subprocess.PIPE, stdout=log, stderr=subprocess.STDOUT)
        deadline = time.monotonic() + timeout
        while p.poll() is None and time.monotonic() < deadline:
            text = log_path.read_text(errors="replace")
            if input_bytes and "INPUT:" in text:
                p.stdin.write(input_bytes)
                p.stdin.flush()
                input_bytes = b""
            if marker in text or "panic:" in text or "_FAIL" in text:
                break
            time.sleep(0.1)
        if p.poll() is None:
            p.terminate()
        p.wait(timeout=5)
        p.stdin.close()
    text = log_path.read_text(errors="replace")
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
    parser.add_argument("lab", choices=["lab7", "bitmap", "lab8", "lab8-full"])
    args = parser.parse_args()
    if args.lab == "lab7":
        for harts in (1, 2):
            run(args.lab, args.lab.upper()+"_PASS", harts, timeout=8)
        run("lab7", "LAB7_PASS", buffers=16384, timeout=8)
    if args.lab in ("lab7", "bitmap"):
        bitmap_test()
    if args.lab == "lab8":
        for harts in (1, 2):
            run("lab8", "LAB8_PASS", harts, buffers=64, fs_test=1, timeout=900)
    if args.lab in ("lab8", "lab8-full"):
        options=["TEST=lab8", "NBUFFER=8", "FS_TEST=2"]
        subprocess.run(["make", "-s", "-j2", "build", *options], cwd=ROOT, check=True)
        target=ROOT/subprocess.check_output(["make", "-s", "print-target", *options],cwd=ROOT,text=True).strip()
        subprocess.run(["make", "-s", str(target.relative_to(ROOT)/"mkfs"), *options],cwd=ROOT,check=True)
        for blocks in (18,2068):
            image=CACHE/f"full-{blocks}-{time.time_ns()}.img"
            subprocess.run([str(target/"mkfs"),str(image),str(blocks)],check=True)
            run("lab8","LAB8_FULL_PASS",image=image,fs_test=2,timeout=60)
