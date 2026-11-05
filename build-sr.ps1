<#
.SYNOPSIS
  One-stop build script for freeciv-sr (llvm-mingw + vcpkg + ICU).

.DESCRIPTION
  Replaces build-sr-install-deps.ps1, build-sr-configure.ps1,
  build-sr-compile.ps1, build-sr-stage.ps1.

  Phases (run in order when combined):
    -Deps       Install vcpkg packages (sqlite3 pkgconf). One-time setup.
    -Configure  Wipe build-sr/ and run meson setup.
    -Compile    meson compile -C build-sr + refresh runtime DLLs in build-sr/.
    -Stage      Copy exe + runtime DLLs into dist-sr/, then validate.
    -Full       All of the above (= -Deps -Configure -Compile -Stage).

  Default (no flags): -Compile -Stage (the day-to-day inner loop).

  Path overrides via environment variables (fall back to defaults):
    FREECIV_SR_ROOT   repo root       (default: C:\GIT\sodi\freeciv-3.2.4)
    FREECIV_SR_MINGW  llvm-mingw bin  (default: C:\Qt\Tools\llvm-mingw1706_64\bin)
    VCPKG_ROOT        vcpkg root      (default: C:\GIT\vcpkg)

.EXAMPLE
  .\build-sr.ps1                    # compile + stage (typical edit/run cycle)
  .\build-sr.ps1 -Configure         # reconfigure after adding a source file
  .\build-sr.ps1 -Configure -Compile -Stage
  .\build-sr.ps1 -Full              # from-scratch bootstrap
#>
param(
  [switch]$Deps,
  [switch]$Configure,
  [switch]$Compile,
  [switch]$Stage,
  [switch]$Full
)

$ErrorActionPreference = 'Stop'

# Default = compile + stage if nothing was picked.
if (-not ($Deps -or $Configure -or $Compile -or $Stage -or $Full)) {
  $Compile = $true
  $Stage = $true
}
if ($Full) {
  $Deps = $true; $Configure = $true; $Compile = $true; $Stage = $true
}

function Resolve-Required-Path([string]$envVal, [string]$default,
                               [string]$envName, [string]$label) {
  $v = if ([string]::IsNullOrEmpty($envVal)) { $default } else { $envVal }
  if (-not (Test-Path $v)) {
    throw "$label not found at '$v'. Set `$env:$envName to override."
  }
  return $v
}

$root         = Resolve-Required-Path $env:FREECIV_SR_ROOT  'C:\GIT\sodi\freeciv-3.2.4'         'FREECIV_SR_ROOT'  'freeciv-sr repo root'
$mingwBin     = Resolve-Required-Path $env:FREECIV_SR_MINGW 'C:\Qt\Tools\llvm-mingw1706_64\bin' 'FREECIV_SR_MINGW' 'llvm-mingw bin dir'
$vcpkgRoot    = Resolve-Required-Path $env:VCPKG_ROOT       'C:\GIT\vcpkg'                      'VCPKG_ROOT'       'vcpkg root'

$build        = Join-Path $root 'build-sr'
$dist         = Join-Path $root 'dist-sr'
$vcpkgTriplet = Join-Path $vcpkgRoot 'installed\x64-mingw-static'
$icuRoot      = Join-Path $root 'build-deps\mingw-pkgs\icu-extract\mingw64'
$gccLibs      = Join-Path $root 'build-deps\mingw-pkgs\gcc-libs-extract\mingw64\bin'
$winpthread   = Join-Path $root 'build-deps\mingw-pkgs\winpthread-extract\mingw64\bin'
$extras       = Join-Path $root 'build-deps\runtime-extras'

Set-Location $root

function Initialize-Env {
  # Compose PATH / PKG_CONFIG_PATH used by configure + compile + stage.
  if (-not (Test-Path (Join-Path $mingwBin 'x86_64-w64-mingw32-clang.exe'))) {
    throw "llvm-mingw not found at $mingwBin"
  }
  if (-not (Test-Path $vcpkgTriplet)) {
    throw "vcpkg x64-mingw-static not installed yet: $vcpkgTriplet (run -Deps)"
  }
  if (-not (Test-Path "$icuRoot\lib\pkgconfig\icu-uc.pc")) {
    throw "icu not extracted at $icuRoot"
  }
  $env:PATH            = "$mingwBin;$vcpkgTriplet\tools\pkgconf;$vcpkgTriplet\bin;$icuRoot\bin;$env:PATH"
  $env:PKG_CONFIG_PATH = "$vcpkgTriplet\lib\pkgconfig;$icuRoot\lib\pkgconfig"
  $env:LIBRARY_PATH    = "$vcpkgTriplet\lib;$icuRoot\lib"
  $env:CPATH           = "$vcpkgTriplet\include;$icuRoot\include"

  # vcpkg ships zlib as libzlib.a; meson c.find_library('z') looks for libz.a.
  $libAlias = Join-Path $vcpkgTriplet 'lib\libz.a'
  if (-not (Test-Path $libAlias) -and (Test-Path (Join-Path $vcpkgTriplet 'lib\libzlib.a'))) {
    Copy-Item (Join-Path $vcpkgTriplet 'lib\libzlib.a') $libAlias
  }
  $pkcAlias = Join-Path $vcpkgTriplet 'tools\pkgconf\pkg-config.exe'
  if ((Test-Path (Join-Path $vcpkgTriplet 'tools\pkgconf\pkgconf.exe')) -and -not (Test-Path $pkcAlias)) {
    Copy-Item (Join-Path $vcpkgTriplet 'tools\pkgconf\pkgconf.exe') $pkcAlias
  }
  $env:PKG_CONFIG = $pkcAlias
}

