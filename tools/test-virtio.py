#!/usr/bin/env python3
"""Exercise four simultaneous raw-block writers on Lab7/Lab8 in VMware."""
import argparse
import pathlib
import re
import subprocess
import time

ROOT=pathlib.Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument("--harts",type=int,choices=(1,2),default=2)
parser.add_argument("--rounds",type=int,choices=range(1,11),default=3)
args=parser.parse_args()
if "SYS_get_block" not in (ROOT/"src/user/syscall_num.h").read_text():
    parser.error("Use this test on lab-7/lab-8; Lab9 uses tools/test-fs.py lab9.")
options=["TEST=lab7_irq",f"CPUNUM={args.harts}","NBUFFER=8"]
subprocess.run(["make","-s","-j2","build",*options],cwd=ROOT,check=True)
target=ROOT/subprocess.check_output(["make","-s","print-target",*options],cwd=ROOT,text=True).strip()
cache=pathlib.Path.home()/".cache/ecnu-oslab/lab789/virtio"
cache.mkdir(parents=True,exist_ok=True)
for iteration in range(args.rounds):
    image=cache/f"irq-{args.harts}-{time.time_ns()}.img"
    subprocess.run(["make","-s","disk",f"DISKIMG={image}",*options],cwd=ROOT,check=True)
    log_path=target/f"irq-{iteration}.log"
    command=["qemu-system-riscv64","-machine","virt","-bios","default","-kernel",
             str(target/"kernel/kernel-qemu.elf"),"-m","130M","-smp",str(args.harts),"-nographic",
             "-drive",f"file={image},if=none,format=raw,id=fs0",
             "-device","virtio-blk-device,drive=fs0,bus=virtio-mmio-bus.0"]
    with log_path.open("wb") as log:
        p=subprocess.Popen(command,stdin=subprocess.DEVNULL,stdout=log,stderr=subprocess.STDOUT)
        try:
            deadline=time.monotonic()+90
            while p.poll() is None and time.monotonic()<deadline:
                text=log_path.read_text(errors="replace")
                if "LAB7_IRQ_PASS" in text or "panic:" in text or "_FAIL" in text:
                    break
                time.sleep(.1)
        finally:
            if p.poll() is None:
                p.terminate()
            p.wait(timeout=5)
    text=log_path.read_text(errors="replace")
    assert "LAB7_IRQ_PASS" in text and "panic:" not in text and "_FAIL" not in text,text
    assert sorted(re.findall(r"cpu (\d+) is booting with OpenSBI!",text))==[str(i) for i in range(args.harts)]
    with image.open("rb") as f:
        for child in range(4):
            f.seek((5000+child)*4096)
            expected=(127).to_bytes(4,"little")+bytes([ord('A')+child])*4092
            assert f.read(4096)==expected,"Disk contents differ"
    print(f"PASS virtio harts={args.harts} round={iteration+1} image={image}",flush=True)
