<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->
# Build prompt

The method for building Yantra with Claude from the specifications
([requirements_yantra.md](requirements_yantra.md),
[requirements_radios.md](requirements_radios.md)). It covers two things:
the orchestrator and the build log. The build is unattended: no stops to
ask the owner; a needed choice takes the option the spec recommends
(otherwise the simplest one that keeps the requirements) and is written in
the build log.

## Build machine

Everything that depends on the build machine (build and test commands,
job and CPU limits, build-tree locations, the Windows test machine, the
SDK location, host names) is defined in the requirements project (the
manager folder, not published), file `BUILD_MACHINE.md`. Read it before
building and pass its rules on in every step brief.

## 1. The orchestrator

**Role.** The session that reads this prompt is the orchestrator from the
first step to the final report. It never writes code, tests or docs and
never commits in the code repository. It plans, delegates, checks, logs
and reports. It may edit only the build log's plan, Status block and
token lines, and the token log.

**Planning.** Before a phase, write its step list into the build log
("In progress"): 3-6 steps, each a few hours of work that ends in working,
committed code, based on the phase's spec sections and on what earlier
steps left behind. At the end of each phase, check coverage: every
requirement ID of the phase's spec sections is done, scheduled later, or
recorded as partial or blocked with a reason.

**One fresh subagent per step.** Every step runs in one new general-purpose
subagent with a clean context (never a fork of the orchestrator), one at a
time. Wait for its completion before checking it and before starting the
next step; do not read its transcript.

**The step brief.** A fresh agent must be able to work from the brief
alone. It holds:

- the step and phase, the repository, and "only this step";
- what to read first: the project rules, the build log's Status and
  Current state, `BUILD_MACHINE.md`, and the spec sections of the step's
  requirement IDs;
- the requirement IDs, what to build, the tests and docs expected
  (screenshots for UI), hooks and file names left by earlier steps, known
  pitfalls, and the name of the next step;
- the rules: small logical commits that each build and pass their tests,
  real dates, requirement IDs cited instead of spec text, the co-author
  trailer, no history rewrite, no push; never delete, skip or loosen a
  test to make it pass; the spec's scope and excluded sources; stop every
  process it starts; stop at a clean commit and report STEP INCOMPLETE
  when its context runs short;
- the logging it must do (section 2);
- the report it must return (at most about 30 lines): STEP DONE or STEP
  INCOMPLETE, commits, requirement IDs done / partly done / not done with
  the reason, test totals, changed tests or golden files and why, known
  issues, open questions.

For the last step of a phase, the brief adds: write the phase summary
entry and move the phase's step entries to the archive.

**Checking every step.** The report is model output, not proof. After each
step the orchestrator checks for itself:

1. the working tree is clean and every new commit has the trailer;
2. the build has no warnings; the fast suite and the timing tests pass
   (the full suite and the other build trees after the last step of a
   phase);
3. test totals did not drop without a logged reason; changed golden files
   are explained in their commit message;
4. the build log has the step entry and an updated Status block with the
   BLOCKED list intact;
5. no stray processes, test servers or untracked files;
6. no forbidden content: no licence-restricted images, no personal data,
   no radio or IQ code outside the radio plugins;
7. for UI steps, a look at one or two new screenshots;
8. claims such as "library X is not allowed" or "feature Y is impossible"
   are checked against the spec before they are accepted.

**Deciding.** All checks pass: next step. A check fails or the report says
STEP INCOMPLETE: a new subagent (a fix-up or retry run) for exactly the
failure or the remaining work. A test that failed once and passed on rerun
is not a pass: its root-cause fix goes into the next brief. A wrong premise
or a must-have left partial: insert an extra step (e.g. "9.7b"). After two
failed attempts at the same item: mark it BLOCKED with the reason and move
on. A rule slip is never fixed by rewriting history: note it and make sure
the head passes.

**Reporting to the owner.** After each checked step: a few lines on what it
delivered, what is partial, new open questions, and its tokens (including
fix-up and retry runs). After each phase: a short paragraph (commits,
requirement status, blocked items) and the phase's token table with the
running total; point out a step above twice the average or rework above
20 % of a phase. Never wait for an owner decision before continuing.

## 2. The build log

**Where.** The build log lives in the requirements project, never in the
code repository; older entries move to an archive file next to it.

**Status block (the resume point).** At the top, exactly these lines:
current phase, last commit, next step, test baseline, open BLOCKED items.
Each step updates them; the BLOCKED list is always passed on in full.

**Current state.** Below the Status block, at most about 250 lines: a
commit table (one line per finished phase), "In progress" (the plan of the
current phase and what earlier steps left), decisions taken instead of
asking the owner, rejected options, gotchas, open questions, and the token
usage (one line per finished phase plus the running total). Each step
rewrites it to stay current and short.

**One entry per step.** Below a separator, newest at the bottom:
`### <date time> - Phase <n>, step <n.m> <name> - DONE | PARTIAL | BLOCKED`
with what was done (files, requirement IDs), the commits (hash and
subject), the test totals, decisions, problems and fallbacks, and what is
left. Timestamps come from the clock, never guessed.

**Token log.** One row per subagent run, recorded by the orchestrator from
the completion notification before anything else: step, kind (`step`,
`fixup`, `retry`, `final`), tokens, tool uses, seconds, result. A run
without figures gets `n/a` (never an estimate); a run lost in a restart
gets `n/a` and the note "lost in restart". The log gives a table per phase
and a build report (per phase, total, rework share, largest steps,
average per step); see [TOKENS.md](TOKENS.md).

**Final report.** After the last phase, one more subagent re-runs every
check in every build tree and writes the "Final report" at the end of the
build log: the commits per phase, the token report, the status of every
requirement ID of both specs (done / partly done / not done / blocked /
deferred, with the reason and totals per priority), test and CI results,
known issues, what needs real hardware to verify, open questions and any
deviations from the rules. It sets the next step to "none: build
complete" and makes no code changes. After that, no new steps unless the
owner asks.
