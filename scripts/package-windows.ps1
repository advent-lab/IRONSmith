<#
.SYNOPSIS
    Assembles a portable, unzip-and-run Windows package for IRONSmith.

.DESCRIPTION
    Copies the already-built dev-release binaries into packaging/staging/,
    then bundles everything the app needs that ISN'T copied by the build
    itself: Qt6 runtime DLLs + platform/style plugins (via windeployqt),
    the MSYS2 UCRT64 C/C++ runtime DLLs, and a full Python 3.14 distribution
    (stdlib + site-packages, since the embedded HLIR bridge imports lxml
    and networkx). A small launcher batch file points PYTHONHOME at the
    bundled Python before starting ironsmith.exe, overriding the absolute
    PYTHON_HOME_DIR baked into the binary at build time (HlirBridge.cpp
    checks the PYTHONHOME env var first).

    Requires the project already built via the dev-release preset:
        cmake --preset dev-release
        cmake --build --preset build-dev-release

.PARAMETER BuildDir
    Path to the CMake build tree. Defaults to out/build/dev-release.

.PARAMETER PythonHome
    Path to the Python 3.14 install used by the build. Defaults to the
    per-user install CMake already found (see memory: pythoncore-3.14-64).

.PARAMETER Msys2Root
    Path to the MSYS2 install providing the UCRT64 toolchain/runtime.

.EXAMPLE
    ./scripts/package-windows.ps1
#>
[CmdletBinding()]
param(
    [string]$BuildDir = "$PSScriptRoot/../out/build/dev-release",
    [string]$PythonHome = "$env:LOCALAPPDATA/Python/pythoncore-3.14-64",
    [string]$Msys2Root = "C:/msys64/ucrt64"
)

$ErrorActionPreference = "Stop"

function Resolve-Existing([string]$Path, [string]$Label) {
    $resolved = Resolve-Path -Path $Path -ErrorAction SilentlyContinue
    if (-not $resolved) {
        throw "$Label not found at: $Path"
    }
    return $resolved.Path
}

$BuildDir    = Resolve-Existing $BuildDir "Build directory"
$PythonHome  = Resolve-Existing $PythonHome "Python home"
$Msys2Root   = Resolve-Existing $Msys2Root "MSYS2 UCRT64 root"

$RepoRoot     = Resolve-Existing "$PSScriptRoot/.." "Repo root"
$PackagingDir = Join-Path $RepoRoot "packaging"
$StagingDir   = Join-Path $PackagingDir "staging"
$BinDir       = Join-Path $StagingDir "bin"
$LibDir       = Join-Path $StagingDir "lib/ironsmith"

Write-Host "== IRONSmith Windows packaging ==" -ForegroundColor Cyan
Write-Host "Build dir:    $BuildDir"
Write-Host "Python home:  $PythonHome"
Write-Host "MSYS2 root:   $Msys2Root"
Write-Host "Staging dir:  $StagingDir"
Write-Host ""

# ---------------------------------------------------------------------------
# 1. Clean staging and copy the app's own build output.
# ---------------------------------------------------------------------------
if (Test-Path $StagingDir) {
    Write-Host "Removing previous staging dir..."
    Remove-Item $StagingDir -Recurse -Force
}
New-Item -ItemType Directory -Path $BinDir  -Force | Out-Null
New-Item -ItemType Directory -Path $LibDir  -Force | Out-Null

Write-Host "Copying ironsmith.exe + its own DLLs..."
$srcBin = Join-Path $BuildDir "bin"
Copy-Item (Join-Path $srcBin "ironsmith.exe") $BinDir
# Only the app's own runtime deps - skip the *Tests.exe binaries and the
# example .ironsmith design files that also live in bin/ during dev builds.
Get-ChildItem $srcBin -Filter "*.dll" | Copy-Item -Destination $BinDir

Write-Host "Copying lib/ironsmith (shared libs + plugins)..."
$srcLib = Join-Path $BuildDir "lib/ironsmith"
Copy-Item (Join-Path $srcLib "*") $LibDir -Recurse -Force

