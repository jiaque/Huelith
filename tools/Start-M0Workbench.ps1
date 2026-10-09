param([switch]$SelfTest)
$ErrorActionPreference = 'Stop'
$projectDirectory = Split-Path -Parent $PSScriptRoot
$pythonExecutable = 'D:\anconda\envs\codex\python.exe'
if (-not (Test-Path -LiteralPath $pythonExecutable)) {
    throw 'Conda codex environment was not found. Update pythonExecutable in this launcher.'
}
$arguments = @((Join-Path $PSScriptRoot 'm0_workbench.py'))
if ($SelfTest) {
    $arguments += @('--self-test', (Join-Path $projectDirectory 'docs/test-evidence/M0/python-workbench'))
}
& $pythonExecutable @arguments
exit $LASTEXITCODE
