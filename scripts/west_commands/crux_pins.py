"""West command that reports where a workspace differs from the Crux pin contract.

west accepts a product manifest that picks another Zephyr, or another module
revision, without a word. This command makes such a difference visible. The
contract is the west.yml of crux-pilot at the revision the workspace pins, plus
the west.yml of Zephyr at the revision that Crux pins. Explicit Crux entries win
over Zephyr's.

Every active project gets one line:

    OK name sha                          same commit as the contract
    OVERRIDE name sha (contract sha): why   differs, declared with userdata
    PRODUCT name sha                     not part of the contract
    MISMATCH name sha (contract sha)     differs without a declared reason

A product declares a difference in its manifest:

    userdata:
      crux-override: "reason"

Revisions are compared as commits resolved by git in the project checkout, so a
tag and its commit are equal. If a revision cannot be resolved the contract
cannot be read, and the command fails instead of reporting OK.

The comparison functions need neither west nor git. Only the command class does.
"""
import subprocess
from pathlib import Path

try:
    from west.commands import WestCommand
except ImportError:  # keeps the pure functions importable and testable without west
    WestCommand = object

CRUX_PROJECT = "crux-pilot"
OVERRIDE_KEY = "crux-override"
SHORT = 12


class ContractError(Exception):
    """The contract or a project revision could not be read or resolved."""


def merge_contract(crux_projects, zephyr_projects):
    """Return {name: revision}. Explicit Crux entries win over Zephyr's."""
    contract = {}
    for entries in (zephyr_projects, crux_projects):
        for entry in entries:
            if entry.get("revision"):
                contract[entry["name"]] = str(entry["revision"])
    return contract


def override_reason(userdata):
    """Return the declared override reason, or None when there is none."""
    if not isinstance(userdata, dict):
        return None
    reason = userdata.get(OVERRIDE_KEY)
    if isinstance(reason, str) and reason.strip():
        return reason.strip()
    return None


def compare_pins(contract, projects, resolve, skip=()):
    """Compare projects with the contract and return one result dict per active project.

    contract maps a project name to a revision string. projects is a list of
    dicts with name, revision, active and userdata. resolve(name, revision, role)
    returns a full commit id or raises ValueError. role is "workspace" for the
    revision the workspace names and "contract" for the one Crux pins. Raises ContractError, naming
    every revision that could not be resolved, so that nothing is reported OK on
    guesswork.
    """
    results = []
    problems = []

    def commit(name, revision, role):
        try:
            return resolve(name, revision, role)
        except ValueError as error:
            problems.append(f"{name}: cannot resolve the {role} revision {revision}: {error}")
            return None

    for entry in projects:
        name = entry["name"]
        if not entry.get("active", True) or name in skip:
            continue
        have = commit(name, entry["revision"], "workspace")
        want_revision = contract.get(name)
        if want_revision is None:
            results.append({"status": "PRODUCT", "name": name, "sha": have})
            continue
        want = commit(name, want_revision, "contract")
        if have is None or want is None:
            continue
        result = {"name": name, "sha": have, "contract_sha": want}
        reason = override_reason(entry.get("userdata"))
        if have == want:
            result["status"] = "OK"
        elif reason:
            result.update(status="OVERRIDE", reason=reason)
        else:
            result["status"] = "MISMATCH"
        results.append(result)
    if problems:
        raise ContractError("\n".join(problems))
    return results


def format_result(result):
    sha = result["sha"][:SHORT]
    status, name = result["status"], result["name"]
    if status == "PRODUCT":
        return f"PRODUCT {name} {sha}"
    if status == "OK":
        return f"OK {name} {sha}"
    contract = result["contract_sha"][:SHORT]
    if status == "OVERRIDE":
        return f"OVERRIDE {name} {sha} (contract {contract}): {result['reason']}"
    return f"MISMATCH {name} {sha} (contract {contract})"


def count_mismatches(results):
    return sum(1 for result in results if result["status"] == "MISMATCH")


