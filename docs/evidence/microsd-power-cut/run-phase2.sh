#!/usr/bin/env bash
set -u
cd "$(dirname "$0")"
S=<repo>/samples/storage_sd_interrupt/host/interrupt_capture.py
echo "phase2 start $(date -u +%H:%M:%S)" > phase2.status
perl -e 'alarm 700; exec @ARGV' python3 $S --port /dev/cu.usbmodem14302 --cycles 3 \
  --cut-cmd "uhubctl -l 0-1.4 -p 3 -a off -f" --restore-cmd "uhubctl -l 0-1.4 -p 3 -a on" \
  --off-seconds 3 --min-run 60 --max-run 90 --boot-timeout 25 --seed 22 \
  --log p2-console.log --out p2-results.json > p2-stdout.log 2>&1
echo "phase2 done $(date -u +%H:%M:%S) exit=$?" >> phase2.status
