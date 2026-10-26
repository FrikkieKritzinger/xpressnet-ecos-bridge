"""Build the development firmware and flash it to the bridge over WiFi.

Usage: python tools/devflash.py [host] [--port 8266] [--no-build]

Builds the `wemos_dev` PlatformIO environment (unless --no-build), uploads
.pio/build/wemos_dev/firmware.bin to the bridge's development updater
(utils/dev_ota - an unauthenticated HTTP /update page that only the dev build
contains), then waits for the bridge to come back and prints the start of its
network log.

The FIRST dev build must be flashed over USB (pull the D1 from the
motherboard); after that the board can stay seated. Always flash the
`wemos_dev` environment: a plain `wemos` build has no updater.
"""
import argparse
import os
import socket
import subprocess
import sys
import time
import urllib.error
import urllib.request
import uuid

ENV = "wemos_dev"


def build(root):
    print("Building %s ..." % ENV)
    result = subprocess.run([sys.executable, "-m", "platformio", "run", "-e", ENV], cwd=root)
    if result.returncode != 0:
        sys.exit("Build failed - nothing uploaded.")


def upload(host, port, path):
    with open(path, "rb") as f:
        data = f.read()
    boundary = uuid.uuid4().hex
    head = (
        "--%s\r\n"
        'Content-Disposition: form-data; name="firmware"; filename="firmware.bin"\r\n'
        "Content-Type: application/octet-stream\r\n\r\n" % boundary
    ).encode()
    tail = ("\r\n--%s--\r\n" % boundary).encode()
    body = head + data + tail
    req = urllib.request.Request(
        "http://%s:%d/update" % (host, port),
        data=body,
        method="POST",
        headers={"Content-Type": "multipart/form-data; boundary=" + boundary},
    )
    print("Uploading %d bytes to %s:%d ..." % (len(data), host, port))
    try:
        with urllib.request.urlopen(req, timeout=120) as resp:
            text = resp.read().decode("utf-8", "replace").strip()
    except (urllib.error.URLError, OSError) as exc:
        sys.exit(
            "Upload failed: %s\nIs the board running a wemos_dev build and on the network? "
            "(The first dev build must go over USB.)" % exc
        )
    print("Bridge replied: %s" % text[:200])
    if "SUCCESS" not in text.upper():
        sys.exit("The bridge did not report success.")


def wait_for_log(host, seconds=60):
    print("Waiting for the bridge to restart ...")
    end = time.time() + seconds
    time.sleep(4)
    while time.time() < end:
        try:
            sock = socket.create_connection((host, 23), timeout=3)
        except OSError:
            time.sleep(1)
            continue
        sock.settimeout(3)
        shown = 0
        try:
            buf = b""
            stop = time.time() + 6
            while time.time() < stop and shown < 40:
                try:
                    chunk = sock.recv(1024)
                except socket.timeout:
                    break
                if not chunk:
                    break
                buf += chunk
                while b"\n" in buf and shown < 40:
                    line, buf = buf.split(b"\n", 1)
                    text = line.decode("utf-8", "replace").rstrip("\r")
                    if text.strip():
                        print("  " + text)
                        shown += 1
        finally:
            sock.close()
        print("Bridge is back.")
        return
    print("The bridge did not come back within %d s - check the board." % seconds)


def main():
    # The bridge log contains arrows (non-cp1252); see tools/netlog.py.
    for stream in (sys.stdout, sys.stderr):
        try:
            stream.reconfigure(encoding="utf-8", errors="replace")
        except (AttributeError, ValueError):
            pass

    ap = argparse.ArgumentParser()
    ap.add_argument("--env", default=None, help="PlatformIO environment (default wemos_dev; wemos_sniff for the XpressNet sniffer)")
    ap.add_argument("host", nargs="?", default="192.168.0.51")
    ap.add_argument("--port", type=int, default=8266)
    ap.add_argument("--no-build", action="store_true")
    args = ap.parse_args()
    global ENV
    if args.env:
        ENV = args.env

    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    if not args.no_build:
        build(root)
    firmware = os.path.join(root, ".pio", "build", ENV, "firmware.bin")
    if not os.path.exists(firmware):
        sys.exit("No firmware at %s - build first." % firmware)
    upload(args.host, args.port, firmware)
    wait_for_log(args.host)


if __name__ == "__main__":
    main()