# ---------------------------------------------------------------------------
# 2. Qt6 runtime: windeployqt against the exe AND every plugin DLL, so any
#    Qt module a plugin pulls in (that ironsmith.exe itself never directly
#    references, e.g. QtSvg from a plugin's icon handling) still gets
#    detected - not just what the main exe alone would report.
# ---------------------------------------------------------------------------
$windeployqt = Join-Path $Msys2Root "bin/windeployqt6.exe"
if (-not (Test-Path $windeployqt)) {
    throw "windeployqt6.exe not found under $Msys2Root/bin"
}

Write-Host "Running windeployqt..."
$targets = @(Join-Path $BinDir "ironsmith.exe")
$targets += Get-ChildItem $LibDir -Recurse -Filter "*.dll" | ForEach-Object { $_.FullName }

foreach ($t in $targets) {
    & $windeployqt --release --no-translations --no-system-d3d-compiler --no-opengl-sw --dir $BinDir $t
    if ($LASTEXITCODE -ne 0) {
        Write-Warning "windeployqt exited $LASTEXITCODE for $t (continuing - some targets have no Qt deps of their own)"
    }
}

# ---------------------------------------------------------------------------
# 3. MSYS2 UCRT64 runtime DLLs - the GCC/pthread runtime, plus every
#    third-party MinGW library Qt itself was dynamically linked against
#    (freetype, harfbuzz, icu, pcre2, zlib, glib, ...) and Qt6PrintSupport
#    (a QScintilla dependency windeployqt never sees since it's only run
#    against ironsmith.exe and the plugin DLLs, not qscintilla2_qt6.dll).
#    windeployqt has no idea any of this exists - it only knows about
#    official Qt module/plugin DLLs. Found by iterating `objdump -p` over
#    the staged tree to a fixed point (see git history for the process);
#    if a future Qt/QScintilla upgrade adds a new transitive dependency,
#    re-run that same objdump sweep rather than guessing at the list.
# ---------------------------------------------------------------------------
Write-Host "Copying MSYS2 UCRT64 runtime + third-party library DLLs..."
$runtimeDlls = @(
    "libgcc_s_seh-1.dll", "libstdc++-6.dll", "libwinpthread-1.dll",
    "Qt6PrintSupport.dll",
    "libb2-1.dll", "libbrotlicommon.dll", "libbrotlidec.dll", "libbz2-1.dll",
    "libdouble-conversion.dll", "libffi-8.dll", "libfreetype-6.dll",
    "libgio-2.0-0.dll", "libglib-2.0-0.dll", "libgmodule-2.0-0.dll",
    "libgobject-2.0-0.dll", "libgraphite2.dll", "libharfbuzz-0.dll",
    "libiconv-2.dll", "libicudt77.dll", "libicuin77.dll", "libicuuc77.dll",
    "libintl-8.dll", "libjpeg-8.dll", "libmd4c.dll",
    "libpcre2-8-0.dll", "libpcre2-16-0.dll", "libpng16-16.dll",
    "libzstd.dll", "zlib1.dll"
)
foreach ($dll in $runtimeDlls) {
    $src = Join-Path $Msys2Root "bin/$dll"
    if (Test-Path $src) {
        Copy-Item $src $BinDir
    } else {
        Write-Warning "Runtime DLL not found, skipping: $dll"
    }
}

# ---------------------------------------------------------------------------
# 4. Python: stdlib + site-packages (lxml, networkx - the HLIR bridge's own
#    Python-side imports) into staging/python, plus the MSVC runtime DLLs
#    this python.org build needs. The launcher sets PYTHONHOME to this
#    folder so HlirBridge.cpp's getenv("PYTHONHOME") check picks it up
#    instead of the absolute path baked in at build time.
# ---------------------------------------------------------------------------
Write-Host "Copying Python 3.14 distribution (this can take a minute - stdlib + site-packages)..."
$PyDestRoot = Join-Path $StagingDir "python"
New-Item -ItemType Directory -Path $PyDestRoot -Force | Out-Null

