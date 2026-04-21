# bundle_python.ps1 - Stage the Python runtime and pip dependencies for packaging.
#
# Usage:
#   .\packaging\bundle_python.ps1 -Staging <staging_dir> [-PythonHome <path>]
#
# Parameters:
#   -Staging    Root staging directory (e.g. packaging/staging)
#   -PythonHome Path to the Python installation to bundle.
#               Defaults to the Python found on PATH at invocation time.

param(
    [Parameter(Mandatory=$true)]
    [string]$Staging,

    [string]$PythonHome = ""
)

$ErrorActionPreference = "Stop"

# Resolve Python home
if (-not $PythonHome) {
    $pythonExe = (Get-Command python -ErrorAction Stop).Source
    $PythonHome = Split-Path $pythonExe -Parent
}

if (-not (Test-Path $PythonHome)) {
    Write-Error "Python home not found: $PythonHome"
    exit 1
}

$pythonExe = Join-Path $PythonHome "python.exe"
if (-not (Test-Path $pythonExe)) {
    Write-Error "python.exe not found in: $PythonHome"
    exit 1
}

Write-Host "Bundling Python from: $PythonHome"
Write-Host "Staging to:           $Staging"

$stagingPython = Join-Path $Staging "python"
New-Item -ItemType Directory -Force -Path $stagingPython | Out-Null

# Copy the Python executable
Write-Host "Copying python.exe..."
Copy-Item "$PythonHome\python.exe" "$stagingPython\python.exe" -Force

# Copy DLLs (python3xx.dll, vcruntime, etc.)
Write-Host "Copying Python DLLs..."
Get-ChildItem "$PythonHome\*.dll" | ForEach-Object {
    Copy-Item $_.FullName "$stagingPython\" -Force
}

# Copy the standard library (Lib/)
Write-Host "Copying standard library..."
$libSrc = Join-Path $PythonHome "Lib"
$libDst = Join-Path $stagingPython "Lib"
if (Test-Path $libSrc) {
    # Robocopy: /E = include subdirs, /XD = exclude dirs, /NFL /NDL = suppress file/dir listing
    robocopy $libSrc $libDst /E /XD "__pycache__" "test" "tests" /XF "*.pyc" /NFL /NDL /NJH /NJS | Out-Null
} else {
    Write-Warning "Python Lib/ directory not found at: $libSrc"
}

# Copy compiled extension modules (DLLs/)
Write-Host "Copying extension DLLs..."
$dllsSrc = Join-Path $PythonHome "DLLs"
$dllsDst = Join-Path $stagingPython "DLLs"
if (Test-Path $dllsSrc) {
    robocopy $dllsSrc $dllsDst /E /NFL /NDL /NJH /NJS | Out-Null
}

# Install pip dependencies into site-packages
Write-Host "Installing pip dependencies..."
$sitePackages = Join-Path $stagingPython "Lib\site-packages"
New-Item -ItemType Directory -Force -Path $sitePackages | Out-Null

$requirements = @(
    "PyYAML>=6.0.2",
    "numpy>=1.19.5,<2.0",
    "ml_dtypes",
    "aiofiles",
    "rich"
)

foreach ($pkg in $requirements) {
    Write-Host "  Installing $pkg ..."
    & $pythonExe -m pip install $pkg --target $sitePackages --no-cache-dir --quiet
    if ($LASTEXITCODE -ne 0) {
        Write-Warning "pip install failed for: $pkg (exit $LASTEXITCODE)"
    }
}

# Strip unnecessary files to reduce installer size
Write-Host "Stripping unnecessary files..."
@("*.dist-info", "*.egg-info") | ForEach-Object {
    Get-ChildItem $sitePackages -Filter $_ -Recurse | Remove-Item -Recurse -Force -ErrorAction SilentlyContinue
}

$pythonSize = (Get-ChildItem $stagingPython -Recurse | Measure-Object -Property Length -Sum).Sum / 1MB
Write-Host ("Python bundle size: {0:F1} MB" -f $pythonSize)
Write-Host "Python bundling complete."
