#!/usr/bin/env bash
# Replace-pattern power-cut test: only the board's hub port (hub 0-1.4 port 3, checked: DAPLink) is switched.
set -u
cd "$(dirname "$0")"
S=<repo>/samples/storage_sd_replace/host/replace_capture.py
echo "start $(date -u +%H:%M:%S)" > replace-test.status
perl -e 'alarm 2700; exec @ARGV' python3 $S --port /dev/cu.usbmodem14302 --cycles 100 \
  --cut-cmd "uhubctl -l 0-1.4 -p 3 -a off -f" --restore-cmd "uhubctl -l 0-1.4 -p 3 -a on" \
  --off-seconds 3 --min-run 0.5 --max-run 4 --boot-timeout 25 --seed 31 \
  --log replace-console.log --out replace-results.json > replace-stdout.log 2>&1
echo "done $(date -u +%H:%M:%S) exit=$?" >> replace-test.status
