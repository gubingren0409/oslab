#!/usr/bin/env python3
"""Prepare, program, or restore this board's SPI using its verified course TF image.

The default action only reads SPI/TF and stages ordinary files. Programming is a
separate explicit action, to be used after the proposed plan has been approved.
No TF partition or U-Boot environment is written.
"""
import argparse
import fcntl
import gzip
import hashlib
import json
import os
from pathlib import Path
import platform
import stat
import struct


ROOT = Path("/home/user/oslab-kernels")
BACKUP = ROOT / "spi-backup-before-reset-fix/spi-full.bin.gz"
STAGE = ROOT / "spi-reset-plan"
OLD_SHA = "0beea703af950b3d1dbf871ba63675d2aa663a9cf0eaf8e9b4524733befbb5dc"
BACKUP_SHA = "589d427e959aaa22bb3499a20da5266091f9a4bc07f89e7e7056b65d0bb3f59e"
SPL_SHA = "a1a6531e9ca1c7935d6c97528e20a6d6cd5bff0da293166f84f1c23ec44c38ee"
FIT_SHA = "41e9601d59dc4042269250ae6a2d57d2a6ec9424e6a0fdb36483fc3876c8c7b1"
PARTS = [("mtd0", "spl", 0, 0xf0000),
         ("mtd1", "uboot-env", 0xf0000, 0x10000),
         ("mtd2", "uboot", 0x100000, 0xf00000)]
MEMGETINFO = 0x80204d01  # Linux mtd-abi.h, struct mtd_info_user (32 bytes).
MEMERASE = 0x40084d02    # struct erase_info_user: uint32 start, length.
ERASE_SIZE = 4096


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def read_prefix(path, size):
    with open(path, "rb", buffering=0) as stream:
        data = bytearray()
        while len(data) < size:
            chunk = stream.read(size - len(data))
            require(chunk, "Short read from " + str(path))
            data.extend(chunk)
    return bytes(data)


def check_board():
    require(platform.machine() == "riscv64" and os.geteuid() == 0,
            "Run with sudo on the physical VisionFive 2")
    model = Path("/proc/device-tree/model").read_text().rstrip("\0")
    require("StarFive" in model and "VisionFive 2 v1.3B" in model,
            "Unexpected board model: " + model)
    for dev, name, _, size in PARTS:
        device = Path("/dev") / dev
        require(stat.S_ISCHR(device.stat().st_mode), "Expected MTD character device")
        require((Path("/sys/class/mtd") / dev / "name").read_text().strip() == name,
                "Unexpected partition name")
        with device.open("rb", buffering=0) as stream:
            info = bytearray(32)
            fcntl.ioctl(stream.fileno(), MEMGETINFO, info, True)
        kind, flags, actual, erase, writesize, oobsize, _ = struct.unpack("<B3x5IQ", info)
        require((kind, actual, erase, writesize, oobsize) == (3, size, ERASE_SIZE, 1, 0),
                "Unexpected NOR geometry for " + dev)
        require(flags & 0x400, "MTD is not writable: " + dev)
    return model


def original():
    compressed = BACKUP.read_bytes()
    require(sha(compressed) == BACKUP_SHA, "Backup archive SHA256 mismatch")
    data = gzip.decompress(compressed)
    require(len(data) == 0x1000000 and sha(data) == OLD_SHA,
            "Backup size/SHA256 mismatch")
    return data


def live_spi():
    return b"".join(read_prefix("/dev/" + dev, size) for dev, _, _, size in PARTS)


def candidates():
    spl = read_prefix("/dev/mmcblk1p1", 0xf0000)
    header = read_prefix("/dev/mmcblk1p2", 8)
    require(struct.unpack(">II", header) == (0xd00dfeed, 3221045),
            "Unexpected TF firmware FIT header")
    fit = read_prefix("/dev/mmcblk1p2", 3221045)
    require(sha(spl) == SPL_SHA and sha(fit) == FIT_SHA,
            "TF firmware differs from the verified official 6.0 image")
    return {"mtd0": spl, "mtd2": fit}


