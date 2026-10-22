# M5 networking software evidence

Status: qualification and independent review in progress; not physical acceptance.
The user approved the client-only architecture on 22 October 2026, including
explicit Host SIGPIPE composition and narrowly scoped dependency enforcement.
This work does not authorise hardware operations, merge, issue closure or M5
milestone completion. See the [consumer contract](../networking-contract.md).

## Source and reproducibility

Branch: feat/m5-portable-networking, based on main 94b11e4. Zephyr is unchanged at
684c9e8f32e4373a21098559f748f06915f950c9 (4.4.0). The software-only environment uses
the existing Linux amd64 development image, CMake 3.28.4, Ninja 1.11.1, Clang 18.1.3
for Host, GCC 13.3.0 for native_sim and pinned Zephyr SDK 1.0.1 for RT1060.
Native Picolibc tests fetch revision 01254932e8e81085817ed61fd858648584ffe37c,
from the existing pinned Zephyr manifest, into an external cache. No upstream
checkout, board, Flash layout or other task branch is modified.

Routine commands:

```sh
scripts/check-host.sh
CRUX_HOST_PRESET=host-sanitize scripts/check-host.sh
python3 -m unittest discover -s tests/build -v
CLANG_FORMAT=clang-format scripts/check-format.sh
scripts/analyze-host.sh
scripts/check-dependencies.sh
scripts/bootstrap-networking-tests.sh
scripts/test-networking.sh
```

The linked worktree container mounts the workspace read-only and this worktree
writable; it has no hardware/device access. Local Host sanitizer dependency
builds use their own cache so concurrent execution work does not share generated
GoogleTest objects. Raw local outputs are retained under build/investigation;
final source/evidence hashes and completed results will be recorded after checks.

## Evidence interpretation

- Deterministic tests replace syscalls/clock only in private test object targets;
  production code is compiled separately. They cover short I/O, would-block,
  interruption, deadlines, connect errors, EOF, UDP protocol lengths, failed
  cleanup and netif output/address semantics.
- Host real-stack fixtures use actual Linux loopback and explicit SIGPIPE policy.
  Standard-profile consumer code is shared with the Zephyr fixture; peer server
  functions and native resource inspection are private fixture facilities.
- Zephyr real-stack evidence requires Picolibc and actual live native contexts.
  Merely running a native_sim executable is insufficient: the early external-libc
  run resolved calls to Linux, then terminated with SIGPIPE. That failed run and
  the corrected interpretation of the earlier header probe are preserved.
- Native carrier manipulation is emulation, not physical cable loss. Netif status
  does not bind a socket or prove reachability. Both loopback test peers and the
  observed primary refer to 127.0.0.1; RT1060 composition assumes the isolated
  static 192.168.77.0/24 setup documented in the contract.
- Descriptor/context/pool return supports only the declared repeated scenarios;
  it does not prove absence of all leaks, long-run capacity or target timing.

## Independent review

Initial read-only review found delayed readiness could permit an extra sample
transfer after expiry, and the runtime adapter did not retry interrupted poll
against its original deadline. Both require regression tests and correction.
No final review PASS or completed qualification is claimed at this stage.

## Outstanding physical acceptance

Issue #28's representative physical RT1060 controller communication path remains
pending. Cross-compilation cannot establish actual TCP/UDP traffic, PHY/carrier
loss, resource/timing behaviour or recovery. A separately reviewed, explicitly
authorised physical procedure with identified firmware, peer and raw evidence
is still required. Existing accepted M3 evidence is retained unchanged and is
not relabelled as portable M5/physical POSIX qualification.
