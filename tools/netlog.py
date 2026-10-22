"""Read the bridge's network debug log (utils/net_log.h) from the PC.

Usage: python tools/netlog.py [host] [--port 23] [--seconds N] [--out FILE]

Connects to the bridge's read-only TCP log port, prefixes each line with the
PC's wall-clock time and prints it (and appends to --out if given). The bridge
sends its buffered history first, then live output. Reconnects if the link
drops, so it can be left running across bridge reboots.
"""
import argparse
import socket
import sys
import time


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("host", nargs="?", default="192.168.0.51")
    ap.add_argument("--port", type=int, default=23)
    ap.add_argument("--seconds", type=float, default=0, help="stop after N seconds (0 = run until Ctrl+C)")
    ap.add_argument("--out", help="also append lines to this file")
    args = ap.parse_args()

    out = open(args.out, "a", encoding="utf-8") if args.out else None
    end = time.time() + args.seconds if args.seconds else None
    pending = b""

    def emit(line):
        text = time.strftime("%H:%M:%S ") + line.rstrip("\r")
        print(text, flush=True)
        if out:
            out.write(text + "\n")
            out.flush()

    while end is None or time.time() < end:
        try:
            sock = socket.create_connection((args.host, args.port), timeout=5)
        except OSError as exc:
            emit("[connect failed: %s - retrying]" % exc)
            time.sleep(2)
            continue
        emit("[connected to %s:%d]" % (args.host, args.port))
        sock.settimeout(1.0)
        try:
            while end is None or time.time() < end:
                try:
                    data = sock.recv(1024)
                except socket.timeout:
                    continue
                if not data:
                    break
                pending += data
                while b"\n" in pending:
                    line, pending = pending.split(b"\n", 1)
                    emit(line.decode("utf-8", "replace"))
        finally:
            sock.close()
        emit("[disconnected]")
        time.sleep(1)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        sys.exit(0)
