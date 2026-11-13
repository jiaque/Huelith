$ErrorActionPreference = 'Stop'
$repository = Split-Path $PSScriptRoot -Parent
$destination = Join-Path $repository 'build/dependencies/c2patool'
New-Item -ItemType Directory -Force $destination | Out-Null
$archive = Join-Path $destination 'download.zip'
Invoke-WebRequest 'https://github.com/contentauth/c2patool/releases/download/v0.28.4/v0.28.4-x86_64-pc-windows-msvc.zip' -OutFile $archive
if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne '00E5BCFEF95FCF913CC3D67825D7D50EA1EB1180C7FDBEF746C9CE3B65E272F1') {
    throw 'C2PA Tool archive checksum mismatch'
}
Expand-Archive -LiteralPath $archive -DestinationPath $destination -Force
foreach ($license in @('LICENSE-MIT', 'LICENSE-APACHE')) {
    Invoke-WebRequest "https://raw.githubusercontent.com/contentauth/c2patool/v0.28.4/$license" -OutFile (Join-Path $destination "c2patool/$license")
}
Write-Output 'C2PA Tool 0.28.4 is ready. CMake copies only the executable; sample keys are never shipped.'
