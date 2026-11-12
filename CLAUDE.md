# Claude Code instructions

@AGENTS.md

`AGENTS.md` holds the rules for every agent in this repository and wins over this file. This file adds Claude-specific routing: which skill, model and subagent fits each kind of work.

## Route the work

Inspect the request, the issue or PR, existing decisions and installed skills, then pick the row that fits. Skills are preferences. Use an installed skill by its full `plugin:skill` name. Suggest a user-invoked skill to the user instead of running it. Run one workflow per task and reuse approved requirements and plans.

| Work | Skill or agent | Model |
|---|---|---|
| Unclear requirements or a design decision | `mattpocock-skills:grilling` or `superpowers:brainstorming`, one of them. Suggest `mattpocock-skills:grill-me` to the user. | Opus for cross-cutting design, otherwise Sonnet |
| Breaking large work into issues | Suggest `mattpocock-skills:to-tickets` to the user | Sonnet |
| Approved multi-step implementation | `superpowers:writing-plans`, then `superpowers:executing-plans` or `superpowers:subagent-driven-development` | Sonnet |
| New behaviour, bug fixes, scripts and tests | `superpowers:test-driven-development` | Sonnet |
| Failing checks or unexpected behaviour | `superpowers:systematic-debugging` | Sonnet, or Opus if the cause stays unclear |
| Agent guidance and skills | `mattpocock-skills:writing-for-agents` | Sonnet |
| External research | `mattpocock-skills:research` | Sonnet |
| Finding files or code | built-in `Explore` agent | Haiku |
| Material review | `reviewer` agent | Opus, fixed in the agent |
| Before claiming completion | `superpowers:verification-before-completion` | Same as the work |
| Handoff to another session or agent | Suggest `mattpocock-skills:handoff` to the user | Same as the work |
| Small or mechanical change | none | Sonnet |

Skills and subagents add no approval or permissions. When a skill asks for confirmation, continue only if the task's approval and tool permissions already cover the action. A skill's default folders, commits, branches, PRs and clean-up give way to `AGENTS.md` and the task's approval. Record skill outputs such as designs, plans and research in the issue or in existing docs.

## Models and subagents

- The models above are defaults. The main session keeps its selected model. When work needs a different model, delegate it to a subagent with that model or ask the user to switch.
- Choose for end-to-end efficiency: the required quality first, then time, retries and rework, then cost. Start with moderate reasoning effort and raise it for complex or risky work. Investigate a failure before reaching for a stronger model.
- A subagent that a skill dispatches uses at least the default model for that work.
- Change a default only through a reviewed change to this file, based on recorded outcomes such as rework.
- Use a subagent for independent review, context isolation, safe parallel work or a different model. Keep work that shares files in one agent.
- Record the exact model ID that ran, or "unknown", and the tool version.

## Review with the reviewer agent

The `reviewer` agent in `.claude/agents/` has read-only tools and a fixed Opus model. Use it for every material review and keep its model. Reviews inside a skill, such as the task reviews of `superpowers:subagent-driven-development`, do not replace it. It has no shell, so supply the base and head SHAs, the diff command, the diff as a file, the changed protected paths, the acceptance criteria, and the check results with who ran them. Run the checks yourself and record them apart from the review.

## Completion and handoff

Record the revision, the checks as `AGENTS.md` requires, open findings, approvals still needed, and significant skill, delegation and model choices.

## ECC evaluation period

Since 2026-11-12 the owner is evaluating the everything-claude-code (ECC) plugin, and the `superpowers` and `mattpocock-skills` plugins are disabled. Use the ECC skill in the table below instead of each unavailable skill in the routing table. The model column is unchanged. When the evaluation ends, remove this whole section.

Every skill below is exposed with the `ecc:` prefix. Each runs under the existing rule: a skill's default folders, commits, branches, PRs and clean-up give way to `AGENTS.md` and the task's approval. Where a skill's default conflicts with that, the table says so. Skip that default, or ask the user first.

| Work | ECC replacement | Model | Conflict with `AGENTS.md` |
|---|---|---|---|
| Unclear requirements or a design decision | `ecc:council` for a decision with several credible paths. Suggest `ecc:council` to the user in place of `grill-me`. | Opus for cross-cutting design, otherwise Sonnet | None. It launches subagents. |
| Recording a design decision | `ecc:architecture-decision-records` only when the task asks for an ADR | Sonnet | Writes `docs/adr/`. Record decisions in the issue or in existing docs unless the task approves an ADR. |
| Breaking large work into issues | Suggest `ecc:epic-decompose` to the user. It reconciles an existing epic issue, not free-form work. If none fits, do the work without a skill. | Sonnet | Writes a coordination block and a comment on the issue. Run it only with the user's approval. |
| Approved multi-step implementation | `ecc:plan`, then `ecc:prp-plan` and `ecc:prp-implement` only for a large plan | Sonnet | `ecc:plan` waits for confirmation and may write `.claude/plans/`. `ecc:prp-plan` and `ecc:prp-implement` write `.claude/PRPs/`, create a branch from `main` and archive the plan. Keep the plan in the issue, and use the task's branch. |
| New behaviour, bug fixes, scripts and tests | `ecc:tdd-workflow`. Add `ecc:cpp-testing` for C++ and `ecc:python-testing` for Python tests. | Sonnet | `ecc:tdd-workflow` makes a checkpoint commit per stage and targets 80% coverage. Commit only as `CONTRIBUTING.md` and the task allow, and use the repository's test runners. |
| Failing checks or unexpected behaviour | `ecc:build-fix` for build errors, `ecc:agent-introspection-debugging` when the agent loops or stalls | Sonnet, or Opus if the cause stays unclear | `ecc:build-fix` assumes npm, Cargo, Maven or Go builds. Use the `scripts/` checks and keep each fix minimal. |
| Agent guidance and skills | `ecc:rules-distill` for rule extraction, `ecc:skill-comply` to measure compliance. No equivalent for writing guidance: write it directly. | Sonnet | `ecc:rules-distill` edits rule files and `ecc:skill-comply` runs `claude -p` at cost. Both need the user's approval, and rule files here are protected paths. |
| External research | `ecc:deep-research`, or `ecc:search-first` before adding a dependency | Sonnet | `ecc:deep-research` needs firecrawl or exa MCP tools and sends queries to them. Send no repository code or private context. Without those tools, report that and use WebSearch. |
| Finding files or code | built-in `Explore` agent | Haiku | None. |
| Material review | `reviewer` agent, unchanged and mandatory. Do not replace it with `ecc:code-review`, `ecc:orch-review` or `ecc:code-reviewer`. | Opus, fixed in the agent | None. |
| Before claiming completion | `ecc:verification-loop` | Same as the work | Its commands assume Node and Python. Run the `AGENTS.md` checks instead and use the loop only as a checklist. |
| Handoff to another session or agent | Suggest `ecc:save-session` to the user | Same as the work | Writes to `~/.claude/session-data/`, outside the repository. Put nothing private in the repository. |
| Small or mechanical change | none | Sonnet | None. |

Do not use these ECC skills without the owner's approval. They commit, push, open PRs or send code outside the session: `ecc:astra-review` sends the diff to OpenAI, and the `ecc:orch-*` workflows, `ecc:prp-commit`, `ecc:prp-pr`, `ecc:pr` and `ecc:multi-*` commit, open PRs or call external models.
