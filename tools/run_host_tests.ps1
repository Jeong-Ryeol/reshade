# Sherbet 호스트 테스트 일괄 빌드·실행 (Windows / MSVC)
#
# 맥에서는 clang 으로 돌리던 스위트다. 윈도우에서는 clang 없이 cl.exe 로 그대로 돈다.
# 컴파일이 된다고 해서 "밤 11시 50분에 딱 한 번 울린다" 나 "FHD·2K·4K 가 같은 자리를
# 가리킨다" 가 검증되지는 않는다 — 그건 여전히 이 스위트만 잡아낸다. msbuild 와 별개로 돌릴 것.
#
#   사용법:  powershell -ExecutionPolicy Bypass -File tools\run_host_tests.ps1
#   실패하면 종료코드 1.

$ErrorActionPreference = 'Stop'
$repo = Resolve-Path "$PSScriptRoot\.."
$out  = Join-Path $env:TEMP 'sherbet_hosttests'
New-Item -ItemType Directory -Force $out | Out-Null

# cl.exe 는 PATH 에 없다. vcvars64.bat 를 한 번 부르고 그 환경변수를 이 세션에 끌어온다.
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) { throw "vswhere 를 찾지 못했습니다. Visual Studio(C++ 워크로드) 가 필요합니다." }
$vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsPath) { throw "Visual Studio C++ 워크로드를 찾지 못했습니다." }
$vcvars = Join-Path $vsPath 'VC\Auxiliary\Build\vcvars64.bat'

cmd /c "`"$vcvars`" >nul && set" | ForEach-Object {
    if ($_ -match '^([^=]+)=(.*)$') { Set-Item -Path "env:$($matches[1])" -Value $matches[2] -ErrorAction SilentlyContinue }
}

$flags = @(
    '/nologo', '/std:c++17', '/EHsc', '/utf-8',
    '/DIMGUI_DISABLE_OBSOLETE_FUNCTIONS', '/DIMGUI_DEFINE_MATH_OPERATORS',
    "/I$repo\deps\imgui", "/I$repo\source"
)

$pass = 0; $fail = 0
foreach ($src in Get-ChildItem "$repo\tools\sherbet_*_test.cpp" | Sort-Object Name) {
    $name = $src.BaseName
    $buildLog = "$out\$name.build.log"
    $runLog   = "$out\$name.run.log"

    & cl @flags $src.FullName "/Fo$out\" "/Fe$out\$name.exe" > $buildLog 2>&1
    if ($LASTEXITCODE -ne 0) {
        Write-Host "[BUILD FAIL] $name   -> $buildLog" -ForegroundColor Red
        $fail++
        continue
    }

    & "$out\$name.exe" > $runLog 2>&1
    if ($LASTEXITCODE -ne 0) {
        Write-Host "[RUN FAIL  ] $name   -> $runLog" -ForegroundColor Red
        $fail++
        continue
    }

    Write-Host "[PASS      ] $name" -ForegroundColor Green
    $pass++
}

Write-Host ""
Write-Host "  통과 $pass / 실패 $fail    (로그: $out)"
exit $(if ($fail -gt 0) { 1 } else { 0 })
