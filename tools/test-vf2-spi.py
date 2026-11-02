#!/usr/bin/env python3
"""Offline checks for SPI write boundaries and verification; no hardware access."""
import importlib.util
from pathlib import Path
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location("spi", Path(__file__).with_name("vf2-spi-update.py"))
spi = importlib.util.module_from_spec(spec)
spec.loader.exec_module(spi)


class SpiTests(unittest.TestCase):
    def test_layout_preserves_environment_and_unwritten_regions(self):
        old = bytes(range(256)) * 65536
        new = spi.updated(old, {"mtd0": b"S" * 4097, "mtd2": b"F" * 8193})
        self.assertEqual(len(new), len(old))
        self.assertEqual(new[:4097], b"S" * 4097)
        self.assertEqual(new[4097:8192], b"\xff" * 4095)
        self.assertEqual(new[8192:0x100000], old[8192:0x100000])
        self.assertEqual(new[0x100000:0x102001], b"F" * 8193)
        self.assertEqual(new[0x102001:0x103000], b"\xff" * 4095)
        self.assertEqual(new[0x103000:], old[0x103000:])

    def test_partial_writes_and_erase_alignment(self):
        chip = bytearray(b"X" * 8192)
        position = [0]

        def erase(descriptor, request, info):
            self.assertEqual((descriptor, request), (42, spi.MEMERASE))
            self.assertEqual(spi.struct.unpack("<II", info), (0, 8192))
            chip[:] = b"\xff" * 8192

        def write(descriptor, data):
            count = min(len(data), 7)
            start = position[0]
            chip[start:start + count] = data[:count]
            position[0] += count
            return count

        payload = b"v" * 4097
        with patch.object(spi.os, "open", return_value=42), \
                patch.object(spi.os, "close"), patch.object(spi.os, "lseek"), \
                patch.object(spi.os, "write", side_effect=write), \
                patch.object(spi.fcntl, "ioctl", side_effect=erase), \
                patch.object(spi, "read_prefix", side_effect=lambda path, size: bytes(chip[:size])):
            spi.write_partition("mtd0", payload, 0xf0000)
        self.assertEqual(chip, payload + b"\xff" * 4095)

    def test_environment_and_overflow_never_open_a_device(self):
        with patch.object(spi.os, "open") as opening:
            with self.assertRaises(RuntimeError):
                spi.write_partition("mtd1", b"x", 65536)
            with self.assertRaises(RuntimeError):
                spi.write_partition("mtd0", b"x" * 8193, 8192)
            opening.assert_not_called()

    def test_failed_erase_never_writes(self):
        with patch.object(spi.os, "open", return_value=42), \
                patch.object(spi.os, "close"), patch.object(spi.fcntl, "ioctl"), \
                patch.object(spi, "read_prefix", return_value=b"\0" * 4096), \
                patch.object(spi.os, "write") as writing:
            with self.assertRaises(RuntimeError):
                spi.write_partition("mtd0", b"x", 0xf0000)
            writing.assert_not_called()


if __name__ == "__main__":
    unittest.main()
