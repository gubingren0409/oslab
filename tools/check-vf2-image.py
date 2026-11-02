#!/usr/bin/env python3
"""Check the actual VF2 ELF and Image before handing them to U-Boot."""
import argparse
import hashlib
import json
import pathlib
import struct
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("prefix", type=pathlib.Path,
                    help="artifact path without .elf/.bin")
prefix = parser.parse_args().prefix
elf = prefix.with_suffix(".elf").read_bytes()
image = prefix.with_suffix(".bin").read_bytes()
assert elf[:6] == b"\x7fELF\x02\x01", "Expected little-endian ELF64"
assert struct.unpack_from("<H", elf, 18)[0] == 243, "Expected RISC-V ELF"
entry = struct.unpack_from("<Q", elf, 24)[0]
symbols = {}
sizes = {}
for line in subprocess.check_output(
        ["riscv64-linux-gnu-nm", "-n", "-S", str(prefix.with_suffix('.elf'))],
        text=True).splitlines():
    fields = line.split()
    if len(fields) in (3, 4):
        symbols[fields[-1]] = int(fields[0], 16)
        if len(fields) == 4:
            sizes[fields[-1]] = int(fields[1], 16)

assert len(image) >= 64, "Truncated Image header"
code0, code1, offset, size, flags, version, res1, res2, magic, magic2, res3 = \
    struct.unpack_from("<IIQQQIIQQII", image)
jump = (((code0 >> 31) & 1) << 20 | ((code0 >> 21) & 0x3ff) << 1 |
        ((code0 >> 20) & 1) << 11 | ((code0 >> 12) & 0xff) << 12)
assert code0 & 0xfff == 0x06f and jump == 64, "Entry must jump over 64-byte header"
assert code1 == 0x13, "Second instruction must be nop"
assert offset == 0x200000 and entry == symbols['_entry'] == 0x40200000
assert version == 2 and magic == 0x5643534952 and magic2 == 0x05435352
assert flags == res1 == res2 == res3 == 0, "Reserved header fields must be zero"
assert size == symbols['KERNEL_IMAGE_SIZE'] == symbols['ALLOC_BEGIN'] - entry
assert size > 0 and size % 4096 == 0 and len(image) <= size < 128 * 1024 * 1024
assert symbols['BSS_BEGIN'] % 8 == symbols['BSS_END'] % 8 == 0
assert symbols['BSS_BEGIN'] <= symbols['CPU_stack']
assert sizes['CPU_stack'] == 5 * 4096
assert symbols['CPU_stack'] + sizes['CPU_stack'] <= symbols['BSS_END']
assert symbols['BSS_END'] <= symbols['ALLOC_BEGIN'] < symbols['ALLOC_END']
assert symbols['ALLOC_END'] == entry + 128 * 1024 * 1024
print(json.dumps({"status": "passed", "prefix": str(prefix), "entry": hex(entry),
                  "text_offset": hex(offset), "image_size": size,
                  "file_size": len(image), "managed_end": hex(symbols['ALLOC_END']),
                  "sha256": hashlib.sha256(image).hexdigest()}, indent=2))
