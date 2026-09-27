# 本地 Windows 开发环境检查（C 技术栈）
# 说明：C 后端依赖 Linux 库（libmicrohttpd/libcurl/sqlite3），
#       Windows 上建议使用 WSL（Ubuntu）或直接在课程服务器上开发。
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

Write-Host "== 本地 Windows 开发环境检查（C 技术栈）==" -ForegroundColor Cyan

Check-Tool "git"    { git --version } "未安装 git (https://git-scm.com)"
Check-Tool "gcc"    { gcc --version } "未安装 gcc（Windows 建议安装 WSL 后在其中开发）"
Check-Tool "make"   { make --version } "未安装 make"

Write-Host ""
if ($missing.Count -eq 0) {
    Write-Host "✔ 基础工具齐全。注意：完整构建需在 WSL/课程服务器上完成（依赖 Linux 库）。" -ForegroundColor Green
} else {
    Write-Host ("✘ 缺失组件: {0}" -f ($missing -join ', ')) -ForegroundColor Red
    Write-Host "  建议：启用 WSL（wsl --install）后在 Ubuntu 内按 docs/environment-setup.md 操作。"
}
