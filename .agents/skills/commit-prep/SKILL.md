---
name: commit-prep
description: Stage the relevant changes, draft a commit message that states the verification level, and commit it as a savepoint.
disable-model-invocation: true
allowed-tools: Bash(git *)
---

# Commit a savepoint

Stage the relevant changes and commit them. **No confirmation is needed** —
savepoints are wanted at every meaningful step, including unverified work (see
`agents-instructions.md` § Git).

## Steps

1. Run `git status` to show changed/untracked files
2. Run `git diff --stat` for a summary of changes
3. Identify which files belong to this step and stage only those (skip
   `.claude/settings.local.json`). Where independent pieces of work are in the
   tree at once, commit them separately so each can be reverted on its own.
4. Draft a concise commit message (imperative mood, explain why not what),
   **stating the verification level** in the body
5. Run `git commit`, then report the hash

## Commit message format

```
Short imperative summary (under 72 chars)

Optional body explaining motivation or non-obvious decisions.
Bullet points for multiple changes if needed.

Verification level: savepoint, untested | host-verified, not heard | verified on hardware
```

## Rules

- Never `git push` — only the user pushes
- Never commit on `main`; commit on the current feature branch
- Never bypass signing or hooks
- Never include `.claude/settings.local.json`
- If README controls table needs updating, flag it in the report
