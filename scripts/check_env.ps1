# 本地 Windows 开发环境检查
# 用法：powershell -ExecutionPolicy Bypass -File scripts\check_env.ps1

$missing = @()

function Check-Tool {
    param([string]$Name, [scriptblock]$Cmd, [string]$Expect)
    try {
        $v = (& $Cmd 2>&1 | Select-Object -First 1) -join ' '
        if ($v -match 'error|无法|not recognized') { throw }
        Write-Host ("  [OK]   {0,-10} {1}" -f $Name, $v) -ForegroundColor Green
    } catch {
        Write-Host ("  [缺失] {0,-10} {1}" -f $Name, $Expect) -ForegroundColor Red
        $script:missing += $Name
    }
}

Write-Host "== 本地 Windows 开发环境检查 ==" -ForegroundColor Cyan

Check-Tool "git"    { git --version } "未安装 git (https://git-scm.com)"
Check-Tool "python" { python --version } "未安装 Python >= 3.10 (https://python.org)"
Check-Tool "node"   { node --version } "未安装 Node >= 18 (https://nodejs.org)"
Check-Tool "npm"    { npm --version } "未安装 npm (随 Node 安装)"

Write-Host ""
if ($missing.Count -eq 0) {
    Write-Host "✔ 环境完整。后端/前端安装命令见 README.md 的『本地 Windows 开发』章节。" -ForegroundColor Green
} else {
    Write-Host ("✘ 缺失组件: {0}" -f ($missing -join ', ')) -ForegroundColor Red
    Write-Host "  安装后重新运行本脚本确认。"
}
