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

Every skill, command and agent below is named with the `ecc:` prefix. The table lists the main known conflicts, not all of them. `AGENTS.md` and the task's approval win over every default of any ECC skill, including its folders, commits, branches, PRs, installs and clean-up. Skip a conflicting default, or ask the user first. ECC hooks run by default (Bash, Edit, Write and Skill matchers). Note their effects in the evaluation record.

| Work | ECC replacement | Model | Conflict with `AGENTS.md` |
|---|---|---|---|
| Unclear requirements or a design decision | No ECC equivalent for clarifying requirements: ask the user directly. Use `ecc:council` only to settle a choice between defined options. | Opus for cross-cutting design, otherwise Sonnet | `ecc:council` is not for code review, planning or architecture design. Its persistence rule may update GitHub issues: get approval first. |
| Recording a design decision | `ecc:architecture-decision-records` only when the task asks for an ADR | Sonnet | Writes `docs/adr/`. Record decisions in the issue or in existing docs unless the task approves an ADR. |
| Breaking large work into issues | Suggest `ecc:epic-decompose` to the user. It reconciles an existing epic issue, not free-form work. If none fits, do the work without a skill. | Sonnet | Adds and removes issue labels, sets status claimed or in-progress, writes a local store and comments on the issue. It runs `node scripts/github-coordination.js`, which exists only in the plugin. Suggest it only with the user's approval for each issue write. |
| Approved multi-step implementation | `ecc:plan`, then `ecc:prp-plan` and `ecc:prp-implement` only for a large plan | Sonnet | `ecc:plan` waits for confirmation and may write `.claude/plans/`. `ecc:prp-plan` and `ecc:prp-implement` write `.claude/PRPs/`, create a branch from `main` and archive the plan. `ecc:prp-implement` also runs `git pull --rebase origin <branch>`, writes `.claude/PRPs/reports/` inside the protected `.claude/`, and writes tests after implementing, against TDD. Keep the plan in the issue, use the task's branch and follow the TDD rule. |
| New behaviour, bug fixes, scripts and tests | `ecc:tdd-workflow`. Add `ecc:cpp-testing` for C++ and `ecc:python-testing` for Python tests. | Sonnet | `ecc:tdd-workflow` makes a checkpoint commit per stage, runs a Node helper in Step 0 and expects Playwright E2E. Its Step 8 writes a TDD evidence report to `.github/tdd/`, `.claude/tdd/` or `docs/releases/`, and the first two are protected: write none there, and record evidence in the issue or PR. Its 80% coverage target is not a repository requirement. No ECC skill covers Ztest or Twister: use the repository's test runners. |
| Failing checks or unexpected behaviour | `ecc:cpp-build` for CMake and linker errors, `ecc:agent-introspection-debugging` when the agent loops or stalls. No ECC equivalent for debugging failing tests or unexpected runtime behaviour: reproduce, isolate and fix directly under the TDD rule. | Sonnet, or Opus if the cause stays unclear | `ecc:cpp-build` and `ecc:build-fix` run `cmake -B build -S .` or npm and Cargo builds. Use the repository presets and `scripts/` checks instead, and keep each fix minimal. Prefer `ecc:cpp-build` to `ecc:build-fix`. |
| Agent guidance and skills | No ECC equivalent for writing guidance: write it directly. `ecc:skill-comply` can measure compliance, but weakly. | Sonnet | `ecc:skill-comply` runs `claude -p` at cost and needs the user's approval. `ecc:rules-distill` targets ECC rule files through a path absent from a plugin install: do not use it. |
| External research | `ecc:deep-research`, or `ecc:search-first` before adding a dependency | Sonnet | `ecc:deep-research` needs firecrawl or exa MCP tools and sends queries to them. Send no repository code or private context. `ecc:search-first` defaults to Adopt (install and use directly), Extend (install plus a thin wrapper) and Configure MCP, and queries npm, PyPI, GitHub and the web: use it for search only, and never install or wrap without approval (escalate before dependency changes, no pass-through wrappers). Without those tools, report that and use WebSearch. |
| Finding files or code | built-in `Explore` agent | Haiku | None. |
| Material review | `reviewer` agent, unchanged and mandatory. Do not replace it with `ecc:code-review`, `ecc:orch-review` or `ecc:code-reviewer`. | Opus, fixed in the agent | None. |
| Before claiming completion | `ecc:verification-loop` | Same as the work | Its commands assume Node and Python. Run the `AGENTS.md` checks instead and use the loop only as a checklist. |
| Handoff to another session or agent | Suggest `ecc:save-session` to the user | Same as the work | Writes to `~/.claude/session-data/`, outside the repository. Record the handoff in the issue or PR as this file requires. |
| Small or mechanical change | none | Sonnet | None. |

