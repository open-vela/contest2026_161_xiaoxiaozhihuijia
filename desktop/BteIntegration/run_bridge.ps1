param(
    [string]$Port = 'COM7',
    [switch]$StartApp
)
$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath $PSScriptRoot
$bridgeArgs = @('-X', 'utf8', (Join-Path $PSScriptRoot 'bridge.py'), '--port', $Port)
if ($StartApp) { $bridgeArgs += '--start-app' }
& python @bridgeArgs
exit $LASTEXITCODE
