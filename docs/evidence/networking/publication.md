# Networking evidence publication

The independent review of PR #38 identified local home/worktree paths in the
first published evidence. The current tree replaces those prefixes with stable
tokens; the socket/netif implementation and qualification claims are unchanged.

- `$REPO`: source worktree root.
- `$WORKSPACE`: parent west workspace root.
- `$HOME`: local user home directory, where present outside the first two roots.

Substitutions use the longest applicable prefix first. They also cover Twister
artifact names that embed the worktree path without its initial slash. They do
not modify test results, failure messages other than path prefixes, numbers,
source revision IDs, line numbers or whitespace. Twelve of the 22 public logs
required replacement. The other ten retain their original bytes.

Manifest schema 2 separates three inventories:

1. `source_sha256`: the unchanged 244 historical input fingerprints at the
   qualified implementation revision, not a claim that every file in a newer
   integrated main tree has the old hash.
2. `private_raw_evidence_sha256` and sizes: 141 original files, retained privately
   in the original archive. Inventory names are normalized for publication;
   their hashes continue to describe the original bytes. The archive hash is
   unchanged. This archive includes local paths and must not be published as a
   sanitized artifact.
3. `published_evidence_sha256` and sizes: the 22 current normalized public logs.
   Each hash is recomputed from the published file. No public log is represented
   as an unchanged raw artifact when its prefix was replaced.

The focused verification compares every public log to prefix substitution of
its original Git blob, checks all public hashes/sizes, checks source fingerprints
against the original revision, and scans this PR's current changed files and
manifest inventory names for local home/worktree identifiers. A separate scan
of all file contents changed by the four original PR commits found no matches
for private-key, GitHub/OpenAI token, AWS access-key or credential-URL signatures.
That is a targeted check, not a guarantee that no possible sensitive information
exists. The known exposed local paths remain in earlier public Git snapshots;
this correction does not claim to revoke access to pushed history. No history
rewrite or force push was performed.

The cleanup adds no dependency-check exception and does not qualify additional
networking behavior. Native `close(int)` remains the explicitly documented,
pinned declaration-only exception for Issue #24 and future Zephyr upgrades.
The native TIME_WAIT/resource and pending physical-acceptance limits remain in
the consumer contract. Final integration checks and independent re-review are
recorded against the published PR HEAD in PR #38, rather than relabeling the
original local runs as a test of a later integrated revision.
