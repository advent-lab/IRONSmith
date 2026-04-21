# build_installer.ps1 - Build the IRONSmith Windows installer.
#
# Usage (run from the repository root):
#   powershell -ExecutionPolicy Bypass -File .\packaging\build_installer.ps1
#   powershell -ExecutionPolicy Bypass -File .\packaging\build_installer.ps1 -Preset dev-release
#
# Qt and Python paths are read automatically from the CMake cache.
# Override them explicitly if needed:
#   -WinDeployQt C:\path\to\windeployqt.exe
#   -PythonHome  C:\path\to\python

param(
    [string]$Preset      = "dev-release",
    [string]$BuildDir    = "",
    [string]$PythonHome  = "",
    [string]$WinDeployQt = "",
    [string]$InnoSetup   = "",
    [switch]$SkipInstall,
    [switch]$SkipPython,
    [switch]$SkipInno
)

$ErrorActionPreference = "Stop"
$RepoRoot   = $PSScriptRoot | Split-Path -Parent
$StagingDir = Join-Path $PSScriptRoot "staging"

# --- Resolve build directory ---
if (-not $BuildDir) {
    $BuildDir = Join-Path $RepoRoot "out\build\$Preset"
}
if (-not (Test-Path $BuildDir)) {
    Write-Error "Build directory not found: $BuildDir`nRun: cmake --build --preset build-$Preset"
}

# --- Read paths from CMake cache ---
function Get-CMakeCacheValue([string]$CacheFile, [string]$Key) {
    $line = Select-String -Path $CacheFile -Pattern "^$Key[=:]" | Select-Object -First 1
    if ($line) {
        return ($line.Line -split '=', 2)[1].Trim()
    }
    return $null
}

$cmakeCache = Join-Path $BuildDir "CMakeCache.txt"
if (Test-Path $cmakeCache) {
    if (-not $WinDeployQt) {
        $WinDeployQt = Get-CMakeCacheValue $cmakeCache "WINDEPLOYQT_EXECUTABLE:FILEPATH"
    }
    if (-not $PythonHome) {
        $pythonExe = Get-CMakeCacheValue $cmakeCache "_Python3_EXECUTABLE:INTERNAL"
        if ($pythonExe -and (Test-Path $pythonExe)) {
            $PythonHome = Split-Path $pythonExe -Parent
        }
    }
}

# --- Step 1: cmake --install ---
if (-not $SkipInstall) {
    Write-Host ""
    Write-Host "=== Step 1: cmake --install ===" -ForegroundColor Cyan
    if (Test-Path $StagingDir) {
        Write-Host "Removing old staging directory..."
        Remove-Item $StagingDir -Recurse -Force
    }
    cmake --install $BuildDir --prefix $StagingDir
    if ($LASTEXITCODE -ne 0) { Write-Error "cmake --install failed (exit $LASTEXITCODE)" }
    Write-Host "Install complete -> $StagingDir"
}

# --- Step 2: windeployqt ---
Write-Host ""
Write-Host "=== Step 2: windeployqt ===" -ForegroundColor Cyan

if (-not $WinDeployQt -or -not (Test-Path $WinDeployQt)) {
    Write-Warning "windeployqt not found. Set -WinDeployQt <path> or ensure Qt bin is on PATH."
    Write-Warning "Skipping Qt deployment - installer may be missing Qt DLLs."
} else {
    Write-Host "Using windeployqt: $WinDeployQt"
    $exePath = Join-Path $StagingDir "bin\ironsmith.exe"
    & $WinDeployQt $exePath --dir (Join-Path $StagingDir "bin") --no-translations --no-system-d3d-compiler
    if ($LASTEXITCODE -ne 0) { Write-Error "windeployqt failed (exit $LASTEXITCODE)" }
    Write-Host "Qt deployment complete."
}

# --- Step 2b: Copy missing MSYS2 runtime DLLs ---
# windeployqt only handles Qt DLLs. IRONSmith DLLs may pull in MSYS2 ucrt64
# runtime DLLs (e.g. libb2-1.dll) that need to be bundled too.
Write-Host ""
Write-Host "=== Step 2b: MSYS2 runtime dependencies ===" -ForegroundColor Cyan

$msysBin = "C:\msys64\ucrt64\bin"
$objdump  = Join-Path $msysBin "objdump.exe"
$stagingBin = Join-Path $StagingDir "bin"

