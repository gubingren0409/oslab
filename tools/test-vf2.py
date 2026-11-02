#!/usr/bin/env python3
"""Verify SELFTEST Images already copied to /boot/oslab, using minicom in VMware."""
import argparse
import json
import os
import re
import subprocess
import time

from vf2_console import Console, PROMPT, SETUP

DTB = "/dtbs/6.12.5-starfive/starfive/jh7110-starfive-visionfive-2-v1.3b.dtb"
MARKERS = {
    "lab5": "LAB5_PASS", "lab6": "LAB6_PASS",
    "lab6_fork": "LAB6_FORK_PASS", "lab6_slots": "LAB6_SLOTS_PASS",
    "lab6_locks": "LAB6_LOCKS_PASS", "lab6_initexit": "LAB6_INITEXIT_PASS",
}


def run(test, boot_hart=None, reset=False):
    console = Console()
    manifest = json.loads((SETUP / "artifacts/board-images-manifest.json").read_text())
    name = f"{test}-selftest.bin"
    label = test + (f"-hart{boot_hart}" if boot_hart else "-direct")
    start = console.mark()
    result = {"case": test, "boot": boot_hart or "direct", "image": manifest[name]}

    def load(filename, address):
        expected = manifest[filename]
        output = console.uboot(f"load mmc 1:3 {address:#x} /oslab/{filename}")
        assert re.search(rf"(?m)^{expected['size']} bytes read\b", output), output
        output = console.uboot(f"crc32 {address:#x} {expected['size']:x}")
        assert re.search(r"==>\s*" + expected["crc32"] + r"\b", output, re.I), output

    try:
        console.uboot("echo OSLAB_UBOOT_READY")
        console.uboot("setenv fdt_high 0xffffffffffffffff")
        load(name, 0x40200000)
        output = console.uboot(f"load mmc 1:3 0x4a000000 {DTB}")
        assert re.search(r"(?m)^58144 bytes read\b", output), output
        output = console.uboot("crc32 0x4a000000 e320")
        assert re.search(r"==>\s*4d9b2678\b", output, re.I), output
        entry = 0x40200000
        if boot_hart:
            load("vf2-hart-loader.bin", 0x49000000)
            console.uboot(f"mw.l 0x49000410 {boot_hart:x}")
            entry = 0x49000000
        mark = console.mark()
        console.send(f"booti {entry:#x} - 0x4a000000")
        required = ["MEMORY_SELFTEST_PASS", "MMAP_CONCURRENCY_PASS harts=4",
                    "TIMER_SELFTEST_PASS ticks=20 interval=400000", MARKERS[test]]
        deadline = time.monotonic() + 60
        while time.monotonic() < deadline:
            output = console.read(mark)
            assert "panic:" not in output and "_FAIL" not in output, output
            if all(marker in output for marker in required):
                break
            time.sleep(0.1)
        else:
            raise RuntimeError("Kernel result timed out:\n" + output)
        actual = int(re.search(r"SELFTEST_BOOT_HART=(\d+)", output).group(1))
        assert actual in range(1, 5), output
        if boot_hart:
            assert actual == boot_hart, output
        assert sorted(re.findall(r"cpu (\d+) is booting", output)) == ["1", "2", "3", "4"], output
        assert sorted(re.findall(r"TIMER_HART_PASS hart=(\d+)", output)) == ["1", "2", "3", "4"], output
        if test == "lab6":
            assert output.index("LAB6_RUNNABLE_WHILE_SLEEP") < output.index("LAB6_SLEEP_WAKE"), output
        if test == "lab6_fork":
            for level, count in ((1, 1), (2, 2), (3, 4)):
                assert output.count(f"level-{level}!") == count, output
        uart_mark = console.mark()
        console.send("A", enter=False)
        uart = console.wait(uart_mark, r"UART_SELFTEST_PASS irq=32 hart=[1-4]", timeout=10)
        result.update(status="passed", actual_boot_hart=actual, active_harts=[1, 2, 3, 4],
                      uart_irq=32, timer_interval=400000)
        print(output + uart, flush=True)
    except Exception:
        result["status"] = "failed"
        raise
    finally:
        (SETUP / f"logs/{label}.log").write_text(console.read(start))
        (SETUP / f"logs/{label}.json").write_text(json.dumps(result, indent=2) + "\n")
    print(f"PASS {label}", flush=True)
    if reset:
        mark = console.mark()
        console.key("C-r")
        try:
            console.wait_uboot(mark, timeout=45)
        finally:
            (SETUP / f"logs/{label}-reset.log").write_text(console.read(mark))
        print("Returned to U-Boot", flush=True)