def git(path, *arguments):
    return subprocess.run(["git", "-C", str(path), *arguments], capture_output=True, text=True)


def resolve_commit(path, revision, role="contract"):
    """Resolve a tag, branch or commit to a commit id inside a project checkout.

    west leaves no local branch for a branch revision. It records what it fetched
    in manifest-rev, so that is the fallback for the workspace side only. The
    contract side never uses it: that would compare the workspace with itself.
    """
    candidates = [revision] + (["manifest-rev"] if role == "workspace" else [])
    for candidate in candidates:
        result = git(path, "rev-parse", "--verify", "--quiet", f"{candidate}^{{commit}}")
        if result.returncode == 0 and result.stdout.strip():
            return result.stdout.strip()
    raise ValueError(f"not found in {path}")


def parse_projects(text, source):
    import yaml  # west depends on it. Imported late so the pure functions need nothing.

    try:
        data = yaml.safe_load(text)
        projects = data["manifest"]["projects"]
    except (yaml.YAMLError, KeyError, TypeError) as error:
        raise ContractError(f"cannot read the projects of {source}: {error}")
    return [entry for entry in projects if isinstance(entry, dict) and "name" in entry]


def show_file(path, revision, name, label, role):
    """Return a file of a project at a revision, or raise ContractError."""
    try:
        commit = resolve_commit(path, revision, role)
    except ValueError:
        raise ContractError(
            f"cannot read the Crux contract: {label} revision {revision} is not in {path}")
    result = git(path, "show", f"{commit}:{name}")
    if result.returncode:
        raise ContractError(
            f"cannot read the Crux contract: {name} is not in {path} at {commit}")
    return result.stdout


class CruxPins(WestCommand):
    def __init__(self):
        super().__init__(
            "crux-pins",
            "compare the workspace pins with the versions Crux was verified on",
            "Print OK, OVERRIDE, PRODUCT or MISMATCH for each active project and fail on "
            "a MISMATCH. A product declares a difference with "
            f"userdata: {{{OVERRIDE_KEY}: <reason>}}.")

    def do_add_parser(self, parser_adder):
        return parser_adder.add_parser(
            self.name, help=self.help, description=self.description)

    def read_contract(self, projects):
        crux = projects.get(CRUX_PROJECT)
        if crux is not None:
            text = show_file(crux.abspath, str(crux.revision), "west.yml", "crux-pilot", "workspace")
            source = f"{CRUX_PROJECT} {crux.revision}"
        else:
            # crux-pilot is itself the manifest repository: use its working tree.
            text = Path(self.manifest.abspath).read_text()
            source = "the manifest repository"
        crux_projects = parse_projects(text, source)
        zephyr_pin = next((entry.get("revision") for entry in crux_projects
                           if entry["name"] == "zephyr"), None)
        zephyr = projects.get("zephyr")
        if not zephyr_pin or zephyr is None:
            raise ContractError("cannot read the Crux contract: no zephyr project is pinned")
        zephyr_text = show_file(zephyr.abspath, str(zephyr_pin), "west.yml", "zephyr", "contract")
        return merge_contract(crux_projects, parse_projects(zephyr_text, "zephyr west.yml"))

    def do_run(self, args, unknown):
        projects = {project.name: project for project in self.manifest.projects}
        try:
            contract = self.read_contract(projects)
        except ContractError as error:
            self.die(str(error))
        entries = [{"name": name, "revision": str(project.revision),
                    "active": self.manifest.is_active(project), "userdata": project.userdata}
                   for name, project in projects.items()]

        def resolve(name, revision, role):
            return resolve_commit(projects[name].abspath, revision, role)

        try:
            results = compare_pins(contract, entries, resolve, skip=("manifest", CRUX_PROJECT))
        except ContractError as error:
            self.die(f"cannot compare with the Crux contract:\n{error}")
        for result in results:
            self.inf(format_result(result))
        mismatches = count_mismatches(results)
        if mismatches:
            self.die(f"{mismatches} project(s) differ from the Crux contract without a "
                     f"declared {OVERRIDE_KEY} reason")