Do not use these ECC skills without the owner's approval. They commit, push, open PRs, post reviews or send code outside the session: `ecc:astra-review` sends the diff to OpenAI, PR-mode `ecc:code-review` posts `gh pr review --approve` or `--request-changes`, which can look like an approval, and the `ecc:orch-*` workflows (some commit or open PRs), `ecc:prp-commit`, `ecc:prp-pr`, `ecc:pr` and `ecc:multi-*` commit, open PRs or call external models. Also do not use these ECC agents and skills without the owner's approval. They write to protected paths, `.claude/`, `~/.claude` or `docs/`: `ecc:harness-optimizer` (edits hooks, agents, skills and settings), `ecc:doc-updater` (generates `docs/CODEMAPS/*`, edits READMEs), `ecc:agent-sort`, `ecc:living-docs-governance`, and `ecc:skill-stocktake` (every run writes a results cache under `~/.claude/skills/skill-stocktake/`). `ecc:rules-distill` stays barred, as the table says.

### Extra review lenses

These agents are optional input for the author and the `reviewer` agent. They add to the mandatory `reviewer` agent and never replace it. Their findings are not a verdict and gate nothing. They have no Write or Edit tools but do have Bash: tell them not to change files. Use one only when the change touches that kind of code:

- `ecc:python-reviewer`: Python test tooling and scripts, such as `tests/target/` and `tests/build/`.
- `ecc:silent-failure-hunter`: C error paths and scripts, for swallowed errors and bad fallbacks.
- `ecc:pr-test-analyzer`: behavioural test coverage of a change.
- `ecc:security-reviewer`: secure-management, TLS, SSH, credential and access code. It is web and npm oriented (OWASP Top 10, `npm audit`), so its fit for embedded C is partial.

`ecc:cpp-reviewer` and the `ecc:cpp-coding-standards` skill target C++, not this repository's C17 code. Use them only for C++ test code.

### ECC rules and contexts

Do not install ECC `rules/` or load ECC `contexts/`. A plugin cannot distribute `rules/` (per the ECC README), and they conflict with this repository:

- `rules/common/git-workflow.md` notes that ECC installs set `includeCoAuthoredBy` to false, so commits carry no `Co-Authored-By` trailer (this repository's practice is to add one), and its PR steps say to push the branch, with no Gate 2 or protected-path rule.
- `rules/common/agents.md` says to use `ecc:planner`, `ecc:code-reviewer`, `ecc:tdd-guide` and `ecc:architect` with "No user prompt needed".
- `contexts/dev.md` as a whole favours speed over process ("Write code first, explain after", "Get it working", "Run tests after changes") and has no test-first step, unlike the TDD rule.

### Session memory and learning

- ECC's session-end hook writes summaries to `~/.claude/session-data/`. On every Stop event it updates them with the last 10 user messages, each cut to 200 characters, plus edited file names and tool names (a model-written summary may be added). A session start can inject a "previous session" summary, and a parallel session or worktree of this repository can be its source. Treat it as stale and verify facts in the repository, issues and PRs. The Claude Code auto-memory is the record of owner decisions.
- Do not use `/resume-session`: with no argument it loads the newest file in the folder, whatever the project. Use `/sessions` to pick a file when needed.
- Do not enable the background observer (`skills/continuous-learning-v2/config.json` has `observer.enabled: false`). Do not run `/evolve --generate`, `/promote` or `/instinct-import` without the owner's approval.
- Use `/learn-eval` only for proposed skills: it asks before each save, and the owner confirms each one. Project scope writes `.claude/skills/<pattern-name>/SKILL.md` in this repository, a protected path: keep it uncommitted unless a protected-path PR with the owner's approval covers it.
- Instincts belong to `continuous-learning-v2`: the observer creates them and `/instinct-import` imports them. `/evolve` only groups existing instincts into evolved skills, commands and agents. Review them with `/instinct-status`. `/prune` deletes pending instincts older than 30 days: run it only with the owner's approval, after a `--dry-run` preview. Instincts are for tool-use habits. Owner decisions and preferences stay in the auto-memory.
