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
    MISMATCH name sha: why               the checkout is not what the manifest pins

A product declares a difference in its manifest:

    userdata:
      crux-override: "reason"

The workspace side is the project's HEAD, because that is what gets built. HEAD
must be what west update fetched for the project (manifest-rev), and a SHA or tag
in the manifest must be that commit too, so a moved checkout or a skipped or failed
update is a MISMATCH that no override excuses. A branch name in the manifest cannot
be checked this way: west keeps no ref for it, and a local branch of that name
proves nothing.

The contract side is compared by commit, resolved by git in the project checkout,
so a tag and its commit are equal. If a contract revision cannot be resolved the
contract cannot be read, and the command fails instead of reporting OK.

The comparison functions need neither west nor git. Only the command class does.
"""
import re
import subprocess
from pathlib import Path

try:
    from west.commands import WestCommand
except ImportError:  # keeps the pure functions importable and testable without west
    WestCommand = object

CRUX_PROJECT = "crux-pilot"
OVERRIDE_KEY = "crux-override"
SHORT = 12
MANIFEST_REV = "refs/heads/manifest-rev"
MAYBE_SHA = re.compile(r"[0-9a-fA-F]{4,40}")


class ContractError(Exception):
    """The contract or a project revision could not be read or resolved."""


class StaleCheckout(Exception):
    """A project's checkout is not what its manifest pins, whatever the contract says."""

    def __init__(self, head, reason):
        super().__init__(reason)
        self.head = head


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
    commit checked out for that revision and "contract" for the one Crux pins. A
    "workspace" resolve may raise StaleCheckout, which becomes a MISMATCH whatever
    the contract or a declared override says. Raises ContractError, naming every
    revision that could not be resolved, so that nothing is reported OK on guesswork.
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
        try:
            have = commit(name, entry["revision"], "workspace")
        except StaleCheckout as stale:
            results.append({"status": "MISMATCH", "name": name, "sha": stale.head, "stale": str(stale)})
            continue
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
    if "stale" in result:
        return f"MISMATCH {name} {sha}: {result['stale']}"
    contract = result["contract_sha"][:SHORT]
    if status == "OVERRIDE":
        return f"OVERRIDE {name} {sha} (contract {contract}): {result['reason']}"
    return f"MISMATCH {name} {sha} (contract {contract})"


def count_mismatches(results):
    return sum(1 for result in results if result["status"] == "MISMATCH")


def git(path, *arguments):
    return subprocess.run(["git", "-C", str(path), *arguments], capture_output=True, text=True)


def rev_parse(path, revision):
    """Return the commit a revision names inside a checkout, or None."""
    result = git(path, "rev-parse", "--verify", "--quiet", f"{revision}^{{commit}}")
    return result.stdout.strip() if result.returncode == 0 else None


def contract_commit(path, revision):
    """Resolve a revision that Crux or Zephyr pins, or raise ValueError.

    This never uses manifest-rev: that would compare the workspace with itself.
    """
    commit = rev_parse(path, revision)
    if commit is None:
        raise ValueError(f"not found in {path}")
    return commit


def fetched_commit(path, revision):
    """Return what west update fetched for a manifest revision, or raise ValueError saying why not."""
    fetched = rev_parse(path, MANIFEST_REV)
    if fetched is None:
        raise ValueError("no manifest-rev, so west update has not run in this checkout")
    named = rev_parse(path, f"refs/tags/{revision}")
    if named is None and MAYBE_SHA.fullmatch(revision):
        named = rev_parse(path, revision)
        if named is None:
            raise ValueError(f"the manifest names {revision}, which was never fetched. Run west update")
    if named is not None and named != fetched:
        raise ValueError(f"the manifest names {revision}, but west fetched {fetched[:SHORT]}. Run west update")
    return fetched


def checkout_commit(path, revision):
    """Return a project's HEAD. Raise StaleCheckout if it is not what the manifest pins."""
    head = rev_parse(path, "HEAD")
    if head is None:
        raise ValueError(f"no checkout in {path}")
    try:
        fetched = fetched_commit(path, revision)
    except ValueError as error:
        raise StaleCheckout(head, str(error))
    if head != fetched:
        raise StaleCheckout(
            head, f"HEAD differs from manifest-rev {fetched[:SHORT]}, what west fetched. Run west update")
    return head


def parse_projects(text, source):
    import yaml  # west depends on it. Imported late so the pure functions need nothing.

    try:
        data = yaml.safe_load(text)
        projects = data["manifest"]["projects"]
    except (yaml.YAMLError, KeyError, TypeError) as error:
        raise ContractError(f"cannot read the projects of {source}: {error}")
    return [entry for entry in projects if isinstance(entry, dict) and "name" in entry]


def show_manifest(path, commit):
    """Return west.yml of a project at a commit, or raise ContractError."""
    result = git(path, "show", f"{commit}:west.yml")
    if result.returncode:
        raise ContractError(f"cannot read the Crux contract: west.yml is not in {path} at {commit}")
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
            try:
                commit = fetched_commit(crux.abspath, str(crux.revision))
            except ValueError as error:
                raise ContractError(f"cannot read the Crux contract: {CRUX_PROJECT}: {error}")
            text = show_manifest(crux.abspath, commit)
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
        try:
            commit = contract_commit(zephyr.abspath, str(zephyr_pin))
        except ValueError:
            raise ContractError(
                f"cannot read the Crux contract: zephyr revision {zephyr_pin} is not in {zephyr.abspath}")
        zephyr_text = show_manifest(zephyr.abspath, commit)
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
            read = checkout_commit if role == "workspace" else contract_commit
            return read(projects[name].abspath, revision)

        try:
            results = compare_pins(contract, entries, resolve, skip=("manifest", CRUX_PROJECT))
        except ContractError as error:
            self.die(f"cannot compare with the Crux contract:\n{error}")
        for result in results:
            self.inf(format_result(result))
        mismatches = count_mismatches(results)
        if mismatches:
            self.die(f"{mismatches} project(s) differ from the Crux contract without a "
                     f"declared {OVERRIDE_KEY} reason, or from what west fetched")
