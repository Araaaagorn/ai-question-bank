# =============================================================================
# GitHub 远程仓库初始化 + main 分支保护（与代码审查联动）
# 前置条件：已安装并登录 gh CLI（gh auth login），且账号对仓库有 admin 权限
# 用法：bash scripts/github_setup.sh [仓库名] [private|public]
# 示例：bash scripts/github_setup.sh ai-question-bank private
# =============================================================================
set -euo pipefail

REPO_NAME="${1:-ai-question-bank}"
VISIBILITY="${2:-private}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
PAYLOAD="$SCRIPT_DIR/branch_protection.json"

command -v gh >/dev/null 2>&1 || { echo "✘ 未找到 gh CLI，请先安装并登录：gh auth login" >&2; exit 1; }
gh auth status >/dev/null 2>&1 || { echo "✘ gh 未登录，请先执行：gh auth login" >&2; exit 1; }

OWNER="$(gh api user --jq .login)"
REPO="$OWNER/$REPO_NAME"
cd "$ROOT_DIR"

echo "== 1/3 创建远程仓库 $REPO（$VISIBILITY）并推送 main =="
if gh repo view "$REPO" >/dev/null 2>&1; then
  echo "  仓库已存在，仅设置 remote 并推送。"
  git remote add origin "https://github.com/$REPO.git" 2>/dev/null || git remote set-url origin "https://github.com/$REPO.git"
else
  gh repo create "$REPO_NAME" --"$VISIBILITY" --source . --remote origin --push
fi
git push -u origin main

echo ""
echo "== 2/3 启用 main 分支保护（PR + CI + 至少1人 Review）=="
# 分支保护要求 required_status_checks 的 contexts 与 CI 实际检查名一致
# （本项目 CI job 名为 server）。若 GitHub 上检查名不同，
# 用 gh pr checks <PR号> 查看后修改 branch_protection.json。
gh api -X PUT "repos/$REPO/branches/main/protection" \
  --input "$PAYLOAD" >/dev/null
echo "  已启用：必须 PR、CI 全绿、至少 1 人 Approve、禁止 force push。"
echo "  注意：若 .github/CODEOWNERS 中填了真实账号，Code Owner 审查也会自动生效。"

echo ""
echo "== 3/3 设置默认仓库与提示 =="
gh repo set-default "$REPO" || true
echo ""
echo "✔ 完成。后续协作请遵守 CONTRIBUTING.md 的分支与 PR 规范。"
