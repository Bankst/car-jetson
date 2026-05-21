#!/usr/bin/env bash
# Timestamp each line from a serial console at the host.
# Two clocks per line:
#   [+SSSS.mmm]  monotonic seconds since script start (boot-time delta)
#   [HH:MM:SS.mmm] wall-clock (for cross-ref with other logs)
#
# Usage: ./uart-tslog.sh [-d DEV] [-b BAUD] [-o LOGFILE]
# Defaults: /dev/ttyUSB0 @ 115200, tee to ./uart-$(date +%Y%m%d-%H%M%S).log
#
# Requires: stty, python3 (for sub-ms timing). No picocom/minicom needed.
# Ctrl-C to stop.

set -euo pipefail

DEV=/dev/ttyUSB0
BAUD=115200
LOG=""

while getopts "d:b:o:h" opt; do
  case $opt in
    d) DEV=$OPTARG ;;
    b) BAUD=$OPTARG ;;
    o) LOG=$OPTARG ;;
    h) sed -n '2,12p' "$0"; exit 0 ;;
    *) exit 2 ;;
  esac
done

[[ -n $LOG ]] || LOG="uart-$(date +%Y%m%d-%H%M%S).log"
[[ -r $DEV && -w $DEV ]] || { echo "cannot access $DEV (try sudo or add user to dialout)"; exit 1; }

# Raw 8N1, no flow control, no echo, no input processing.
stty -F "$DEV" "$BAUD" cs8 -cstopb -parenb -crtscts \
  raw -echo -echoe -echok -echoctl -echoke \
  -ixon -ixoff -icrnl -inlcr -igncr \
  -opost -onlcr min 1 time 0

echo "logging $DEV @ $BAUD -> $LOG (Ctrl-C to stop)"
echo "# t0=$(date -Ins)" | tee "$LOG"

# Python reader: per-line monotonic+wall stamp, line-buffered tee.
exec python3 -u - "$DEV" "$LOG" <<'PY'
import os, sys, time, datetime
dev, log = sys.argv[1], sys.argv[2]
t0 = time.monotonic()
fd = os.open(dev, os.O_RDONLY | os.O_NOCTTY)
buf = bytearray()
with open(log, "ab", buffering=0) as lf:
    def emit(line: bytes):
        dt = time.monotonic() - t0
        wall = datetime.datetime.now().strftime("%H:%M:%S.") + f"{int((time.time()%1)*1000):03d}"
        stamp = f"[+{dt:9.3f}] [{wall}] ".encode()
        out = stamp + line + b"\n"
        os.write(1, out)
        lf.write(out)
    while True:
        chunk = os.read(fd, 4096)
        if not chunk:
            continue
        buf.extend(chunk)
        while True:
            i = buf.find(b"\n")
            if i < 0: break
            line = bytes(buf[:i]).rstrip(b"\r")
            del buf[:i+1]
            emit(line)
PY
