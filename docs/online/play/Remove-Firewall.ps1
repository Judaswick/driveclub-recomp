# Undoes Allow-Firewall.bat and Allow-Firewall-Internet.bat (Windows Firewall, inbound).
# Double-click Remove-Firewall.bat; Windows asks once for admin rights.
# Removes every "DriveClub Online" rule and switches the rules Windows made for the game and the server back on that
# Allow-Firewall switched off (firewall-switched-off.txt; without that file, from an older Allow-Firewall: every
# switched-off inbound rule of DriveClubGame.exe or driveclubsrv.exe). Windows then asks again "allow access?" the next
# time the game or the server is reached from another PC, as before Allow-Firewall.
param([string]$Root = $PSScriptRoot)

if (-not ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
        [Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Start-Process powershell -Verb RunAs -ArgumentList "-NoProfile -ExecutionPolicy Bypass -File `"$PSCommandPath`" -Root `"$Root`""
    exit
}

$removed = @(Get-NetFirewallRule -DisplayName 'DriveClub Online*' -ErrorAction SilentlyContinue)
foreach ($r in $removed) {
    Write-Host "Removed: $($r.DisplayName)"
}
$removed | Remove-NetFirewallRule

$switchedOff = Join-Path $Root 'firewall-switched-off.txt'
$names = @()
if (Test-Path $switchedOff) {
    $names = Get-Content $switchedOff | Where-Object { $_.Trim() } | Select-Object -Unique
} else {
    foreach ($f in Get-NetFirewallApplicationFilter | Where-Object { $_.Program -match '(driveclubsrv|DriveClubGame)\.exe$' }) {
        $r = $f | Get-NetFirewallRule
        if ($r.Direction -eq 'Inbound' -and $r.Action -eq 'Allow' -and $r.Enabled -eq 'False') { $names += $r.Name }
    }
}
foreach ($name in $names) {
    $r = Get-NetFirewallRule -Name $name -ErrorAction SilentlyContinue
    if ($r) {
        Enable-NetFirewallRule -Name $name
        Write-Host "Switched on again: $($r.DisplayName)"
    }
}
if (Test-Path $switchedOff) { Remove-Item $switchedOff }

if (-not $removed -and -not $names) { Write-Host 'Nothing to undo: no DriveClub Online rules were found.' }
else { Write-Host 'Done: the firewall is as before Allow-Firewall.' }
Start-Sleep 4