if (-not (Test-Path $objdump)) {
    Write-Warning "objdump.exe not found at $objdump - skipping MSYS2 dependency scan."
} elseif (-not (Test-Path $msysBin)) {
    Write-Warning "MSYS2 ucrt64 bin not found at $msysBin - skipping MSYS2 dependency scan."
} else {
    # Collect all DLL imports recursively from everything in staging/bin
    $pending  = [System.Collections.Generic.Queue[string]]::new()
    $resolved = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)

    Get-ChildItem $stagingBin -Recurse -Include "*.dll","*.exe" | ForEach-Object {
        $pending.Enqueue($_.FullName)
    }

    while ($pending.Count -gt 0) {
        $file = $pending.Dequeue()
        $imports = & $objdump -p $file 2>$null |
            Where-Object { $_ -match "DLL Name:" } |
            ForEach-Object { ($_ -split "DLL Name:")[1].Trim() }

        foreach ($dll in $imports) {
            if ($resolved.Contains($dll)) { continue }
            $resolved.Add($dll) | Out-Null

            # Skip if already in staging or is a Windows system DLL
            if (Test-Path (Join-Path $stagingBin $dll)) { continue }
            $sysPath = Join-Path $env:SystemRoot "System32\$dll"
            $sys32   = Join-Path $env:SystemRoot "SysWOW64\$dll"
            if ((Test-Path $sysPath) -or (Test-Path $sys32)) { continue }

            # Copy from MSYS2 if present
            $src = Join-Path $msysBin $dll
            if (Test-Path $src) {
                Write-Host "  Copying MSYS2 dependency: $dll"
                Copy-Item $src $stagingBin -Force
                $pending.Enqueue((Join-Path $stagingBin $dll))
            }
        }
    }
    Write-Host "MSYS2 dependency scan complete."
}

# --- Step 3: Bundle Python runtime ---
if (-not $SkipPython) {
    Write-Host ""
    Write-Host "=== Step 3: Bundle Python ===" -ForegroundColor Cyan
    if (-not $PythonHome) {
        Write-Error "Python home not found. Pass -PythonHome <path> or ensure CMakeCache.txt has _Python3_EXECUTABLE."
    }
    Write-Host "Python home: $PythonHome"
    $bundleScript = Join-Path $PSScriptRoot "bundle_python.ps1"
    & $bundleScript -Staging $StagingDir -PythonHome $PythonHome
    if ($LASTEXITCODE -ne 0) { Write-Error "Python bundling failed (exit $LASTEXITCODE)" }
}

# --- Step 4: Compile Inno Setup installer ---
if (-not $SkipInno) {
    Write-Host ""
    Write-Host "=== Step 4: Inno Setup ===" -ForegroundColor Cyan

    if (-not $InnoSetup) {
        foreach ($c in @(
            "C:\Program Files (x86)\Inno Setup 6\ISCC.exe",
            "C:\Program Files\Inno Setup 6\ISCC.exe"
        )) {
            if (Test-Path $c) { $InnoSetup = $c; break }
        }
    }

    if (-not $InnoSetup -or -not (Test-Path $InnoSetup)) {
        Write-Host "Inno Setup not found. Downloading and installing..."
        $innoInstaller = Join-Path $env:TEMP "innosetup-installer.exe"
        try {
            Invoke-WebRequest -Uri "https://jrsoftware.org/download.php/is.exe" `
                -OutFile $innoInstaller -UseBasicParsing
            Write-Host "Running Inno Setup installer silently..."
            Start-Process -FilePath $innoInstaller `
                -ArgumentList "/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART", "/SP-" `
                -Wait
            Remove-Item $innoInstaller -Force -ErrorAction SilentlyContinue
        } catch {
            Write-Error "Failed to download or install Inno Setup: $_"
        }
        foreach ($c in @(
            "C:\Program Files (x86)\Inno Setup 6\ISCC.exe",
            "C:\Program Files\Inno Setup 6\ISCC.exe"
        )) {
            if (Test-Path $c) { $InnoSetup = $c; break }
        }
        if (-not $InnoSetup -or -not (Test-Path $InnoSetup)) {
            Write-Error "Inno Setup installation failed or ISCC.exe not found after install."
        }
    }

    if ($InnoSetup -and (Test-Path $InnoSetup)) {
        Write-Host "Using Inno Setup: $InnoSetup"
        Push-Location $PSScriptRoot
        try {
            & $InnoSetup (Join-Path $PSScriptRoot "ironsmith.iss")
            if ($LASTEXITCODE -ne 0) { Write-Error "ISCC.exe failed (exit $LASTEXITCODE)" }
        } finally {
            Pop-Location
        }
        $installer = Get-Item (Join-Path $PSScriptRoot "IRONSmith-*-Setup.exe") -ErrorAction SilentlyContinue
        if ($installer) {
            Write-Host ""
            Write-Host "Installer ready: $($installer.FullName)" -ForegroundColor Green
            Write-Host ("Size: {0:F1} MB" -f ($installer.Length / 1MB))
        }
    }
} else {
    Write-Host ""
    Write-Host "Staging complete. Skipped Inno Setup compilation." -ForegroundColor Yellow
    Write-Host "Staging directory: $StagingDir"
}
