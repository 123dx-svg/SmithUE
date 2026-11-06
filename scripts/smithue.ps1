<#
smithue.ps1 — 纯 Windows / 零 node 的 SmithUE 调用（用 PowerShell 自带 Invoke-RestMethod）

为什么存在：smithue-cli 是 node 写的客户端；但你的插件只是个本地 HTTP 服务端
（127.0.0.1:<port>/api/v1/execute）。任何 HTTP 客户端都能调它——本脚本用
Windows 自带的 PowerShell，不需要装 node / npm。

用法：
  .\smithue.ps1 <command> <params.json>    # 从文件读参数（UTF-8，最稳，中文也不坏）
  .\smithue.ps1 <command> '{"k":"v"}'      # inline JSON（ASCII 安全时）
  .\smithue.ps1 <command>                  # 无参数 = {}

环境变量：
  $env:SMITHUE_PORT      跳过端口发现，直连
  $env:SMITHUE_PROJECT   exact project path or name
  $env:SMITHUE_PID       unique editor process
  $env:SMITHUE_SESSION   reuse a CLI session binding

说明：<command> 是 exec 工具名（ping / list_assets / get_job_status / move_folder …），
等价于 `smithue-cli exec <command>`。
#>
param(
  [Parameter(Mandatory = $true)][string]$Command,
  [string]$Params = '{}'
)
$ErrorActionPreference = 'Stop'

# ---- Discover an unambiguous or session-bound editor ----
if ($env:SMITHUE_PORT) {
  if ($env:SMITHUE_SESSION -or $env:SMITHUE_PID -or $env:SMITHUE_PROJECT) {
    throw 'SMITHUE_PORT cannot be combined with a session, PID or project target.'
  }
  $port = [int]$env:SMITHUE_PORT
  if ($port -lt 1 -or $port -gt 65535) { throw 'Invalid SMITHUE_PORT.' }
}
else {
  $dir = Join-Path $env:LOCALAPPDATA '.smithue'
  $files = Get-ChildItem $dir -Filter *.port -ErrorAction SilentlyContinue
  $entries = foreach ($f in $files) {
    try {
      $j = Get-Content -LiteralPath $f.FullName -Raw -Encoding UTF8 | ConvertFrom-Json
      $j | Add-Member -NotePropertyName _mtime -NotePropertyValue $f.LastWriteTime -Force
      $j
    } catch {}
  }
  if ($env:SMITHUE_PROJECT) {
    $entries = $entries | Where-Object {
      "$($_.project_name)" -eq $env:SMITHUE_PROJECT -or
      [IO.Path]::GetFullPath($_.project) -eq [IO.Path]::GetFullPath($env:SMITHUE_PROJECT)
    }
  }
  if ($env:SMITHUE_PID) {
    $entries = $entries | Where-Object { $_.pid -eq [int]$env:SMITHUE_PID }
  }
  if ($env:SMITHUE_SESSION -and -not $env:SMITHUE_PID -and -not $env:SMITHUE_PROJECT) {
    $sha = [Security.Cryptography.SHA256]::Create()
    try {
      $key = [BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($env:SMITHUE_SESSION))).Replace('-', '').ToLowerInvariant()
    } finally { $sha.Dispose() }
    $binding = Join-Path (Join-Path $dir 'sessions') "$key.json"
    $pinned = (Get-Content -LiteralPath $binding -Raw -Encoding UTF8 | ConvertFrom-Json).pinned
    if (-not $pinned) { throw 'Session has no editor binding. Explicitly bind a PID or project first.' }
    $entries = $entries | Where-Object {
      $_.pid -eq $pinned.pid -and $_.port -eq $pinned.port -and
      $_.instance_id -eq $pinned.instance_id -and
      [IO.Path]::GetFullPath($_.project) -eq [IO.Path]::GetFullPath($pinned.project)
    }
  }
  if (-not $entries) {
    Write-Error "No running SmithUE editor found (port dir: $dir). Start UE with the SmithUE plugin, or set `$env:SMITHUE_PORT."
    exit 1
  }
  if (@($entries).Count -ne 1) { throw 'Multiple editors match. Select a unique SMITHUE_PID or bind SMITHUE_SESSION.' }
  $instance = @($entries)[0]
  $port = $instance.port
}

$ready = Invoke-RestMethod -Uri "http://127.0.0.1:$port/ready" -TimeoutSec 10
if (-not $ready.pid -or -not $ready.instance_id) {
  throw 'Target lacks verifiable instance identity. Update its plugin.'
}
if (-not $ready.ready) { throw 'Target editor is not ready.' }
if ($instance -and $null -ne $ready.pid -and $ready.pid -ne $instance.pid) {
  throw 'Target editor identity mismatch; refusing to execute.'
}
if ($instance -and $instance.instance_id -and $instance.instance_id -ne $ready.instance_id) {
  throw 'Target editor instance changed; explicitly rebind.'
}
$guardHeaders = @{
  'X-SmithUE-Expected-Pid' = [string]$ready.pid
  'X-SmithUE-Expected-Instance' = [string]$ready.instance_id
}

# ---- 参数来源：文件路径（UTF-8）或 inline JSON ----
if ($Params -and (Test-Path -LiteralPath $Params)) {
  $paramsJson = Get-Content -LiteralPath $Params -Raw -Encoding UTF8
}
else {
  $paramsJson = $Params
}
if (-not "$paramsJson".Trim()) { $paramsJson = '{}' }
$paramsJson = "$paramsJson".TrimStart([char]0xFEFF)  # strip BOM

# ---- 拼 body（params 原样嵌入，不二次序列化）→ UTF-8 字节发送 ----
$full = '{"command":"' + $Command + '","params":' + $paramsJson + '}'
$bytes = [System.Text.Encoding]::UTF8.GetBytes($full)

$resp = Invoke-RestMethod -Uri "http://127.0.0.1:$port/api/v1/execute" `
  -Method Post -ContentType 'application/json; charset=utf-8' -Body $bytes -Headers $guardHeaders
$resp | ConvertTo-Json -Depth 30 -Compress
