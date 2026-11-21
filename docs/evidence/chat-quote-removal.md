# Removal of quoted chat messages from the evidence narrative

Some Markdown files of the evidence quoted the maintainer's chat messages (approvals, in Korean) and named
the session through which they were relayed. The quotes and the session wording were replaced by a
neutral statement that the maintainer approved the step, with the date and the scope of the approval. The
scope facts (what was approved, which runs, which limits) are unchanged. The maintainer approved this
removal on 2026-11-18.

## What changed

- Only Markdown narrative changed: the `README.md` files and plan documents listed below, plus the three
  `SHA256SUMS.tsv` files, which carry the new digests of the plan documents. No log, JSON, console
  capture or other measurement file changed.
- In the `microsd-card-states-stack`, `networking-physical-stage1`, `networking-physical-stage1-run2`
  and `networking-physical-stage1-run3` records, and in the plan revisions under their `inputs/plans/`,
  each quoted message was replaced by "approved by the maintainer" with the same date and the same scope.
- Smaller wording changes: `microsd-physical/README.md` and `boot-emmc/vendor-documents.md` no longer
  say that an approval was given "in chat"; `rt1060-management-image-feasibility/entropy-line-c10/README.md`
  states the approval of 2026-11-09 without the quote; `boot-composition/wp1a-evidence.md` no longer
  says that a decision was relayed to the work.
- The session's own instructions (for example to continue with the fix PRs, or the last time to start a
  board window) are not the maintainer's approval and stay attributed to the main session.

## How the digests stay consistent

This follows the rule of [probe-serial-redaction.md](probe-serial-redaction.md).

- In the three `SHA256SUMS.tsv` files, `original_sha256` of each changed plan document is kept as it was
  recorded (the digest recorded at run time; the runs' `plan_sha256` refers to the JSON plans, which this change
  does not touch). `committed_sha256` holds the digest of the file as committed now, and the state is
  `redacted`. The row of `stage1-phase-b-plan-r5.md` has no run-time digest (the run-time version was
  not retained, see the correction in the `README.md` of `networking-physical-stage1`), so only its
  `committed_sha256` changed and its state text gained one clause.
- The digests that the runs recorded about their inputs are unchanged and are not expected to match the
  committed plan documents.
- The table below lists the digest of each changed file on `main` before this change and after it. The
  table in [probe-serial-redaction.md](probe-serial-redaction.md) records the digests from the earlier
  serial redaction, which are the "before" digests of the files it lists.

