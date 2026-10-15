param([Parameter(Mandatory=$true)][string]$Executable)
$ErrorActionPreference = 'Stop'
$app = (Resolve-Path -LiteralPath $Executable).Path
if ($app.Contains('"') -or $app.Contains("`r") -or $app.Contains("`n")) { throw 'Invalid executable path' }
$root = 'HKCU:\Software\Classes\tonedock'
New-Item -Path $root -Force | Out-Null
Set-Item -Path $root -Value 'URL:ToneDock protocol'
New-ItemProperty -Path $root -Name 'URL Protocol' -Value '' -Force | Out-Null
$command = New-Item -Path "$root\shell\open\command" -Force
Set-Item -Path $command.PSPath -Value ('"' + $app + '" "%1"')
Write-Output 'Registered tonedock:// for the current user.'
