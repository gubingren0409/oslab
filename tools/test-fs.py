#!/usr/bin/env python3
"""Filesystem tests; run inside the original VMware Ubuntu guest."""
import argparse
import pathlib
import subprocess
import time
import os
import re

ROOT = pathlib.Path(__file__).resolve().parents[1]
CACHE = pathlib.Path(os.environ.get("OSLAB_FS_CACHE", str(pathlib.Path.home() / ".cache/ecnu-oslab/lab789")))

def run(test, marker, harts=2, buffers=8, timeout=30, image=None, input_bytes=b"", fs_test=0):
    options = [f"TEST={test}", f"CPUNUM={harts}", f"NBUFFER={buffers}", f"FS_TEST={fs_test}"]
    if test == "lab8":
        options.append("LEGACY_TEST=1")
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
    assert sorted(re.findall(r"cpu (\d+) is booting with OpenSBI!",text)) == [str(i) for i in range(harts)],text
    print(f"PASS {test}, harts={harts}, buffers={buffers}, image={image}", flush=True)
    return text, image

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("lab", choices=["lab8", "lab8-full", "lab9"])
    args = parser.parse_args()
    CACHE.mkdir(parents=True,exist_ok=True)
    if args.lab == "lab8":
        for harts in (1, 2):
            run("lab8", "LAB8_PASS", harts, buffers=64, fs_test=1, timeout=900)
    if args.lab in ("lab8", "lab8-full"):
        options=["TEST=lab8", "NBUFFER=8", "FS_TEST=2"]
        subprocess.run(["make", "-s", "-j2", "build", *options], cwd=ROOT, check=True)
        target=ROOT/subprocess.check_output(["make", "-s", "print-target", *options],cwd=ROOT,text=True).strip()
        subprocess.run(["make", "-s", str(target.relative_to(ROOT)/"mkfs"), *options],cwd=ROOT,check=True)
        # Lab9 also creates the one-block /dev directory during mount.
        for blocks in (19,2069):
            image=CACHE/f"full-{blocks}-{time.time_ns()}.img"
            subprocess.run([str(target/"mkfs"),str(image),str(blocks)],check=True)
            run("lab8","LAB8_FULL_PASS",image=image,fs_test=2,timeout=60)
    if args.lab == "lab9":
        for harts in (1,2):
            text,image=run("lab9","LAB9_PASS",harts,buffers=64,timeout=300,
                input_bytes=b"bad\x15helx\x7flo\rHello\nGuess who I am\nHow many free memory left\nGood job\n")
            for marker in ("COURSE_TEST_1_PASS","COURSE_TEST_2_PASS","COURSE_TEST_3_PASS","COURSE_TEST_4_PASS",
                           "LAB9_EDGE_PASS","LAB9_ELF_PASS","LAB9_FS_PASS","Hi, I am gpt0!","Thanks for your kind words!"):
                assert marker in text,text
            assert "OUTPUT: \x00hello\n" in text,text
            run("lab9_persist","LAB9_COLD_PASS",3-harts,buffers=64,timeout=30,image=image)
            options=["TEST=lab9",f"CPUNUM={harts}","NBUFFER=64"]
            target=ROOT/subprocess.check_output(["make","-s","print-target",*options],cwd=ROOT,text=True).strip()
            audit=subprocess.check_output(["python3","tools/check-fs-image.py",str(image),
                "--program-dir",str(target/"user")],cwd=ROOT,text=True)
            (target/"image-audit.json").write_text(audit)
            print(f"PASS image audit, harts={harts}",flush=True)