Copy-Item (Join-Path $PythonHome "Lib") $PyDestRoot -Recurse -Force
if (Test-Path (Join-Path $PythonHome "DLLs")) {
    Copy-Item (Join-Path $PythonHome "DLLs") $PyDestRoot -Recurse -Force
}
foreach ($f in @("vcruntime140.dll", "vcruntime140_1.dll")) {
    $src = Join-Path $PythonHome $f
    if (Test-Path $src) {
        Copy-Item $src $BinDir
    }
}
# python.exe itself - CodeGenBridge shells out to it (via CODEGEN_PYTHON_EXECUTABLE)
# to run aiecad_compiler/main.py as a subprocess, separate from the embedded
# interpreter HlirBridge runs in-process. Not staged anywhere else.
Copy-Item (Join-Path $PythonHome "python.exe") $PyDestRoot -Force

# ---------------------------------------------------------------------------
# 5. Bridge resources: the embedded Python interpreter and the standalone
#    codegen path both bake in absolute dev-machine paths at compile time
#    (HLIR_BRIDGE_PYTHON_DIR, HLIR_AIECAD_DIR, CODEGEN_MAIN_PY,
#    IRONSMITH_BUILTIN_KERNELS_DIR). HlirBridge.cpp/CodeGenBridge.cpp/
#    KernelRegistryService.cpp now check an env var of the same name first
#    (mirroring the existing PYTHONHOME override), so stage our own copies
#    here and point those env vars at them from the launcher instead of
#    relying on the dev tree existing on the target machine.
# ---------------------------------------------------------------------------
Write-Host "Copying HLIR bridge Python module, aiecad_compiler, and builtin kernels..."
$ShareDir = Join-Path $StagingDir "share/ironsmith"
New-Item -ItemType Directory -Path $ShareDir -Force | Out-Null

$bridgePySrc = Join-Path $BuildDir "hlir_cpp_bridge/python"
Copy-Item $bridgePySrc (Join-Path $ShareDir "hlir_bridge_python") -Recurse -Force
Remove-Item (Join-Path $ShareDir "hlir_bridge_python/__pycache__") -Recurse -Force -ErrorAction SilentlyContinue

Copy-Item (Join-Path $RepoRoot "src/aiecad_compiler") (Join-Path $ShareDir "aiecad_compiler") -Recurse -Force
Get-ChildItem (Join-Path $ShareDir "aiecad_compiler") -Recurse -Filter "__pycache__" -Directory |
    Remove-Item -Recurse -Force -ErrorAction SilentlyContinue

Copy-Item (Join-Path $RepoRoot "resources/kernels") (Join-Path $ShareDir "kernels") -Recurse -Force

# ---------------------------------------------------------------------------
# 6. Workspace: bundled example designs + an empty scratch folder for
#    attendees' own work. The launcher below starts ironsmith.exe with this
#    directory as the working directory, and ProjectExplorerDataSource
#    defaults its root path to the process CWD on first run (no saved
#    setting yet - true for every attendee's machine), so both folders show
#    up immediately in the Project Explorer without anyone needing to
#    navigate via the Open Folder dialog first.
# ---------------------------------------------------------------------------
Write-Host "Copying example designs and creating sandbox_designs..."
$WorkspaceDir = Join-Path $StagingDir "workspace"
New-Item -ItemType Directory -Path $WorkspaceDir -Force | Out-Null
Copy-Item (Join-Path $RepoRoot "Example_Designs") $WorkspaceDir -Recurse -Force
New-Item -ItemType Directory -Path (Join-Path $WorkspaceDir "sandbox_designs") -Force | Out-Null

# ---------------------------------------------------------------------------
# 7. Launcher.
# ---------------------------------------------------------------------------
Write-Host "Writing launcher..."
$launcher = @'
@echo off
setlocal
set "HERE=%~dp0"
set "PATH=%HERE%bin;%PATH%"
set "PYTHONHOME=%HERE%python"
set "HLIR_BRIDGE_PYTHON_DIR=%HERE%share/ironsmith/hlir_bridge_python"
set "HLIR_AIECAD_DIR=%HERE%share/ironsmith/aiecad_compiler"
set "CODEGEN_MAIN_PY=%HERE%share/ironsmith/aiecad_compiler/main.py"
set "CODEGEN_PYTHON_EXECUTABLE=%HERE%python\python.exe"
set "PYTHON_EXECUTABLE=%HERE%python\python.exe"
set "IRONSMITH_BUILTIN_KERNELS_DIR=%HERE%share/ironsmith/kernels"
cd /d "%HERE%workspace"
start "" "%HERE%bin\ironsmith.exe" %*
'@
Set-Content -Path (Join-Path $StagingDir "IRONSmith.bat") -Value $launcher -Encoding ASCII

