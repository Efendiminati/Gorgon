param(
    [string]$Version = "4.2.0",
    [string]$BinDir = "",
    [string]$OutputDir = "",
    [switch]$Build
)

$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot  = (Resolve-Path (Join-Path $ScriptDir "..\..\..")).Path

if (-not $BinDir) {
    $BinDir = Join-Path $RepoRoot "build\WindowsTesting\Bin\Release"
}

if ($Build) {
    Write-Host "Building GorgonEditor + GorgonPlayer (Release)..."
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    $vsPath = & $vswhere -latest -property installationPath
    $vcvars = Join-Path $vsPath "VC\Auxiliary\Build\vcvars64.bat"
    cmd.exe /c "call `"$vcvars`" >nul && cmake --build `"$RepoRoot\build\WindowsTesting`" --config Release --target GorgonEditor GorgonPlayer"
    if ($LASTEXITCODE -ne 0) { throw "CMake build failed" }
}

foreach ($req in @("GorgonEditor.exe", "GorgonPlayer.exe", "vulkan-1.dll")) {
    $p = Join-Path $BinDir $req
    if (-not (Test-Path $p)) { throw "Missing required binary: $p" }
}

$isccCandidates = @(
    "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe",
    "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
    "$env:ProgramFiles\Inno Setup 6\ISCC.exe"
)
$iscc = $isccCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $iscc) {
    throw "Inno Setup 6 (ISCC.exe) not found. Install with: winget install JRSoftware.InnoSetup"
}

if (-not $OutputDir) {
    $OutputDir = Join-Path $RepoRoot "build\WindowsTesting\Installer"
}
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null

$iss = Join-Path $ScriptDir "GorgonEditor.iss"
& $iscc "/DAppVersion=$Version" "/DBinDir=$BinDir" "/DRepoRoot=$RepoRoot" "/DOutputDir=$OutputDir" $iss
if ($LASTEXITCODE -ne 0) { throw "ISCC failed with exit code $LASTEXITCODE" }

$outExe = Join-Path $OutputDir "GorgonEditor-Setup-$Version-x64.exe"
$hash = (Get-FileHash $outExe -Algorithm SHA256).Hash
$sizeMB = [math]::Round((Get-Item $outExe).Length / 1MB, 2)
Write-Host "Built: $outExe ($sizeMB MB)"
Write-Host "SHA256: $hash"
