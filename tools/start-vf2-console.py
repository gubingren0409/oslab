#!/usr/bin/env python3
"""Start minicom in tmux, capturing locally and mirroring logs to Windows in batches."""
import argparse
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import time


def pane():
    archive = Path(__file__).resolve().parents[2] / "board-setup/logs/vf2-serial-boot.log"
    capture = Path.home() / ".local/state/oslab/vf2-serial-boot.log"
    capture.parent.mkdir(parents=True, exist_ok=True)
    if not capture.exists():
        shutil.copyfile(archive, capture)
    # Flush any tail left over from a previous interrupted console process.
    offset = archive.stat().st_size
    if capture.stat().st_size < offset:
        raise RuntimeError("Capture/archive sizes differ; preserve logs before restarting")
    with archive.open("ab", buffering=0) as mirror:
        follower = subprocess.Popen(["tail", "-c", "+" + str(offset + 1),
                                     "-f", "-s", "0.2", str(capture)], stdout=mirror)
        try:
            return subprocess.call(["minicom", "-o", "-8", "-C", str(capture), "vf2"])
        finally:
            time.sleep(0.5)
            follower.terminate()
            follower.wait()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--restart", action="store_true", help="Restart the existing minicom pane")
    parser.add_argument("--pane", action="store_true", help=argparse.SUPPRESS)
    args = parser.parse_args()
    if os.name != "posix":
        raise SystemExit("Run in the VMware Ubuntu guest")
    if args.pane:
        return pane()
    session = "vf2-serial"
    exists = subprocess.run(["tmux", "has-session", "-t", session],
                            stderr=subprocess.DEVNULL).returncode == 0
    command = shlex.join([sys.executable, str(Path(__file__).resolve()), "--pane"])
    if exists and args.restart:
        subprocess.run(["tmux", "respawn-pane", "-k", "-t", session, command], check=True)
    elif not exists:
        subprocess.run(["tmux", "new-session", "-d", "-s", session, command], check=True)
    print("Console ready; open with: tmux attach -t " + session)


if __name__ == "__main__":
    raise SystemExit(main())
