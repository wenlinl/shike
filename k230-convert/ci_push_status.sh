#!/bin/bash
# 把 CI 进度/失败日志追加到 codex/rawfood-out 分支（单文件历史），主 agent 可直接 git 拉取查看。
set -e

export GIT_AUTHOR_NAME="codex-ci"
export GIT_AUTHOR_EMAIL="codex-ci@users.noreply.github.com"
export GIT_COMMITTER_NAME="codex-ci"
export GIT_COMMITTER_EMAIL="codex-ci@users.noreply.github.com"

STAGE="$1"
LOG="${2:-}"

if [ ! -f ci_status.txt ]; then : > ci_status.txt; fi
echo "$(date -u +%H:%M:%S) [$STAGE]" >> ci_status.txt
if [ -n "$LOG" ] && [ -f "$LOG" ]; then
  echo "--- $STAGE log tail ---" >> ci_status.txt
  tail -25 "$LOG" >> ci_status.txt || true
fi

blob=$(git hash-object -w ci_status.txt)
tree=$(printf '100644 blob %s\tci_status.txt\n' "$blob" | git mktree)
if parent=$(git rev-parse --verify refs/remotes/origin/codex/rawfood-out 2>/dev/null); then
  commit=$(git commit-tree "$tree" -p "$parent" -m "ci: $STAGE")
else
  commit=$(git commit-tree "$tree" -m "ci: $STAGE")
fi
git push origin "$commit":refs/heads/codex/rawfood-out || \
  git push origin "$commit":refs/heads/codex/rawfood-out --force
echo "STATUS_PUSHED $STAGE $commit"