function Copy-RuntimeDLLs([string]$target) {
  # ICU: glob whatever is present so version bumps (icu78 -> icu79 -> ...)
  # don't silently break the stage. If nothing matches, that's a real failure
  # and the validation pass at the end of Stage will catch it.
  Get-ChildItem -Path (Join-Path $icuRoot 'bin') -Filter 'libicu*.dll' -ErrorAction SilentlyContinue |
    ForEach-Object { Copy-Item $_.FullName $target -Force }

  foreach ($dll in 'libgcc_s_seh-1.dll','libstdc++-6.dll','libatomic-1.dll','libgomp-1.dll','libquadmath-0.dll') {
    $src = Join-Path $gccLibs $dll
    if (Test-Path $src) { Copy-Item $src $target -Force }
  }
  $wpt = Join-Path $winpthread 'libwinpthread-1.dll'
  if (Test-Path $wpt) { Copy-Item $wpt $target -Force }

  foreach ($dll in 'exchndl.dll','mgwhelp.dll',
                   'nvdaControllerClient64.dll','nvdaControllerClient.dll',
                   'nvdaControllerClient32.dll') {
    $src = Join-Path $extras $dll
    if (Test-Path $src) {
      Copy-Item $src $target -Force
    } else {
      Write-Host "runtime-extras: $dll missing (skipped)"
    }
  }
}

function Invoke-Deps {
  Write-Host '== [deps] vcpkg install sqlite3 pkgconf =='
  $env:PATH = "$mingwBin;$env:PATH"
  $env:CC  = 'x86_64-w64-mingw32-gcc'
  $env:CXX = 'x86_64-w64-mingw32-g++'
  & (Join-Path $vcpkgRoot 'vcpkg.exe') install sqlite3 pkgconf --triplet x64-mingw-static
  if ($LASTEXITCODE -ne 0) { throw "vcpkg install failed ($LASTEXITCODE)" }
}

function Invoke-Configure {
  Write-Host '== [configure] meson setup build-sr =='
  Remove-Item -Recurse -Force $build -ErrorAction SilentlyContinue
  Initialize-Env
  Write-Host "PKG_CONFIG_PATH=$env:PKG_CONFIG_PATH"
  Write-Host "Compiler: $(Get-Command x86_64-w64-mingw32-clang | Select-Object -ExpandProperty Source)"
  meson setup build-sr `
    --native-file (Join-Path $root 'build-deps\native-mingw.ini') `
    -Dclients=sr `
    "-Dfcmp=[]" `
    "-Dtools=[]" `
    -Dnls=false `
    -Daudio=none `
    -Dreadline=false `
    -Dmwand=false `
    -Dserver=enabled
  if ($LASTEXITCODE -ne 0) { throw "meson setup failed ($LASTEXITCODE)" }
}

function Invoke-Compile {
  Write-Host '== [compile] meson compile -C build-sr =='
  Initialize-Env
  meson compile -C build-sr
  if ($LASTEXITCODE -ne 0) { throw "meson compile failed ($LASTEXITCODE)" }
  # Keep runtime DLLs next to the freshly-built exe so launching
  # build-sr/freeciv-sr.exe directly always works, even after a meson wipe.
  Copy-RuntimeDLLs -target $build
}

function Assert-Stage-Complete {
  $required = @(
    'freeciv-sr.exe',
    'freeciv-sr-tools.exe',
    'freeciv-server.exe',
    'libfreeciv.dll',
    'nvdaControllerClient64.dll'
  )
  $missing = @()
  foreach ($f in $required) {
    $p = Join-Path $dist $f
    if (-not (Test-Path $p)) {
      $missing += "$f (missing)"
    } elseif ((Get-Item $p).Length -eq 0) {
      $missing += "$f (empty)"
    }
  }
  if ($missing.Count -gt 0) {
    throw "Stage validation failed: $($missing -join '; ')"
  }
  Write-Host ("Stage validation: all {0} required files present." -f $required.Count)
}

function Invoke-Stage {
  Write-Host "== [stage] -> $dist =="
  # Clean stray copies left in the repo root by a previous bad invocation.
  # Glob ICU rather than naming versions.
  foreach ($f in 'freeciv-sr.exe', 'libfreeciv.dll') {
    $stray = Join-Path $root $f
    if (Test-Path $stray) { Remove-Item $stray -Force }
  }
  Get-ChildItem -Path $root -Filter 'libicu*.dll' -ErrorAction SilentlyContinue |
    ForEach-Object { Remove-Item $_.FullName -Force }

  New-Item -ItemType Directory -Force -Path $dist | Out-Null

  Copy-Item (Join-Path $build 'freeciv-sr.exe')       $dist -Force
  # Tools exe is a genuine artefact — if meson didn't build it, fail loudly
  # rather than silently stage a stale copy from a prior run.
  Copy-Item (Join-Path $build 'freeciv-sr-tools.exe') $dist -Force
  Copy-Item (Join-Path $build 'freeciv-server.exe')   $dist -Force
  Copy-Item (Join-Path $build 'libfreeciv.dll')       $dist -Force
  Copy-RuntimeDLLs -target $dist

  Write-Host "Staged to ${dist}:"
  Get-ChildItem $dist | Select-Object Name, @{n='MB';e={[math]::Round($_.Length/1MB,2)}} | Format-Table -AutoSize

  Assert-Stage-Complete
}

if ($Deps)      { Invoke-Deps }
if ($Configure) { Invoke-Configure }
if ($Compile)   { Invoke-Compile }
if ($Stage)     { Invoke-Stage }

Write-Host 'Done.'
