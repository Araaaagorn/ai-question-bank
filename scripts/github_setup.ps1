# =============================================================================
# GitHub 远程仓库初始化 + main 分支保护（Windows PowerShell 版）
# 前置条件：已安装并登录 gh CLI（gh auth login），且账号对仓库有 admin 权限
# 用法：powershell -ExecutionPolicy Bypass -File scripts\github_setup.ps1 [仓库名] [private|public]
# =============================================================================
param(
    [string]$RepoName = "ai-question-bank",
    [string]$Visibility = "private"
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$Payload = Join-Path $PSScriptRoot "branch_protection.json"

if (-not (Get-Command gh -ErrorAction SilentlyContinue)) {
    throw "未找到 gh CLI，请先安装并登录：gh auth login"
}
$null = gh auth status 2>$null
if ($LASTEXITCODE -ne 0) {
    throw "gh 未登录，请先执行：gh auth login"
}

$Owner = gh api user --jq .login
$Repo = "$Owner/$RepoName"
Set-Location $Root

Write-Host "== 1/3 创建远程仓库 $Repo（$Visibility）并推送 main =="
$exists = gh repo view $Repo 2>$null
if ($LASTEXITCODE -eq 0) {
    Write-Host "  仓库已存在，仅设置 remote 并推送。"
    git remote add origin "https://github.com/$Repo.git" 2>$null
    if ($LASTEXITCODE -ne 0) { git remote set-url origin "https://github.com/$Repo.git" }
} else {
    gh repo create $RepoName --$Visibility --source . --remote origin --push
}
git push -u origin main

Write-Host ""
Write-Host "== 2/3 启用 main 分支保护（PR + CI + 至少1人 Review）=="
gh api -X PUT "repos/$Repo/branches/main/protection" --input $Payload | Out-Null
Write-Host "  已启用：必须 PR、CI 全绿、至少 1 人 Approve、禁止 force push。"

Write-Host ""
Write-Host "== 3/3 设置默认仓库 =="
gh repo set-default $Repo | Out-Null
Write-Host ""
Write-Host "✔ 完成。后续协作请遵守 CONTRIBUTING.md 的分支与 PR 规范。"
