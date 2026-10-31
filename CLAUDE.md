# Claude Code instructions

@AGENTS.md

This file adds Claude-specific workflow and model-selection guidance. Follow `AGENTS.md`, `CONTRIBUTING.md`, the active user authorisation, adopted decisions and applicable GitHub issue/PR records. Do not duplicate their architecture, testing or approval rules here.

## Choose a workflow

First inspect the request, issue/PR, existing decisions, current source and installed skills. The skill names below are preferences, not repository dependencies. Use them only when installed and applicable. Do not install plugins or change global settings without authorisation.

Respect each installed skill's invocation metadata. Matt Pocock `grill-me`, `to-tickets` and `handoff` are user-invoked workflows: suggest them when appropriate rather than assuming automatic execution. Model-invocable skills such as `grilling` and `writing-for-agents` may be selected when installed and relevant. After installing Matt Pocock Skills, suggest `/setup-matt-pocock-skills` for project configuration, but review and obtain authorisation for any proposed repository, issue or label changes.

- **Unresolved requirements or significant design decisions:** Suggest the user-invoked Matt Pocock `grill-me` for a focused interview, or use Superpowers `brainstorming` for a new design. Do not run both discovery workflows by default. Preserve previously approved decisions unless new evidence warrants review.
- **Approved multi-step implementation:** Use Superpowers `writing-plans` if a new implementation plan is needed. After any required plan approval, use `subagent-driven-development` or `executing-plans` as appropriate, with `test-driven-development` for behaviour changes.
- **Bugs, failed tests and CI failures:** Use `systematic-debugging` to establish the root cause. Add a failing regression test first where practical, then implement and verify the fix.
- **Review and completion:** Use `requesting-code-review` when independent review is required, and `verification-before-completion` before claiming success. Neither skill replaces the project's human review gates.
- **Breaking down larger work:** Suggest the user-invoked Matt Pocock `to-tickets` when issue decomposition would help. Create or change GitHub issues only within the active authorisation.
- **Codex/Claude handoff:** Suggest the user-invoked Matt Pocock `handoff` when useful. Preserve authoritative progress, evidence, blockers and next steps in the relevant GitHub issue/PR when authorised.
- **Small documentation or mechanical changes:** Keep the workflow proportionate. Do not force brainstorming, a new plan or additional subagents without a clear benefit.

Use one primary implementation orchestrator per task. Do not combine Superpowers implementation workflows with Matt Pocock `implement` or `implement-spec` for the same work unless explicitly directed. A skill's default branch, PR, issue-closure or clean-up steps never override the repository rules, existing work or task-specific approval gates.

## Agents and models

- The main conversation uses its currently selected model. These preferences do not automatically switch the main-session model.
- When available, prefer **Haiku** for bounded, read-only research or file discovery, **Sonnet** for routine implementation, testing and delegated review, and **Opus** for complex architecture, cross-component reasoning or high-risk investigations and reviews.
- Select the model and available reasoning effort based on task complexity, required quality, risk, uncertainty, model capability and expected end-to-end cost, including retries and rework. Prefer the lowest-cost configuration likely to meet the required quality, not simply the cheapest model or the strongest one.
- Start with moderate reasoning effort for well-defined tasks. Increase it when deeper analysis is justified, reserving the highest levels for exceptional complexity or risk. Investigate failures before assuming that a stronger model or higher effort is needed.
- Escalate model capability or reasoning effort when findings and verification show the current approach is insufficient. Refine preferences using observed outcomes, validation results and resource use. Never reduce required verification or human approval to save cost.
- Use subagents only when independent review, context isolation or safe parallel investigation adds value. For a required independent review, the reviewer must not have authored the implementation. Do not parallelise tasks that may conflict in shared files or state.
- Specify the model for a delegated task when the installed tool supports it. Otherwise use the available model without claiming a model switch. All subagents remain bound by the same approvals and safety requirements.

## Completion and handoff

Record the actual revision, validation commands and outcomes, tests not run, open findings, blockers and required approvals. Mention significant skill, delegation and model choices when useful for review. Never equate Host tests, `native_sim`, cross-compilation, CI and physical-board evidence.
