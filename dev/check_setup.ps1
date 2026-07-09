# dev/check_setup.ps1
# Checks this machine against CONTRIBUTING.md's Windows setup steps and
# reports what's missing, with the doc section that fixes it.
#
# Usage: pwsh -File dev/check_setup.ps1   (or: powershell -File dev/check_setup.ps1)

$ErrorActionPreference = 'Continue'
$results = @()

function Add-Result {
    param(
        [string]$Name,
        [bool]$Passed,
        [string]$Detail,
        [string]$Fix,
        [bool]$Optional = $false
    )
    $script:results += [pscustomobject]@{
        Name     = $Name
        Passed   = $Passed
        Detail   = $Detail
        Fix      = $Fix
        Optional = $Optional
    }
}

# --- Step 1: MSVC build tools (CONTRIBUTING.md #1) --------------------------

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) {
    Add-Result 'MSVC build tools' $false 'vswhere.exe not found — the Visual Studio Installer itself is not installed' `
        'Run "winget install Microsoft.VisualStudio.2022.BuildTools", then launch the Visual Studio Installer and select the "Desktop development with C++" workload (CONTRIBUTING.md #1).'
} else {
    # -products * is required: by default vswhere only considers full VS
    # editions (Community/Professional/Enterprise) and silently excludes
    # Build Tools-only installs.
    $vsInstall = & $vswhere -latest -products '*' -property installationPath 2>$null
    if (-not $vsInstall) {
        Add-Result 'MSVC build tools' $false 'vswhere.exe is installed but found no Visual Studio instance' `
            'Launch the Visual Studio Installer and install a VS 2022 edition (or Build Tools) with the "Desktop development with C++" workload (CONTRIBUTING.md #1).'
    } else {
        $msvcRoot = Join-Path $vsInstall 'VC\Tools\MSVC'
        $hasMsvc = (Test-Path $msvcRoot) -and (Get-ChildItem $msvcRoot -ErrorAction SilentlyContinue | Select-Object -First 1)
        if ($hasMsvc) {
            Add-Result 'MSVC build tools' $true "Found at $vsInstall" ''
        } else {
            Add-Result 'MSVC build tools' $false "Visual Studio found at $vsInstall, but no VC\Tools\MSVC toolset is installed" `
                'Launch the Visual Studio Installer and add the "Desktop development with C++" workload (CONTRIBUTING.md #1).'
        }
    }
}

# --- Step 2/4: Qt 6.11.1 + QT_ROOT_DIR (CONTRIBUTING.md #2, #4) -------------

$qtRoot = $env:QT_ROOT_DIR
if (-not $qtRoot) {
    Add-Result 'Qt (QT_ROOT_DIR)' $false 'QT_ROOT_DIR is not set' `
        'Install Qt 6.11.1 (MSVC 2022 64-bit kit) and set the QT_ROOT_DIR user env var (CONTRIBUTING.md #2 and #4).'
} elseif (-not (Test-Path $qtRoot)) {
    Add-Result 'Qt (QT_ROOT_DIR)' $false "QT_ROOT_DIR is set to '$qtRoot' but that folder doesn't exist" `
        'Fix the QT_ROOT_DIR user env var to point at your actual Qt install (CONTRIBUTING.md #4).'
} else {
    if (Test-Path (Join-Path $qtRoot 'bin\Qt6Core.dll')) {
        Add-Result 'Qt (QT_ROOT_DIR)' $true "Found at $qtRoot" ''
    } else {
        Add-Result 'Qt (QT_ROOT_DIR)' $false "QT_ROOT_DIR points at '$qtRoot' but Qt6Core.dll isn't there" `
            'Re-check your Qt install and the QT_ROOT_DIR value (CONTRIBUTING.md #2 and #4).'
    }

    # Mirrors CMakeLists.txt's `find_package(Qt6 REQUIRED COMPONENTS ...)` list,
    # plus WebChannel, a transitive dependency of WebEngineWidgets that Qt's
    # installer treats as a separate, independently-selectable module.
    $qtModules = @(
        @{ Component = 'Svg';              Dll = 'Qt6Svg.dll';              Fix = 'Under Qt 6.11.1 -> MSVC 2022 64-bit, check "Qt SVG"' }
        @{ Component = 'Widgets';          Dll = 'Qt6Widgets.dll';         Fix = 'part of the base Qt 6.11.1 MSVC 2022 64-bit kit' }
        @{ Component = 'Test';             Dll = 'Qt6Test.dll';            Fix = 'part of the base Qt 6.11.1 MSVC 2022 64-bit kit' }
        @{ Component = 'WebEngineWidgets'; Dll = 'Qt6WebEngineWidgets.dll'; Fix = 'under Qt 6.11.1 -> Extensions, check "Qt WebEngine for Qt 6.11.1"' }
        @{ Component = 'WebChannel';       Dll = 'Qt6WebChannel.dll';      Fix = 'under MSVC 2022 64-bit -> Additional Libraries, check "Qt WebChannel" (required dependency of WebEngineWidgets)' }
    )

    foreach ($mod in $qtModules) {
        if (Test-Path (Join-Path $qtRoot "bin\$($mod.Dll)")) {
            Add-Result "Qt $($mod.Component) module" $true 'Found' ''
        } else {
            Add-Result "Qt $($mod.Component) module" $false "$($mod.Dll) not found under QT_ROOT_DIR" `
                "In the Qt install tool, $($mod.Fix) (CONTRIBUTING.md #2)."
        }
    }
}

# --- Step 3/4: vcpkg (CONTRIBUTING.md #3, #4) -------------------------------

$vcpkgRoot = $env:VCPKG_ROOT
if (-not $vcpkgRoot) {
    Add-Result 'vcpkg (VCPKG_ROOT)' $false 'VCPKG_ROOT is not set' `
        'Clone and bootstrap vcpkg, then set the VCPKG_ROOT user env var (CONTRIBUTING.md #3 and #4).'
} elseif (-not (Test-Path (Join-Path $vcpkgRoot 'vcpkg.exe'))) {
    Add-Result 'vcpkg (VCPKG_ROOT)' $false "VCPKG_ROOT is set to '$vcpkgRoot' but vcpkg.exe isn't there" `
        'Run bootstrap-vcpkg.bat in your vcpkg folder (CONTRIBUTING.md #3).'
} else {
    Add-Result 'vcpkg (VCPKG_ROOT)' $true "Found at $vcpkgRoot" ''

    $pathEntries = $env:Path -split ';'
    if ($pathEntries -contains '%VCPKG_ROOT%' -or $pathEntries -contains $vcpkgRoot) {
        Add-Result 'vcpkg on PATH' $true 'Present' ''
    } else {
        Add-Result 'vcpkg on PATH' $false 'VCPKG_ROOT is set, but %VCPKG_ROOT% is not on your user Path' `
            'Add %VCPKG_ROOT% to your user Path variable (CONTRIBUTING.md #4).'
    }
}

# --- Step 4: tools that must resolve on PATH --------------------------------

function Test-CommandOnPath {
    param(
        [string]$Name,
        [string]$Command,
        [string]$Fix,
        [bool]$Optional = $false
    )
    $cmd = Get-Command $Command -ErrorAction SilentlyContinue
    if ($cmd) {
        Add-Result $Name $true "Found at $($cmd.Source)" '' $Optional
    } else {
        Add-Result $Name $false "'$Command' not found on PATH" $Fix $Optional
    }
}

Test-CommandOnPath -Name 'CMake' -Command 'cmake' `
    -Fix 'Add Qt''s bundled CMake (<drive>\<folder>\Qt\Tools\CMake_64\bin) — or a standalone CMake install — to your user Path (CONTRIBUTING.md #4).'

Test-CommandOnPath -Name 'Ninja' -Command 'ninja' `
    -Fix 'Add <drive>\<folder>\Qt\Tools\Ninja to your user Path (CONTRIBUTING.md #4).'

Test-CommandOnPath -Name 'Git' -Command 'git' `
    -Fix 'Install Git for Windows, then make sure its bin/ directory is on your Path (CONTRIBUTING.md #4).'

Test-CommandOnPath -Name 'just' -Command 'just' `
    -Fix 'Install just: "winget install Casey.Just", "scoop install just", or "cargo install just" (Task runner section of CONTRIBUTING.md).'

Test-CommandOnPath -Name 'MinGW gdb' -Command 'gdb' -Optional $true `
    -Fix 'Only needed for the windows-mingw debug preset. Add <drive>\<folder>\Qt\Tools\mingw1310_64\bin to your user Path (CONTRIBUTING.md #4).'

Test-CommandOnPath -Name 'websocat' -Command 'websocat' -Optional $true `
    -Fix 'Only needed for manually debugging poe-info-service''s WebSocket API. Add its folder to your user Path (CONTRIBUTING.md #4, poe-info-service/CONTRIBUTING.md).'

Test-CommandOnPath -Name 'sqlite3' -Command 'sqlite3' -Optional $true `
    -Fix 'Only needed for manually inspecting the SQLite database. Add the sqlite-tools folder to your user Path (CONTRIBUTING.md #4, poe-info-service/CONTRIBUTING.md).'

# --- Report ------------------------------------------------------------------

Write-Host ''
Write-Host 'learn-to-play-poe - dev environment check' -ForegroundColor Cyan
Write-Host '(see CONTRIBUTING.md for full setup instructions)' -ForegroundColor DarkGray
Write-Host ''

$required = $results | Where-Object { -not $_.Optional }
$optional = $results | Where-Object { $_.Optional }

function Write-Section {
    param([string]$Title, $Items)
    if (-not $Items) { return }
    Write-Host $Title -ForegroundColor White
    foreach ($r in $Items) {
        if ($r.Passed) {
            Write-Host "  [OK]   $($r.Name)" -ForegroundColor Green -NoNewline
            Write-Host "  $($r.Detail)" -ForegroundColor DarkGray
        } else {
            Write-Host "  [MISS] $($r.Name)" -ForegroundColor Red -NoNewline
            Write-Host "  $($r.Detail)" -ForegroundColor DarkGray
            Write-Host "         -> $($r.Fix)" -ForegroundColor Yellow
        }
    }
    Write-Host ''
}

Write-Section 'Required' $required
Write-Section 'Optional (manual debugging tools)' $optional

$requiredFailures = @($required | Where-Object { -not $_.Passed })
$passCount = @($required | Where-Object { $_.Passed }).Count

Write-Host "$passCount/$($required.Count) required checks passed." -ForegroundColor Cyan

if ($requiredFailures.Count -gt 0) {
    exit 1
} else {
    Write-Host 'All required tools found. Try `just build`.' -ForegroundColor Green
    exit 0
}
