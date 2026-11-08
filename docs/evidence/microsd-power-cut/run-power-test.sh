#!/usr/bin/env bash
# Automatic power-cut test: only the board's hub port (hub 0-1.4 port 3, checked: DAPLink) is switched.
set -u
cd "$(dirname "$0")"
S=<repo>/samples/storage_sd_interrupt/host/interrupt_capture.py
CUT="uhubctl -l 0-1.4 -p 3 -a off -f"
ON="uhubctl -l 0-1.4 -p 3 -a on"
echo "start $(date -u +%H:%M:%S)" > power-test.status
perl -e 'alarm 900; exec @ARGV' python3 $S --port /dev/cu.usbmodem14302 --cycles 30 \
  --cut-cmd "$CUT" --restore-cmd "$ON" --off-seconds 3 --min-run 2 --max-run 6 --boot-timeout 25 \
  --seed 21 --log p1-console.log --out p1-results.json > p1-stdout.log 2>&1
echo "phase1 done $(date -u +%H:%M:%S) exit=$?" >> power-test.status
perl -e 'alarm 700; exec @ARGV' python3 $S --port /dev/cu.usbmodem14302 --cycles 3 \
  --cut-cmd "$CUT" --restore-cmd "$ON" --off-seconds 3 --min-run 60 --max-run 90 --boot-timeout 25 \
  --seed 22 --log p2-console.log --out p2-results.json > p2-stdout.log 2>&1
echo "phase2 done $(date -u +%H:%M:%S) exit=$?" >> power-test.status
