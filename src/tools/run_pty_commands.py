#!/usr/bin/env python3
"""Drive a Unix console process through marker-synchronized PTY commands."""

import argparse
import errno
import fcntl
import os
from pathlib import Path
import select
import signal
import struct
import subprocess
import sys
import termios
import time
from typing import Optional


class TerminationRequested(Exception):
  """Raised when the driver is asked to stop by its parent harness."""


def handle_termination(signum: int, frame: object) -> None:
  del frame
  raise TerminationRequested(f"received signal {signum}")


def parse_args() -> argparse.Namespace:
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument("--log", required=True, type=Path,
                      help="Raw combined stdout and stderr log")
  parser.add_argument("--timeout", type=float, default=60.0,
                      help="Timeout in seconds for each marker and shutdown")
  parser.add_argument("--ready-marker", required=True,
                      help="Marker emitted when the first runtime is ready")
  parser.add_argument("--reload-command", required=True,
                      help="Console command that reloads the runtime")
  parser.add_argument("--reload-marker", required=True,
                      help="Marker emitted after the reload command")
  parser.add_argument("--quit-command", default="quit",
                      help="Console command that exits cleanly")
  parser.add_argument("--ready-file", type=Path,
                      help="Write this file after the reload marker")
  parser.add_argument("--release-file", type=Path,
                      help="Wait for this file before sending quit")
  parser.add_argument("--release-timeout", type=float, default=180.0,
                      help="Timeout in seconds for --release-file")
  parser.add_argument("--release-command", action="append", default=[],
                      help="Console command sent after --release-file appears")
  parser.add_argument("--release-marker",
                      help="Marker awaited after --release-command")
  parser.add_argument("--post-release-command", action="append", default=[],
                      help="Console command sent after --release-marker")
  parser.add_argument("--post-release-marker",
                      help="Marker awaited after --post-release-command")
  parser.add_argument("command", nargs=argparse.REMAINDER,
                      help="Process and arguments, after --")
  args = parser.parse_args()
  if args.command and args.command[0] == "--":
    args.command = args.command[1:]
  if not args.command:
    parser.error("a process command is required after --")
  if bool(args.ready_file) != bool(args.release_file):
    parser.error("--ready-file and --release-file must be supplied together")
  if (args.release_command or args.release_marker or
      args.post_release_command or args.post_release_marker) and not args.release_file:
    parser.error("release commands and markers require --release-file")
  if args.release_marker and not args.release_command:
    parser.error("--release-marker requires --release-command")
  if args.post_release_command and not args.release_marker:
    parser.error("--post-release-command requires --release-marker")
  if args.post_release_marker and not args.post_release_command:
    parser.error("--post-release-marker requires --post-release-command")
  return args


class PtyProcess:
  def __init__(self, command: list[str], log_path: Path):
    self.command = command
    self.log_path = log_path
    self.master = -1
    self.process: Optional[subprocess.Popen] = None
    self.log = None

  def start(self) -> None:
    self.log_path.parent.mkdir(parents=True, exist_ok=True)
    self.log = self.log_path.open("wb")

    master, slave = os.openpty()
    self.master = master
    fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", 40, 140, 0, 0))

    environment = os.environ.copy()
    environment.setdefault("TERM", "xterm-256color")
    try:
      self.process = subprocess.Popen(
        self.command,
        stdin=slave,
        stdout=slave,
        stderr=slave,
        env=environment,
        start_new_session=True,
      )
    finally:
      os.close(slave)

  def read_chunk(self, wait: float) -> bytes:
    readable, _, _ = select.select([self.master], [], [], wait)
    if not readable:
      return b""
    try:
      chunk = os.read(self.master, 65536)
    except OSError as error:
      if error.errno == errno.EIO:
        return b""
      raise
    if chunk and self.log:
      self.log.write(chunk)
      self.log.flush()
    return chunk

  def wait_for(self, marker: str, timeout: float) -> None:
    assert self.process is not None
    deadline = time.monotonic() + timeout
    text = ""
    while time.monotonic() < deadline:
      chunk = self.read_chunk(min(0.25, max(0.0, deadline - time.monotonic())))
      if chunk:
        text += chunk.decode("utf-8", errors="ignore")
        if marker in text:
          return
        if len(text) > 1048576:
          text = text[-524288:]
      elif self.process.poll() is not None:
        raise RuntimeError(
          f"process exited with {self.process.returncode} before marker: {marker}"
        )
    raise RuntimeError(f"timed out waiting for marker: {marker}")

  def send(self, command: str) -> None:
    os.write(self.master, (command + "\n").encode("utf-8"))

  def wait_for_exit(self, timeout: float) -> int:
    assert self.process is not None
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
      self.read_chunk(0.1)
      return_code = self.process.poll()
      if return_code is not None:
        while self.read_chunk(0.0):
          pass
        return return_code
    raise RuntimeError("timed out waiting for process shutdown")

  def wait_for_file(self, path: Path, timeout: float) -> None:
    assert self.process is not None
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
      self.read_chunk(0.1)
      if path.is_file():
        return
      if self.process.poll() is not None:
        raise RuntimeError(
          f"process exited with {self.process.returncode} before release file: "
          f"{path}"
        )
    raise RuntimeError(f"timed out waiting for release file: {path}")

  def close(self) -> None:
    if self.process is not None and self.process.poll() is None:
      try:
        os.killpg(self.process.pid, signal.SIGTERM)
        self.process.wait(timeout=3.0)
      except (ProcessLookupError, subprocess.TimeoutExpired):
        try:
          os.killpg(self.process.pid, signal.SIGKILL)
        except ProcessLookupError:
          pass
        self.process.wait()
    if self.master >= 0:
      os.close(self.master)
      self.master = -1
    if self.log:
      self.log.close()
      self.log = None


def main() -> int:
  args = parse_args()
  process = PtyProcess(args.command, args.log)
  signal.signal(signal.SIGINT, handle_termination)
  signal.signal(signal.SIGTERM, handle_termination)
  try:
    process.start()
    process.wait_for(args.ready_marker, args.timeout)
    process.send(args.reload_command)
    process.wait_for(args.reload_marker, args.timeout)
    if args.ready_file and args.release_file:
      args.ready_file.parent.mkdir(parents=True, exist_ok=True)
      args.ready_file.write_text("ready\n", encoding="utf-8")
      process.wait_for_file(args.release_file, args.release_timeout)
      for command in args.release_command:
        process.send(command)
      if args.release_marker:
        process.wait_for(args.release_marker, args.timeout)
      for command in args.post_release_command:
        process.send(command)
      if args.post_release_marker:
        process.wait_for(args.post_release_marker, args.timeout)
    process.send(args.quit_command)
    return_code = process.wait_for_exit(args.timeout)
    if return_code != 0:
      raise RuntimeError(f"process exited unsuccessfully: {return_code}")
    print(f"PTY_RUNTIME_PASS exit={return_code} log={args.log}")
    return 0
  except (OSError, RuntimeError, TerminationRequested) as error:
    print(f"PTY_RUNTIME_FAIL: {error}", file=sys.stderr)
    return 1
  finally:
    process.close()


if __name__ == "__main__":
  raise SystemExit(main())
