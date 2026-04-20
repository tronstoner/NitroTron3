---
name: commit-prep
description: Stage changes, show diff summary, draft a commit message, and wait for user confirmation before committing.
disable-model-invocation: true
allowed-tools: Bash(git *)
---

# Prepare Commit

Stage changes and draft a commit message for user approval. **Never execute `git commit` until the user explicitly confirms.**

## Steps

1. Run `git status` to show changed/untracked files
2. Run `git diff --stat` for a summary of changes
3. Identify which files should be staged (skip `.claude/settings.local.json`)
4. Draft a concise commit message (imperative mood, explain why not what)
5. Present the staged files and proposed message to the user
6. **Wait for explicit confirmation** before running `git commit`

## Commit message format

```
Short imperative summary (under 72 chars)

Optional body explaining motivation or non-obvious decisions.
Bullet points for multiple changes if needed.
```

## Rules

- Never `git push`
- Never commit without user saying "commit", "yes", "go ahead" or similar
- Never include `.claude/settings.local.json`
- If README controls table needs updating, flag it before committing
