#!/usr/bin/env python3
"""Read-only SPI backup; run as root on the physical course-image Ubuntu."""
import gzip
import hashlib
import json
import os
from pathlib import Path
import platform
import re

if platform.machine() != "riscv64" or os.geteuid() != 0:
    raise SystemExit("Run with sudo on the VisionFive 2, not on the build VM")

out = Path("/home/user/oslab-kernels/spi-backup-before-reset-fix")
out.mkdir(exist_ok=True)
expected = [("mtd0", "spl", 0xf0000), ("mtd1", "uboot-env", 0x10000),
            ("mtd2", "uboot", 0xf00000)]
records = []
full = bytearray()
offset = 0
for device, name, size in expected:
    sysfs = Path("/sys/class/mtd") / device
    assert sysfs.joinpath("name").read_text().strip() == name
    assert int(sysfs.joinpath("size").read_text()) == size
    data = Path("/dev").joinpath(device).read_bytes()
    assert len(data) == size
    full.extend(data)
    archive = out / (device + ".bin.gz")
    compressed = gzip.compress(data, mtime=0)
    if archive.exists():
        assert gzip.decompress(archive.read_bytes()) == data, "Existing backup differs"
    else:
        archive.write_bytes(compressed)
    assert gzip.decompress(archive.read_bytes()) == data
    records.append({"device": "/dev/" + device, "name": name, "offset": offset,
                    "size": size, "sha256": hashlib.sha256(data).hexdigest(),
                    "non_ff_bytes": len(data) - data.count(255),
                    "first64": data[:64].hex(),
                    "versions": [s.decode(errors="replace") for s in
                                 re.findall(rb"U-Boot 20\d\d\.\d\d[^\x00\r\n]{0,100}", data)[:3]]})
    offset += size
assert offset == 0x1000000
archive = out / "spi-full.bin.gz"
if archive.exists():
    assert gzip.decompress(archive.read_bytes()) == full
else:
    archive.write_bytes(gzip.compress(full, mtime=0))
assert gzip.decompress(archive.read_bytes()) == full
for record in records:
    assert hashlib.sha256(Path(record["device"]).read_bytes()).hexdigest() == record["sha256"]
result = {"status": "verified", "read_only": True, "size": len(full),
          "sha256": hashlib.sha256(full).hexdigest(), "compressed_size": archive.stat().st_size,
          "compressed_sha256": hashlib.sha256(archive.read_bytes()).hexdigest(),
          "backup": str(archive), "partitions": records}
out.joinpath("manifest.json").write_text(json.dumps(result, indent=2) + "\n")
print(json.dumps(result, indent=2))