def suite(resume=False):
    """Wait for physical resets; automatically load the remaining test Images."""
    console = Console()
    (SETUP / "logs/vf2-suite.pid").write_text(str(os.getpid()))
    cases = [(name, None) for name in
             ("lab6_locks", "lab6_fork", "lab6_slots", "lab6_initexit")]
    cases += [("lab6", hart) for hart in range(1, 5)]
    cases += [("lab5", hart) for hart in range(2, 5)]
    result_path = SETUP / "logs/vf2-suite-results.json"
    results = json.loads(result_path.read_text()) if resume and result_path.exists() else []
    done = {(item["case"], item["boot"]) for item in results if item["status"] == "passed"}
    # Include a reset already pressed after the previous validated UART probe.
    raw = (SETUP / "logs/vf2-serial-boot.log").read_bytes()
    mark = raw.rfind(b"UART_SELFTEST_PASS irq=32")
    if mark < 0:
        mark = console.mark()
    ready = bool(re.search(PROMPT + r"\Z", console.read(max(0, console.mark() - 4096))))
    for number, (test, hart) in enumerate(cases, 1):
        if (test, hart or "direct") in done:
            continue
        message = f"PASS / HOLD RESET 4s: next {number}/{len(cases)} {test} hart={hart or 'direct'}"
        subprocess.run(["tmux", "display-message", "-t", "vf2-serial", "-d", "600000", message],
                       check=True)
        print(message, flush=True)
        if not ready:
            console.wait_uboot(mark, timeout=900)
        ready = False
        subprocess.run(["tmux", "display-message", "-t", "vf2-serial", "-d", "1000",
                        f"RUNNING {test} hart={hart or 'direct'}"], check=True)
        run(test, hart)
        results.append({"case": test, "boot": hart or "direct", "status": "passed"})
        result_path.write_text(json.dumps(results, indent=2) + "\n")
        mark = console.mark()
    message = "ALL TESTS PASS / HOLD RESET 4s once more to start Ubuntu"
    subprocess.run(["tmux", "display-message", "-t", "vf2-serial", "-d", "600000", message],
                   check=True)
    print(message, flush=True)
    console.wait(mark, r"(?m)^starfive login:\s*$", timeout=900)
    console.send("user")
    console.wait(mark, r"(?m)^Password:\s*$", timeout=15)
    console.send("starfive")
    console.wait(mark, r"user@starfive:.*\$", timeout=60)
    output = console.linux("uname -r; nproc; findmnt -n -o SOURCE,TARGET /; "
                           "cd /boot/oslab && sha256sum -c SHA256SUMS")
    (SETUP / "logs/ubuntu-restored.txt").write_text(output)
    print("UBUNTU_RESTORED", flush=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("case", choices=[*MARKERS, "suite"])
    parser.add_argument("--boot-hart", type=int, choices=range(1, 5))
    parser.add_argument("--reset", action="store_true",
                        help="Request SBI SRST after passing (requires working PMIC reset)")
    parser.add_argument("--resume", action="store_true", help="Resume recorded suite progress")
    args = parser.parse_args()
    if args.case == "suite":
        suite(args.resume)
    else:
        run(args.case, args.boot_hart, args.reset)
