#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Unit tests of the judging helpers of run_rehearsal.py, on console logs built here.

They need no simulator image: each case writes the lines that the append or replace image prints,
with the line checksum of the host scripts, and checks what the runner's judgement does with them.
"""
import os
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, "..", "..", "..", "samples", "storage_sd_interrupt", "host"))
import run_rehearsal  # noqa: E402
from interrupt_capture import checksum  # noqa: E402


def line(text):
    return f"{text} *{checksum(text):02X}"


def append_run(boot, acks, recover_last_valid=None, torn=0):
    lines = ["sd_storage_start: 0", line(f"BOOT {boot}")]
    if recover_last_valid is not None:
        lines.append(line(f"RECOVER run={boot - 1} present=1 last_valid={recover_last_valid} "
                          f"torn={torn} damaged=0"))
    lines += [line(f"ACK {seq}") for seq in range(1, acks + 1)]
    return lines


def replace_run(boot, gens, cfg=None, cmp="none", first=1):
    lines = ["sd_storage_start: 0", line(f"BOOT {boot}")]
    if cfg is not None:
        state, gen = cfg
        lines.append(line(f"CLASS run={boot - 1} cfg={state} cfg_gen={gen} cmp={cmp} "
                          f"cmp_gen={gen if cmp != 'none' else 0}"))
    lines += [line(f"GEN {gen}") for gen in range(first, first + gens)]
    return lines


class AppendJudgement(unittest.TestCase):
    def test_recovery_of_every_acknowledged_record_is_accepted(self):
        log = append_run(1, 40) + append_run(2, 5, recover_last_valid=40)
        self.assertEqual(run_rehearsal.judge_append(log, restarts=1)["judged"], 1)

    def test_recovery_of_more_than_was_acknowledged_is_accepted(self):
        # The console lags the writes: a record can be on the medium before its ACK reached us.
        log = append_run(1, 40) + append_run(2, 5, recover_last_valid=41, torn=7)
        self.assertEqual(run_rehearsal.judge_append(log, restarts=1)["judged"], 1)

    def test_a_lost_acknowledged_record_is_rejected(self):
        log = append_run(1, 40) + append_run(2, 5, recover_last_valid=39)
        with self.assertRaisesRegex(AssertionError, "acknowledged"):
            run_rehearsal.judge_append(log, restarts=1)

    def test_a_damaged_record_is_rejected(self):
        log = append_run(1, 40) + [
            "sd_storage_start: 0", line("BOOT 2"),
            line("RECOVER run=1 present=1 last_valid=40 torn=32 damaged=1")]
        with self.assertRaisesRegex(AssertionError, "damaged"):
            run_rehearsal.judge_append(log, restarts=1)

    def test_a_garbled_ack_does_not_move_the_count(self):
        # A line cut short is dropped by the host parser; 41 must not count as acknowledged.
        log = append_run(1, 40) + ["ACK 41 *0"] + append_run(2, 5, recover_last_valid=40)
        self.assertEqual(run_rehearsal.judge_append(log, restarts=1)["judged"], 1)

    def test_a_restart_that_was_not_judged_is_rejected(self):
        # The second run never reported: fewer judged restarts than expected must not pass.
        log = append_run(1, 40) + append_run(2, 5, recover_last_valid=40)
        with self.assertRaisesRegex(AssertionError, "judged"):
            run_rehearsal.judge_append(log, restarts=2)

    def test_a_report_about_a_run_that_was_not_seen_is_rejected(self):
        log = append_run(1, 40) + append_run(3, 5, recover_last_valid=40)
        with self.assertRaisesRegex(AssertionError, "judged"):
            run_rehearsal.judge_append(log, restarts=1)

    def test_acks_are_counted_for_the_run_that_has_to_stop(self):
        state = {}
        for text in append_run(1, 200):
            run_rehearsal.APPEND.parse_line(text, state)
        self.assertEqual(run_rehearsal.progress(state, "append"), 200)


class ReplaceJudgement(unittest.TestCase):
    def test_a_valid_cfg_at_the_last_generation_is_accepted(self):
        log = replace_run(1, 40) + replace_run(2, 3, cfg=("valid", 40), first=41)
        result = run_rehearsal.judge_replace(log, restarts=1)
        self.assertEqual((result["judged"], result["cmp_valid"]), (1, 0))

    def test_a_cfg_that_is_none_after_a_restart_is_rejected(self):
        log = replace_run(1, 40) + replace_run(2, 3, cfg=("none", 0), first=41)
        with self.assertRaisesRegex(AssertionError, "cfg"):
            run_rehearsal.judge_replace(log, restarts=1)

    def test_a_cfg_older_than_a_printed_generation_is_rejected(self):
        log = replace_run(1, 40) + replace_run(2, 3, cfg=("valid", 38), first=41)
        with self.assertRaisesRegex(AssertionError, "cfg"):
            run_rehearsal.judge_replace(log, restarts=1)

    def test_a_damaged_cfg_is_rejected(self):
        log = replace_run(1, 40) + replace_run(2, 3, cfg=("bad", 40), first=41)
        with self.assertRaisesRegex(AssertionError, "cfg"):
            run_rehearsal.judge_replace(log, restarts=1)

    def test_a_leftover_valid_cmp_is_accepted_and_counted(self):
        log = (replace_run(1, 40) + replace_run(2, 3, cfg=("valid", 40), cmp="valid", first=41) +
               replace_run(3, 3, cfg=("valid", 43), first=44))
        result = run_rehearsal.judge_replace(log, restarts=2)
        self.assertEqual((result["judged"], result["cmp_valid"], result["cmp_bad"]), (2, 1, 0))

    def test_a_leftover_incomplete_cmp_is_accepted_and_counted_apart(self):
        log = replace_run(1, 40) + replace_run(2, 3, cfg=("valid", 40), cmp="bad", first=41)
        result = run_rehearsal.judge_replace(log, restarts=1)
        self.assertEqual((result["cmp_valid"], result["cmp_bad"]), (0, 1))

    def test_a_missing_report_is_rejected(self):
        log = replace_run(1, 40) + replace_run(2, 3, first=41)
        with self.assertRaisesRegex(AssertionError, "judged"):
            run_rehearsal.judge_replace(log, restarts=1)


class Console(unittest.TestCase):
    def test_banner_lines_are_not_events(self):
        self.assertEqual(run_rehearsal.status_line("reliance_format: 2", "reliance_format"), 2)
        self.assertEqual(run_rehearsal.status_line("emmc_storage_start: 0", "reliance_format"), None)
        self.assertEqual(run_rehearsal.status_line("sd_storage_start: -3", "sd_storage_start"), -3)

    def test_a_trace_is_summarised_by_its_write_records(self):
        self.assertEqual(run_rehearsal.count_writes([]), 0)
        records = [(3, 0, 0, 0, b""), (1, 8, 1, 0, b"x" * 512), (2, 0, 0, 0, b"")]
        self.assertEqual(run_rehearsal.count_writes(records), 1)


if __name__ == "__main__":
    unittest.main()