| path (relative to `docs/evidence/`) | sha256 before | sha256 after |
| --- | --- | --- |
| `boot-composition/wp1a-evidence.md` | `dba3460039e1f5139902b83e9032daa6ba19fb17d6f99978015c4c9cd6d752d0` | `42aa04f57bd1741efe03ad4f48aaa2a28b1dbfb38211a57ee231e87b760ece46` |
| `boot-emmc/vendor-documents.md` | `af8c137e8e09333d27873c7b10d516171ee34ea5bed3dfb781788a8bfe2e965c` | `862e4bb24fc26a4491e12e0d8614be294490d82789bac7b66cc2b4178270ac05` |
| `microsd-card-states-stack/README.md` | `5011b7062de626d62318e00daf463e152066434d2ea095ef44a4c0829d51a308` | `e34c776266d72294bd02d0453ebc8edd5d13ce897526def23bd0f5194bd64c70` |
| `microsd-physical/README.md` | `2626642ca6f645744d72cbf5abf38176625d21e841660b6cc2f6ab905c77e81c` | `3f2222d7e6ee32eb8947b80128b6d2ed620cd92584489cb16b2ee2ca16d540ac` |
| `networking-physical-stage1-run2/README.md` | `6d963d80e35f5bda400744c48c5da57f22f44c6950d048c02508ac9134ed71e7` | `61cd5930e0fc11104ad047ffb3f208f3959a59e8d4a4c0457d96103d2e82a2d6` |
| `networking-physical-stage1-run2/SHA256SUMS.tsv` | `12b2ea9eea6108b4f5e853740935f401cbe490fe6caf1a770bcb1b5b8a2a6afb` | `e0398ce871abb563a69ecc4509a9eb4ff9c57811a4acadd6c9550eb1257de6d4` |
| `networking-physical-stage1-run2/inputs/plans/stage1-phase-b-plan-r6.md` | `a8b2b99eecdb01dadf53eb632e2e378e61fd35cf2a40051acc62253918c26f80` | `9786b1f44d28c1b5632fa701ae8df3f99ebe5b694b354a7ace12c62fb5896b2d` |
| `networking-physical-stage1-run3/README.md` | `d968d42d669317a16afb8155d7dc47c3c1e08759800a5c3c53ed317dcf4f4902` | `bbabf757ebd011ec066eed367b880f1162e56fd23bd9a5a07cfb9c21a8d84f20` |
| `networking-physical-stage1-run3/SHA256SUMS.tsv` | `5e129f0becfa3b36e749ac7b3bb866ba0ca75e9e45b4b8eadc69a14284e51768` | `47769f0a7dc7b6ab3a494566b0fb38b27ec741c87de1a9e67892c1d7cc49a629` |
| `networking-physical-stage1-run3/inputs/plans/stage1-phase-b-plan-r7.md` | `e2b73eb83effe02d2328fd10530ea90be011cee77791f85ec324f50877f9a8f8` | `662c7a3334a844d2c9cb62d9825ce99e3324dd500d7ebb3be51fcec756a056f3` |
| `networking-physical-stage1/README.md` | `8aeb02023d4778ad12cc1c5f882b8966572e6df0b43add1bca27311b398add62` | `6d694c6ce37cee648a3ee1b2212db1f34dd6475717e8c29ffb4c6544fb6df7f9` |
| `networking-physical-stage1/SHA256SUMS.tsv` | `8d5298ae4e1e64e7228239cfc134d32b6f9bbfdb195af2ed10e91ea8302c4c58` | `d5e40052b71b01c1d92ed02d70eee608424f3dc540de59ab3baf0ddf588097f5` |
| `networking-physical-stage1/inputs/plans/stage1-phase-b-plan-r3.md` | `efff81a9e2f4d579daec28e0e4981142107db19652139224b4ac45d20e224237` | `ce42fe89064a0911132e4a73332c0830f1ef31bba43dadf8e2b4dfb89b0ebefa` |
| `networking-physical-stage1/inputs/plans/stage1-phase-b-plan-r4.md` | `8029e7a5847bc77433ebf57f806531bd4372493e2673e99149c4f86a362e4db9` | `3f673d69bab2754c7b252c30d1ce1018db5e8c27789f028377fa272a0858421d` |
| `networking-physical-stage1/inputs/plans/stage1-phase-b-plan-r4b.md` | `bd76bdae79d1d854fecd75737d526d991b4d0b0e2beafebefedd661393e8cc1b` | `6d6bc7422369183b75b24b9b8fdd6b621af54b428b3fe21470a24d3bde026363` |
| `networking-physical-stage1/inputs/plans/stage1-phase-b-plan-r5.md` | `337df0e7c5cd5bfb32339fad1fd5f93a600325e0dc3ab601f63451d13bfae8ac` | `c35f5bb2013104943d213c4a89e9eb96ab6901804f076c126a18b041db61994d` |
| `networking-physical-stage1/inputs/plans/stage1-phase-b-plan.md` | `8617fa2497bb25de2a5cb77d122ae2951a42377c250a0aaf345e6b2a1d00d3bb` | `190d7b07cd0a24c2a202f4453f2ff498a8287c66f8eed0850c197115d90a9bd6` |
| `rt1060-management-image-feasibility/entropy-line-c10/README.md` | `905d62ff3fc69835e21e51735d944a63a6b442ed3aa0d01ecf15e18c9a1053bc` | `b01ac429a4761ae71315ad5585d1e1deeae1ea6947931e290499c838ae6f7d07` |
