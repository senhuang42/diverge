# Repository workflow

These instructions apply to the entire repository.

## Test before publishing

- Run the complete non-slow test suite (`.venv/bin/pytest -m "not slow"`) before every commit.
- Run `.venv/bin/ruff check src review tests` before every commit.
- Do not commit or push when either command fails. Fix the failure and rerun both commands first.

## Commit and push continuously

- Commit cohesive completed increments frequently instead of accumulating a large unpublished change.
- Write concise commit messages that describe the verified behavior.
- After each successful commit, push it directly to `origin main`.
- Never force-push or rewrite published history.
- Preserve unrelated user changes; include only files belonging to the completed increment.
