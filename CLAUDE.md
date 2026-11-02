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
| Hand-off to another session or agent | Suggest `mattpocock-skills:handoff` to the user | Same as the work |
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
