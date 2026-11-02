#!/usr/bin/env python3
"""Drive the existing minicom/tmux console from the VMware Ubuntu guest."""
import argparse
import base64
import hashlib
import os
from pathlib import Path
import re
import shlex
import subprocess
import time
import uuid

SETUP = Path(__file__).resolve().parents[2] / "board-setup"
LOG = SETUP / "logs/vf2-serial-boot.log"
SESSION = "vf2-serial"
PROMPT = r"(?m)^(?:StarFive\s*#|=>)\s*$"


class Console:
    def __init__(self):
        if os.name != "posix":
            raise RuntimeError("Run this tool in the VMware Ubuntu guest")
        subprocess.run(["tmux", "has-session", "-t", SESSION], check=True)

    def mark(self):
        return LOG.stat().st_size

    def read(self, mark):
        with LOG.open("rb") as stream:
            stream.seek(mark)
            text = stream.read().decode("utf-8", errors="replace")
        text = re.sub(r"\x1b\[[0-?]*[ -/]*[@-~]", "", text).replace("\r", "")
        clean = []
        for char in text:
            if char == "\b":
                if clean and clean[-1] != "\n":
                    clean.pop()
            elif char != "\x07":
                clean.append(char)
        return "".join(clean)

    def send(self, text, enter=True):
        subprocess.run(["tmux", "send-keys", "-t", SESSION, "-l", text], check=True)
        if enter:
            self.key("Enter")

    def key(self, key):
        subprocess.run(["tmux", "send-keys", "-t", SESSION, key], check=True)

    def paste(self, data):
        subprocess.run(["tmux", "load-buffer", "-b", "vf2-transfer", "-"],
                       input=data, check=True)
        subprocess.run(["tmux", "paste-buffer", "-d", "-b", "vf2-transfer",
                        "-t", SESSION], check=True)

    def wait(self, mark, pattern, timeout=30):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            text = self.read(mark)
            if re.search(pattern, text):
                return text
            time.sleep(0.1)
        raise RuntimeError(f"Console timeout waiting for {pattern!r}:\n{self.read(mark)[-4000:]}")

    def linux(self, command, timeout=30):
        token = "OSLAB_RC_" + uuid.uuid4().hex[:10]
        mark = self.mark()
        self.send(command + "; printf '\\n" + token + "=%s\\n' \"$?\"")
        deadline = time.monotonic() + timeout
        password_sent = False
        while time.monotonic() < deadline:
            text = self.read(mark)
            if not password_sent and re.search(r"(?m)^OSLAB_SUDO_PASSWORD:\s*$", text):
                self.send("starfive")  # Public factory password of the course image.
                password_sent = True
            result = re.search(r"(?m)^" + token + r"=(\d+)\s*$", text)
            if result:
                if int(result.group(1)):
                    raise RuntimeError("Board command failed:\n" + text)
                return self.wait(mark, r"user@starfive:[^\n]*\$\s*\Z", timeout=5)
            time.sleep(0.1)
        self.key("C-c")
        raise RuntimeError("Board command timed out:\n" + self.read(mark)[-4000:])

    def upload(self, path):
        path = Path(path)
        if not re.fullmatch(r"[A-Za-z0-9_.-]+", path.name):
            raise ValueError("Use a plain artifact filename")
        data = path.read_bytes()
        digest = hashlib.sha256(data).hexdigest()
        destination = "/home/user/oslab-kernels/" + path.name
        temporary = shlex.quote(destination + ".part")
        delimiter = "OSLAB_EOF_" + uuid.uuid4().hex
        self.linux("mkdir -p /home/user/oslab-kernels; OSLAB_TTY=$(stty -g); "
                   "OSLAB_PS2=$PS2; PS2=; stty -echo")
        mark = self.mark()
        self.send("base64 -d > " + temporary + " <<'" + delimiter + "'")
        trailer = (delimiter + "\nstty \"$OSLAB_TTY\"; PS2=$OSLAB_PS2; "
                   "sha256sum " + temporary + "; printf '\\nOSLAB_UPLOAD_END\\n'\n")
        self.paste(base64.encodebytes(data) + trailer.encode())
        output = self.wait(mark, r"(?m)^OSLAB_UPLOAD_END\s*$", timeout=120)
        if not re.search(r"(?m)^" + digest + r"\s+", output):
            raise RuntimeError("Upload SHA256 mismatch:\n" + output)
        self.linux("mv -- " + temporary + " " + shlex.quote(destination))
        return {"file": destination, "size": len(data), "sha256": digest}

    def wait_uboot(self, mark, timeout=35):
        deadline = time.monotonic() + timeout
        last_space = 0
        while time.monotonic() < deadline:
            text = self.read(mark)
            if re.search(PROMPT, text):
                return text
            if "VF2_REBOOT_FAIL" in text or "cannot read pmic power register" in text:
                raise RuntimeError("Software reboot failed:\n" + text[-2000:])
            if "BOOT fail" in text:
                raise RuntimeError("BootROM failed to load from SDIO:\n" + text[-2000:])
            if "U-Boot 2021.10" in text or "Hit any key to stop autoboot" in text:
                if time.monotonic() - last_space > 0.15:
                    self.key("C-c")
                    last_space = time.monotonic()
            if "Starting kernel" in text:
                raise RuntimeError("Missed U-Boot stop window; wait for Ubuntu login")
            time.sleep(0.05)
        raise RuntimeError("U-Boot prompt not reached:\n" + self.read(mark)[-4000:])

    def request_reboot(self):
        """Reboot the running Ubuntu or teaching kernel; return the log offset."""
        tail = self.read(max(0, self.mark() - 4096))
        linux = re.search(r"user@starfive:[^\n]*\$\s*\Z", tail)
        if linux:
            self.linux("sudo -p 'OSLAB_SUDO_PASSWORD:' -v")
        mark = self.mark()
        if linux:
            self.send("sudo -n reboot")
        else:
            self.key("C-r")
        return mark

    def reboot_uboot(self, timeout=60):
        tail = self.read(max(0, self.mark() - 4096))
        if re.search(PROMPT + r"\Z", tail):
            return tail
        return self.wait_uboot(self.request_reboot(), timeout)

    def uboot(self, command, timeout=25):
        if re.search(r"\b(?:saveenv|erase|mmc\s+write|sf\s+(?:write|update))\b", command):
            raise ValueError("Persistent storage writes are outside this runner")
        mark = self.mark()
        self.send(command)
        return self.wait(mark, PROMPT, timeout)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("linux", "upload", "uboot", "reboot-uboot"))
    parser.add_argument("value", nargs="?")
    args = parser.parse_args()
    console = Console()
    if args.action == "linux":
        print(console.linux(args.value))
    elif args.action == "upload":
        print(console.upload(args.value))
    elif args.action == "uboot":
        print(console.uboot(args.value))
    else:
        print(console.reboot_uboot())
