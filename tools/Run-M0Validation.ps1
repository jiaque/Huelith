param([string]$BinDirectory = (Join-Path $PSScriptRoot 'bin'))
$ErrorActionPreference = 'Stop'
$bin = (Resolve-Path -LiteralPath $BinDirectory).Path
$reportDirectory = Join-Path $bin '..\validation-results'
New-Item -ItemType Directory -Path $reportDirectory -Force | Out-Null
$reportDirectory = (Resolve-Path -LiteralPath $reportDirectory).Path
$oldPlatform = $env:QT_QPA_PLATFORM
$env:QT_QPA_PLATFORM = 'windows'
$failed = $false
try {
    foreach ($name in @('huelith_unit_tests', 'huelith_m0_tests', 'huelith_m0_gpu_tests', 'huelith_gui_tests')) {
        try {
            & (Join-Path $bin "$name.exe") "--gtest_output=xml:$reportDirectory/$name.xml" 2>&1 |
                Tee-Object -FilePath (Join-Path $reportDirectory "$name.log")
            if ($LASTEXITCODE -ne 0) { $failed = $true }
        } catch {
            $failed = $true
            $_.ToString() | Set-Content -LiteralPath (Join-Path $reportDirectory "$name-error.txt")
        }
    }
    $launches = @()
    foreach ($name in @('Huelith.exe', 'HuelithM0.exe')) {
        try {
            $probeProcess = Start-Process -FilePath (Join-Path $bin $name) -ArgumentList '--smoke-test' -WindowStyle Hidden -PassThru
            $finished = $probeProcess.WaitForExit(15000)
            if (-not $finished) { $probeProcess.Kill(); $failed = $true }
            if ($probeProcess.ExitCode -ne 0) { $failed = $true }
            $launches += [pscustomobject]@{name=$name;started=$true;finished=$finished;exitCode=$probeProcess.ExitCode}
        } catch {
            $failed = $true
            $launches += [pscustomobject]@{name=$name;started=$false;error=$_.ToString()}
        }
    }
    $launches | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $reportDirectory 'launches.json') -Encoding utf8
} finally {
    $env:QT_QPA_PLATFORM = $oldPlatform
}
Write-Output "M0 validation reports: $reportDirectory"
if ($failed) { exit 1 }
