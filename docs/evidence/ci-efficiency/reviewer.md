# Independent CI efficiency review

Decision: PASS for the inspected configuration/source review gate. No material defect found in the pending diff against main b32b858f911b22df10894c8ea922fd3ef6366b2e. This is not full Goal completion or permission to merge.

## Evidence inspected

- Actual before/after host.yml and zephyr.yml, full current workflow files, AGENTS.md, CONTRIBUTING.md, README.md and the new evidence README.
- Independent text comparison confirms every Host execution job/step is identical; every Zephyr execution job/step before the upload step is identical. Job IDs remain host and zephyr; tools, permissions, environments and test commands remain unchanged. git diff --check b32b858 succeeded.
- Inspected scripts/test-m3.sh, test-networking-physical.sh, record-build-provenance.py, prepare-nxp-mdio.py, the affected component test script commands, all four M3 output verifier dependencies, and prepare_m3_image.py / prepare_m5_network.py qualification dependencies.
- Inspected the real downloaded baseline central-directory inventory. Removed Twister bulk is primarily generated header forests, native intermediates and duplicate native-simulator build output; this does not remove any explicit EVKB image path from the baseline workflow.

## Reasoning

1. main-only push plus unchanged PR integration events and manual dispatch implements the authorised trigger policy. The concurrency expression includes workflow identity and PR number. Non-PR runs use distinct run IDs and false cancellation, so unrelated PR, other workflow, main and manual runs do not share a cancellation/pending group. Hosted behaviour remains to be exercised.
2. New generic patterns retain JSON/XML reports at each Twister root, raw recursive logs/errors, configure YAML/cache, compilation databases, hidden selected configurations, DTS and primary Twister maps. include-hidden-files, always upload and retention remain unchanged.
3. Crucial M3 replay dependencies are handler.log, zephyr/.config, production_formats.h and production_phase.h. All are now named by retained patterns. Pending/after M3 and pending time input captures are covered by build/evidence/*inputs*.json. A failure before .config exists can still retain configure/build diagnostics.
4. MDIO source generation produces exactly mdio_nxp_enet.c and corresponding mdio_nxp_enet.json; replacement of recursive directories by these exact files preserves its actual output evidence. Existing explicit images, resource reports and provenance paths remain. M5 CI does not pass --freeze; lack of a frozen physical handoff is an existing limitation correctly documented.
5. Changes are small and proportional: no custom collector, cache, test restructuring, skip logic, hardware action or tool upgrade introduced.

## Material limits and required completion evidence

- The actual @actions/glob inventory/failure-fixture check was still running when this source review completed. I inspected its setup and toolkit comment parsing but do not claim its eventual results. A subsequent failure/missing required file requires correction and re-review.
- Final CI on the changed revision is not yet run. Successful Host/Zephyr execution, actual archive inventory and <=100 MB conditional target, measured upload/job timings, same-PR supersession and absence of feature-push duplicates still need hosted evidence.
- No hardware tests, public changes, merge or issue closure performed or authorised by this review. The documented single-run timing caveats are appropriate.
