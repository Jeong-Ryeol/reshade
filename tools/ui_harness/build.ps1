# Builds the UI host exe and (optionally) the harness build of the ReShade DLL, then lays out the run folder.
#   -Dll      also rebuild ReShade64.dll with the harness hook (and online auth unless -NoAuth)
#   -NoAuth   build the DLL without online auth
#   -Content  (re)copy shaders/presets/config from the installed game folder into the run folder
param([switch]$Dll, [switch]$NoAuth, [switch]$Content, [switch]$Rebuild)
$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$run = Join-Path $env:TEMP 'sherbet_uih_run'
$repo = 'C:\claude\reshade'
New-Item -ItemType Directory -Force $run | Out-Null

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$msbuild = & $vswhere -latest -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1

if (-not (Test-Path "$run\host.exe") -or (Get-Item "$here\host.cpp").LastWriteTime -gt (Get-Item "$run\host.exe").LastWriteTime) {
    $vcvars = Join-Path $vsPath 'VC\Auxiliary\Build\vcvars64.bat'
    $obj = Join-Path $env:TEMP 'sherbet_uih_obj'
    New-Item -ItemType Directory -Force $obj | Out-Null
    cmd /c "`"$vcvars`" >nul && cl /nologo /O2 /EHsc /utf-8 /I`"$repo\deps\stb`" `"$here\host.cpp`" /Fo`"$obj\\`" /Fe`"$run\host.exe`" /link /SUBSYSTEM:WINDOWS d3d11.lib user32.lib shell32.lib"
    if ($LASTEXITCODE -ne 0) { throw 'host build failed' }
    'host.exe built'
}

if ($Dll) {
    $env:CL = '/DSHERBET_UI_HARNESS=1' + $(if ($NoAuth) { '' } else { ' /DSHERBET_ONLINE_AUTH=1' })
    $target = if ($Rebuild) { 'ReShade:Rebuild' } else { 'ReShade' }
    & $msbuild "$repo\ReShade.sln" "/t:$target" /p:Configuration=Release '/p:Platform=64-bit' /m /nologo /v:m /clp:ErrorsOnly
    $code = $LASTEXITCODE
    Remove-Item Env:CL
    if ($code -ne 0) { throw 'dll build failed' }
    Copy-Item "$repo\bin\x64\Release\ReShade64.dll" "$run\dxgi.dll" -Force
    'dxgi.dll built: ' + (Get-Item "$run\dxgi.dll").Length
}

if ($Content) {
    $src = Join-Path $env:LOCALAPPDATA 'FiveM\FiveM.app\plugins'
    foreach ($d in 'reshade-shaders', 'Shaders', 'Sherbet-Fx', 'Sherbet-Presets', 'Sherbet-Crosshairs', 'Sherbet-Backgrounds') {
        if (Test-Path "$src\$d") { robocopy "$src\$d" "$run\$d" /E /NFL /NDL /NJH /NJS /NP | Out-Null }
    }
    foreach ($f in 'ReShadePreset.ini', 'sherbet.auth', 'sherbet.themes') { if (Test-Path "$src\$f") { Copy-Item "$src\$f" "$run\$f" -Force } }
    # Config: same as the installed one, but every path points into the run folder and screenshots go to .\shots
    $ini = [IO.File]::ReadAllText("$src\ReShade.ini")
    $ini = $ini.Replace($src, $run)
    $ini = [regex]::Replace($ini, '(?m)^SavePath=.*$', 'SavePath=.\shots')
    [IO.File]::WriteAllText("$run\ReShade.ini", $ini)
    'content copied'
}
