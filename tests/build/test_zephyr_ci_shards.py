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
# Steps that run in every shard: they prepare the runner or collect what the shard produced.
SET_UP_STEPS = {
    "name: List host packages from the signed index",
    "name: Install Zephyr host packages",
    "name: Install pinned Python tools",
    "name: Fetch pinned Zephyr workspace and install SDK",
    "name: Collect Twister evidence",
}
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


def misplaced_steps(job_steps, shards):
    """Return the steps that run a command but do not name one of the shards.

    Every step with a `run:` is a check unless it is a named set-up step, whatever
    command it runs: a check that names no shard runs in all of them, and one that
    names a shard the matrix lacks is skipped while the run stays green.
    """
    result = []
    for step in job_steps:
        if "\n        run:" not in step["text"] or step["title"] in SET_UP_STEPS:
            continue
        condition = SHARD_CONDITION.match(step["condition"] or "")
        if condition is None or condition.group(1) not in shards:
            result.append(step)
    return result


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
        for step in misplaced_steps(self.steps, self.shards):
            with self.subTest(step=step["title"]):
                self.fail("step runs a command but names no shard of the matrix")

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
        install = self.install_step()
        self.assertIn("Dir::Cache::archives=", install["text"])
        restore = self.step_using("actions/cache/restore@")
        save = self.step_using("actions/cache/save@")
        self.assertLess(titles.index(restore["title"]), titles.index(install["title"]))
        self.assertLess(titles.index(install["title"]), titles.index(save["title"]))

    def install_step(self):
        matches = [s for s in self.steps if re.search(r"apt-get install -y -o", s["text"])]
        self.assertEqual(1, len(matches), "one step must install from the package cache")
        return matches[0]

    def test_restored_packages_are_verified_before_apt_reads_them(self):
        # apt accepts a cached file by name and size alone, so a poisoned cache would install.
        text = self.install_step()["text"]
        self.assertIn("scripts/verify-apt-cache.py", text)
        self.assertLess(text.index("scripts/verify-apt-cache.py"), text.index("apt-get install"))

    def test_package_list_comes_from_the_index_with_a_strong_hash(self):
        # --print-uris reports MD5 unless a hash is forced.
        listing = next(s for s in self.steps if "--print-uris" in s["text"])
        self.assertIn("Acquire::ForceHash=SHA256", listing["text"])
        self.assertIn("apt-get update", listing["text"])
        self.assertIn("$CI_HOST_PACKAGES", listing["text"])
        self.assertIn("$CI_HOST_PACKAGES", self.install_step()["text"])

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


SYNTHETIC_JOB = """  shard:
    steps:
      - name: Install pinned Python tools
        run: pip install west
{steps}
"""


def synthetic_steps(*blocks):
    """Build the steps of a small job: the named set-up step plus the given step blocks."""
    return steps(SYNTHETIC_JOB.format(steps="\n".join(blocks)).splitlines())


def step_block(run, condition=None):
    lines = ["      - name: A check"]
    if condition:
        lines.append(f"        if: {condition}")
    lines.append(f"        run: {run}")
    return "\n".join(lines)


class MisplacedStepsTest(unittest.TestCase):
    """The rule must see every step that runs a command, whatever command it runs."""

    shards = ["storage", "io"]

    def titles(self, *blocks):
        return [s["title"] for s in misplaced_steps(synthetic_steps(*blocks), self.shards)]

    def test_a_step_in_a_named_shard_is_accepted(self):
        self.assertEqual([], self.titles(step_block("tests/ssh/cli/test-compile.sh", "matrix.shard == 'io'")))

    def test_a_step_without_a_condition_is_reported_even_for_an_unfamiliar_command(self):
        for run in ("tests/ssh/cli/test-compile.sh", "make check", "./run.sh", "cmake --build build"):
            with self.subTest(run=run):
                self.assertEqual(["name: A check"], self.titles(step_block(run)))

    def test_a_shard_name_that_is_not_in_the_matrix_is_reported(self):
        self.assertEqual(["name: A check"], self.titles(step_block("make check", "matrix.shard == 'oi'")))

    def test_a_condition_that_is_not_a_shard_is_reported(self):
        self.assertEqual(["name: A check"], self.titles(step_block("make check", "always()")))

    def test_the_named_set_up_steps_need_no_condition(self):
        self.assertEqual([], misplaced_steps(synthetic_steps(), self.shards))


if __name__ == "__main__":
    unittest.main()