def updated(original_data, images):
    result = bytearray(original_data)
    for dev, _, offset, _ in PARTS:
        if dev in images:
            data = images[dev]
            erase_length = (len(data) + ERASE_SIZE - 1) // ERASE_SIZE * ERASE_SIZE
            result[offset:offset + erase_length] = b"\xff" * erase_length
            result[offset:offset + len(data)] = data
    return bytes(result)


def write_partition(dev, data, size):
    length = (len(data) + ERASE_SIZE - 1) // ERASE_SIZE * ERASE_SIZE
    require(dev in ("mtd0", "mtd2") and length <= size, "Invalid write range")
    descriptor = os.open("/dev/" + dev, os.O_RDWR | os.O_SYNC)
    try:
        print("Erasing " + dev + ": " + hex(length), flush=True)
        fcntl.ioctl(descriptor, MEMERASE, struct.pack("<II", 0, length))
        require(read_prefix("/dev/" + dev, length) == b"\xff" * length,
                "Erase verification failed: " + dev)
        os.lseek(descriptor, 0, os.SEEK_SET)
        written = 0
        while written < len(data):
            count = os.write(descriptor, data[written:written + 65536])
            require(count > 0, "Short MTD write: " + dev)
            written += count
    finally:
        os.close(descriptor)
    require(read_prefix("/dev/" + dev, len(data)) == data,
            "Write verification failed: " + dev)
    print("Verified " + dev + ": " + sha(data), flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", nargs="?", default="prepare",
                        choices=("prepare", "program", "restore"))
    parser.add_argument("--expect-current-sha256")
    args = parser.parse_args()
    model = check_board()
    STAGE.mkdir(exist_ok=True)
    with (STAGE / "operation.lock").open("a") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        old = original()
        live = live_spi()
        require(live[0xf0000:0x100000] == old[0xf0000:0x100000],
                "U-Boot environment has changed; stop and review")
        if args.action == "restore":
            require(args.expect_current_sha256 == sha(live),
                    "Restore requires the current complete SPI SHA256")
            for dev, _, offset, size in reversed(PARTS):
                if dev != "mtd1":
                    write_partition(dev, old[offset:offset + size], size)
            require(live_spi() == old, "Full SPI restore verification failed")
            result = {"status": "restored", "sha256": OLD_SHA}
        else:
            require(sha(live) == OLD_SHA, "Live SPI differs from its original backup")
            images = candidates()
            new = updated(old, images)
            plan = {"status": "prepared", "read_only": True, "board": model,
                    "source": "Existing official Ubuntu 24.04.3 VF2 6.0 TF image",
                    "backup": str(BACKUP), "old_spi_sha256": OLD_SHA,
                    "expected_spi_sha256": sha(new), "environment_preserved": True,
                    "tf_partitions_written": False, "writes": []}
            for dev, _, offset, size in reversed(PARTS):
                if dev in images:
                    data = images[dev]
                    (STAGE / (dev + "-candidate.bin")).write_bytes(data)
                    plan["writes"].append({"device": "/dev/" + dev,
                        "chip_offset": offset, "partition_size": size,
                        "payload_size": len(data), "sha256": sha(data),
                        "erase_size": (len(data) + 4095) // 4096 * 4096})
            (STAGE / "plan.json").write_text(json.dumps(plan, indent=2) + "\n")
            if args.action == "program":
                require(args.expect_current_sha256 == OLD_SHA,
                        "Programming requires the expected original SPI SHA256")
                for dev, _, _, size in reversed(PARTS):
                    if dev in images:
                        write_partition(dev, images[dev], size)
                require(live_spi() == new, "Complete SPI verification failed")
                plan.update(status="programmed_and_verified", read_only=False)
            result = plan
        (STAGE / (args.action + "-result.json")).write_text(json.dumps(result, indent=2) + "\n")
        print(json.dumps(result, indent=2), flush=True)


if __name__ == "__main__":
    main()
