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
    if harts != 2:
        target = ROOT / "target" / "qemu" / f"hart{harts}" / test
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
    if arguments.lab == "lab5":
        run_image("lab5", "LAB5_PASS", selftest=True)
    else:
        for harts in (1, 2):
            text = run_image("lab6", "LAB6_PASS", harts, selftest=True)
            assert text.index("LAB6_RUNNABLE_WHILE_SLEEP") < text.index("LAB6_SLEEP_WAKE"), text
            text = run_image("lab6_fork", "LAB6_FORK_PASS", harts, timeout=6)
            for level, expected in ((1, 1), (2, 2), (3, 4)):
                assert text.count(f"level-{level}!") == expected, text
            run_image("lab6_slots", "LAB6_SLOTS_PASS", harts, timeout=6)
            run_image("lab6_locks", "LAB6_LOCKS_PASS", harts, selftest=True, timeout=4)
            run_image("lab6_initexit", "LAB6_INITEXIT_PASS", harts, timeout=3)