# ---------------------------------------------------------------------------
# 6. Self-check: recursively walk every DLL/exe actually staged in bin/ and
#    flag any imported DLL that isn't either bundled or a known Windows OS
#    DLL. Catches the exact class of gap windeployqt misses (transitive
#    third-party MinGW deps) so a future Qt/QScintilla bump that adds a new
#    one fails loudly here instead of shipping a package that crashes with
#    STATUS_DLL_NOT_FOUND on a clean machine.
# ---------------------------------------------------------------------------
Write-Host "Verifying no unbundled non-system DLL dependencies remain..."
$objdump = Join-Path $Msys2Root "bin/objdump.exe"
$safePatterns = @(
    '^api-ms-win-',
    ('^(KERNEL32|USER32|GDI32|ADVAPI32|SHELL32|SHLWAPI|SHCORE|OLE32|OLEAUT32|COMDLG32|COMCTL32|' +
     'WS2_32|WINMM|WINHTTP|WTSAPI32|SETUPAPI|CRYPT32|BCRYPT|NCRYPT|AUTHZ|SECUR32|NETAPI32|USERENV|' +
     'VERSION|MPR|DNSAPI|IPHLPAPI|DWMAPI|UXTHEME|IMM32|D3D9|D3D11|D3D12|DXGI|DWRITE|MSVCRT|NTDLL|' +
     'RPCRT4|PSAPI|GDIPLUS|USP10)\.dll$'),
    '^WINSPOOL\.DRV$'
)

$stagedFiles = Get-ChildItem $BinDir -Recurse -Include "*.dll", "*.exe"
$presentSet  = ($stagedFiles | ForEach-Object { $_.Name.ToLower() }) | Sort-Object -Unique
$allImports  = @{}
foreach ($f in $stagedFiles) {
    (& $objdump -p $f.FullName 2>$null | Select-String "DLL Name") | ForEach-Object {
        $name = ($_ -replace ".*DLL Name:\s*", "").Trim()
        if (-not $allImports.ContainsKey($name)) { $allImports[$name] = [System.Collections.Generic.List[string]]::new() }
        $allImports[$name].Add($f.Name)
    }
}

$stillMissing = @()
foreach ($name in $allImports.Keys) {
    if ($presentSet -contains $name.ToLower()) { continue }
    $isSafe = $false
    foreach ($p in $safePatterns) { if ($name -match $p) { $isSafe = $true; break } }
    if (-not $isSafe) { $stillMissing += "$name  <-- needed by: $($allImports[$name] -join ', ')" }
}

if ($stillMissing.Count -gt 0) {
    Write-Warning "Unbundled non-system DLL dependencies found - the package will likely fail to launch on a clean machine:"
    $stillMissing | Sort-Object | ForEach-Object { Write-Warning "  $_" }
    Write-Warning "Add these to `$runtimeDlls above (from $Msys2Root/bin) and re-run."
} else {
    Write-Host "Clean - every non-system DLL dependency is bundled." -ForegroundColor Green
}

# ---------------------------------------------------------------------------
# 7. Zip it.
# ---------------------------------------------------------------------------
$zipPath = Join-Path $PackagingDir "IRONSmith-Windows.zip"
if (Test-Path $zipPath) { Remove-Item $zipPath -Force }
Write-Host "Compressing to $zipPath ..."
Compress-Archive -Path (Join-Path $StagingDir "*") -DestinationPath $zipPath -CompressionLevel Optimal

$zipSize = (Get-Item $zipPath).Length / 1MB
Write-Host ""
Write-Host "Done. Package: $zipPath ($([math]::Round($zipSize, 1)) MB)" -ForegroundColor Green
if ($stillMissing.Count -gt 0) {
    Write-Host "WARNING: dependency check above found gaps - do not distribute until fixed." -ForegroundColor Red
} else {
    Write-Host "Dependency check passed and a minimal-PATH launch test succeeded during development." -ForegroundColor Green
    Write-Host "Still worth a real test on an actual machine without MSYS2/Qt/Python installed before the workshop." -ForegroundColor Yellow
}
