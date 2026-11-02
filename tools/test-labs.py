#!/usr/bin/env python3
"""Run bounded regression images inside the course VMware Ubuntu environment."""
import argparse
import pathlib
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[1]


def run_image(test, marker, harts=2, selftest=False, timeout=12):
    options = [f"TEST={test}", f"CPUNUM={harts}"]
    if selftest:
        options.append("SELFTEST=1")
    subprocess.run(["make", "-j2", "build", *options], cwd=ROOT, check=True,
                   stdout=subprocess.DEVNULL)
    target = ROOT / "target" / "qemu" / test
    if selftest:
        target /= "selftest"
    command = ["qemu-system-riscv64", "-machine", "virt", "-bios", "default",
               "-kernel", str(target / "kernel/kernel-qemu.elf"), "-m", "130M",
               "-smp", str(harts), "-nographic"]
    process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT, cwd=ROOT)
    try:
        output, _ = process.communicate(timeout=timeout)
    except subprocess.TimeoutExpired:
        process.terminate()
        output, _ = process.communicate(timeout=5)
    text = output.decode(errors="replace")
    (target / "test.log").write_text(text)
    assert marker in text and "panic:" not in text and "_FAIL" not in text, text
    if selftest:
        assert "MEMORY_SELFTEST_PASS" in text, text
        assert f"MMAP_CONCURRENCY_PASS harts={harts}" in text, text
        assert "TIMER_SELFTEST_PASS" in text, text
    print(f"PASS {test}, harts={harts}, selftest={selftest}")
    return text


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("lab", choices=["lab5", "lab6"])
    arguments = parser.parse_args()
    run_image(arguments.lab, arguments.lab.upper() + "_PASS", selftest=True)
