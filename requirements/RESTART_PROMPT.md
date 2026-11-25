<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->
# Restart prompt (short version)

How a stopped build is resumed (after an interruption, a cleared or
compacted conversation, or a new session). The method is the one of
[BUILD_PROMPT.md](BUILD_PROMPT.md): the new session is again the
orchestrator, never writes code, and runs every step in one fresh
subagent, one at a time, each checked afterwards.

1. **Read the build log first:** its Status block (current phase, last
   commit, next step, test baseline, open BLOCKED items) and the "Current
   state" section (the plan in progress, decisions, gotchas, open
   questions).
2. **Check the repository:** `git log --oneline` and `git status`. The
   last commit should match the Status block; a clean tree means the last
   step ended at a commit boundary. Work that is committed is never redone
   or rewritten.
3. **Continue with the first step that is not DONE.** If the phase in
   progress has no step list yet, plan it first. A step reported as STEP
   INCOMPLETE gets a new subagent for exactly the remaining work.
4. **Keep the token log going:** one row per subagent run; a run lost in
   the interruption gets a row without a count and the note "lost in
   restart".
5. **No build log:** the build has not started; start at phase 1 (only
   when the owner has said to start building).
6. **Status says the build is complete:** start no new steps; answer
   questions from the final report in the build log.
