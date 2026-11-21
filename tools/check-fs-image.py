#!/usr/bin/env python3
"""Read-only audit of a completed Lab9 image, independent of the kernel."""
import argparse
import hashlib
import json
import pathlib
import struct

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("image", type=pathlib.Path)
parser.add_argument("--program-dir", type=pathlib.Path, required=True)
args = parser.parse_args()
assert args.image.is_file(), "Expected a regular image file"
f = args.image.open("rb")
sb = struct.unpack("<12I", f.read(48))
assert sb[0] == 0x12341234 and sb[1] == 4096
assert args.image.stat().st_size == sb[2] * 4096

def block(number):
    assert 0 <= number < sb[2]
    f.seek(number * 4096)
    data = f.read(4096)
    assert len(data) == 4096
    return data

def allocated(first, count):
    bits = b"".join(block(first + i) for i in range((count + 32767) // 32768))
    return {i for i in range(count) if bits[i // 8] & (1 << (i % 8))}

disk_inodes = allocated(sb[4], sb[3])
disk_blocks = {sb[10] + i for i in allocated(sb[8], sb[11])}
owned = {}
nodes = {}
links = {0: 1}
paths = {}

def load_inode(number):
    assert number in disk_inodes
    if number in nodes:
        return nodes[number]
    f.seek(sb[6] * 4096 + number * 64)
    fields = struct.unpack("<4h14I", f.read(64))
    kind, major, minor, nlink, size, *indexes = fields
    assert kind in (0, 1, 2) and nlink > 0
    mapping = {}

    def visit(b, level, logical):
        if not b:
            return
        assert sb[10] <= b < sb[2] and b not in owned, "Overlapping/invalid blocks"
        owned[b] = number
        if not level:
            mapping[logical] = b
        else:
            entries = struct.unpack("<1024I", block(b))
            span = 1024 ** (level - 1)
            for i, child in enumerate(entries):
                visit(child, level - 1, logical + i * span)

    for i in range(10):
        visit(indexes[i], 0, i)
    visit(indexes[10], 1, 10)
    visit(indexes[11], 1, 1034)
    visit(indexes[12], 2, 2058)
    expected = (size + 4095) // 4096 if kind == 0 else int(size > 0) if kind == 1 else 0
    assert set(mapping) == set(range(expected)), "Hole or blocks beyond EOF"
    if kind == 1:
        assert size <= 4096 and size % 64 == 0
    if kind == 2:
        assert size == 0
    node = dict(type=kind, major=major, nlink=nlink, size=size, mapping=mapping)
    nodes[number] = node
    return node

def file_bytes(number):
    node = load_inode(number)
    assert node["type"] == 0
    return b"".join(block(node["mapping"][i]) for i in range(len(node["mapping"])))[:node["size"]]

queue = [(0, 0, "/")]
directories = set()
while queue:
    number, parent, path = queue.pop(0)
    node = load_inode(number)
    paths[path] = number
    if node["type"] != 1:
        continue
    assert number not in directories, "Directory alias/cycle"
    directories.add(number)
    raw = block(node["mapping"][0])
    entries = {}
    for i in range(64):
        name, child = struct.unpack_from("<60sI", raw, i * 64)
        if not name[0]:
            continue
        assert b"\0" in name
        name = name.split(b"\0", 1)[0].decode("utf-8")
        assert name not in entries and "/" not in name
        entries[name] = child
    assert entries["."] == number and entries[".."] == parent
    assert len(entries) * 64 == node["size"]
    for name, child in entries.items():
        if name in (".", ".."):
            continue
        links[child] = links.get(child, 0) + 1
        queue.append((child, number, path.rstrip("/") + "/" + name))

assert set(nodes) == disk_inodes, "Orphan or missing inode"
assert set(owned) == disk_blocks, "Leaked or unallocated referenced block"
for number, node in nodes.items():
    assert node["nlink"] == links[number], "Link count mismatch"

programs = "test_1 test_2 test_3 test_4 edge elfprobe fsprobe persistprobe".split()
expected_paths = {"/", "/dev", "/ABCD.txt", "/abcd.txt", "/ABC.txt", "/persist.txt"}
expected_paths.update("/" + name for name in programs)
expected_paths.update("/dev/" + name for name in ("stdin", "stdout", "stderr", "zero", "null", "gpt0"))
assert set(paths) == expected_paths, "Unexpected leftovers/missing files"
files = {}
for name in programs:
    packed = file_bytes(paths["/" + name])
    original = (args.program_dir / (name + ".elf")).read_bytes()
    assert packed == original, "Packed ELF differs: " + name
    files[name] = dict(size=len(packed), sha256=hashlib.sha256(packed).hexdigest())
assert file_bytes(paths["/ABCD.txt"]) == b"ABCDEFGHIJKLMNOPQRSTUVWXYZ" * 200
assert file_bytes(paths["/abcd.txt"]) == b"abcdefghijklmnopqrstuvwxyz" * 500
assert file_bytes(paths["/ABC.txt"]) == b"".join(f"{i}:ABCDEFGHIJKLMNOPQRST ".encode() for i in range(500))
assert file_bytes(paths["/persist.txt"]) == b"LAB9_PERSIST_OK\n"
for major, name in enumerate(("stdin", "stdout", "stderr", "zero", "null", "gpt0"), 2):
    node = nodes[paths["/dev/" + name]]
    assert node["type"] == 2 and node["major"] == major
print(json.dumps(dict(status="passed", image=str(args.image),
                     allocated_inodes=len(nodes), allocated_data_blocks=len(owned),
                     programs=files), indent=2))
