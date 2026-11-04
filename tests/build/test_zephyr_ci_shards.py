"""Check that dividing the Zephyr workflow into shards cannot silently drop a check.

A step whose shard condition matches nothing is skipped, and a skipped step leaves the
run green, so these rules read the workflow text. Only the standard library is used
because `host.yml` runs this file without PyYAML.
"""
from pathlib import Path
import re
import unittest

REPO = Path(__file__).resolve().parents[2]
ZEPHYR = REPO / ".github/workflows/zephyr.yml"
HOST = REPO / ".github/workflows/host.yml"
DOCS_ONLY = ["docs/**", "**.md"]
SHARD_CONDITION = re.compile(r"^matrix\.shard == '([a-z-]+)'$")


def job_text(workflow, job):
    """Return the lines of one job, from its key to the next job or end of file."""
    lines = workflow.splitlines()
    start = lines.index(f"  {job}:")
    end = next((i for i in range(start + 1, len(lines)) if re.match(r"  \S", lines[i])), len(lines))
    return lines[start:end]


def steps(job_lines):
    """Split a job into steps: {'title', 'condition', 'text'}, in workflow order."""
    result = []
    for line in job_lines:
        if line.startswith("      - "):
            result.append({"title": line[8:].strip(), "condition": None, "text": line + "\n"})
        elif result and line.startswith("      "):
            result[-1]["text"] += line + "\n"
            match = re.match(r"\s+if: (.+)$", line)
            if match and line.startswith("        if:"):
                result[-1]["condition"] = match.group(1).strip()
    return result


def paths_ignore(workflow, event):
    """Return the paths-ignore patterns of a trigger event, or None without a filter."""
    lines = workflow.splitlines()
    start = lines.index(f"  {event}:")
    patterns = None
    for line in lines[start + 1:]:
        if not line.startswith("    "):
            break
        if line.strip() == "paths-ignore:":
            patterns = []
        elif patterns is not None and line.strip().startswith("- "):
            patterns.append(line.strip()[2:].strip("\"'"))
    return patterns


def is_check(step):
    """A check is any step that runs repository code after the common set-up."""
    text = step["text"]
    return "scripts/" in text or "python3 " in text or "west " in text


class ZephyrShardsTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.workflow = ZEPHYR.read_text()
        cls.job = job_text(cls.workflow, "shard")
        cls.steps = steps(cls.job)
        matrix = next(line for line in cls.job if line.strip().startswith("shard: ["))
        cls.shards = re.findall(r"[a-z-]+", matrix.split("[", 1)[1])

    def shard_of(self, fragment):
        matches = [s for s in self.steps if fragment in s["text"]]
        self.assertEqual(1, len(matches), f"{fragment} must run in exactly one step")
        condition = SHARD_CONDITION.match(matches[0]["condition"] or "")
        self.assertIsNotNone(condition, f"{fragment} has no shard condition")
        return condition.group(1)

    def test_every_check_runs_in_a_named_shard(self):
        for step in self.steps:
            if not is_check(step) or "scripts/bootstrap-zephyr.sh" in step["text"]:
                continue
            with self.subTest(step=step["title"]):
                condition = SHARD_CONDITION.match(step["condition"] or "")
                self.assertIsNotNone(condition, "check has no shard condition")
                self.assertIn(condition.group(1), self.shards)

    def test_every_shard_runs_a_check(self):
        used = set()
        for step in self.steps:
            condition = SHARD_CONDITION.match(step["condition"] or "")
            if condition:
                used.add(condition.group(1))
        self.assertEqual(set(self.shards), used)

    def test_networking_checks_share_their_test_library(self):
        # The fixture step reads the Picolibc checkout that the networking step fetches.
        self.assertEqual(self.shard_of("scripts/bootstrap-networking-tests.sh"),
                         self.shard_of("scripts/test-networking-physical.sh"))

    def test_one_shard_failing_does_not_cancel_the_others(self):
        self.assertIn("      fail-fast: false", self.job)

    def test_each_shard_uploads_under_its_own_name(self):
        # upload-artifact@v4 refuses a name that another job in the run already used.
        self.assertIn("name: zephyr-${{ matrix.shard }}-${{ github.run_id }}", self.workflow)

    def test_summary_job_keeps_the_zephyr_check_and_needs_every_shard(self):
        summary = "\n".join(job_text(self.workflow, "zephyr"))
        self.assertIn("needs: shard", summary)
        self.assertIn("if: always()", summary)
        self.assertIn("needs.shard.result", summary)
        # Only "success" may pass: failed, cancelled and skipped shards must all fail it.
        self.assertIn('test "$SHARD_RESULT" = success', summary)

    def test_only_documentation_skips_the_workflow(self):
        self.assertEqual(DOCS_ONLY, paths_ignore(self.workflow, "push"))
        self.assertEqual(DOCS_ONLY, paths_ignore(self.workflow, "pull_request"))

    def step_using(self, action):
        matches = [s for s in self.steps if f"uses: {action}" in s["text"]]
        self.assertEqual(1, len(matches), f"{action} must be used by exactly one step")
        return matches[0]

    def test_host_packages_are_restored_before_they_are_installed(self):
        # Slow mirrors cost one job in six up to ten minutes, which sets a run's length.
        titles = [s["title"] for s in self.steps]
        install = next(s for s in self.steps if "apt-get install" in s["text"])
        self.assertIn("Dir::Cache::archives=", install["text"])
        restore = self.step_using("actions/cache/restore@")
        save = self.step_using("actions/cache/save@")
        self.assertLess(titles.index(restore["title"]), titles.index(install["title"]))
        self.assertLess(titles.index(install["title"]), titles.index(save["title"]))

    def test_restored_and_saved_packages_use_one_path_and_key(self):
        field = re.compile(r"^\s+(path|key): (.+)$", re.MULTILINE)
        self.assertEqual(field.findall(self.step_using("actions/cache/restore@")["text"]),
                         field.findall(self.step_using("actions/cache/save@")["text"]))

    def test_cache_paths_are_accepted_by_the_cache_action(self):
        # actions/cache only warns about a path with "..", then stores nothing: every run
        # stays cold while the workflow stays green.
        for step in (s for s in self.steps if "actions/cache" in s["text"]):
            with self.subTest(step=step["title"]):
                path = re.search(r"^\s+path: (.+)$", step["text"], re.MULTILINE).group(1)
                self.assertNotIn("..", path)

    def test_host_workflow_still_runs_for_documentation(self):
        # tests/build reads documentation, so one workflow must keep running on such changes.
        host = HOST.read_text()
        self.assertNotIn("paths-ignore", host)
        self.assertIn("unittest discover -s tests/build", host)


if __name__ == "__main__":
    unittest.main()
