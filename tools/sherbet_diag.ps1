#Requires -Version 5.1
<#
  Sherbet 설치 진단 — 구매자 PC 에서 한 번 돌리고 결과를 통째로 보내 달라고 한다.

  왜 있는가: "DLL 이 아예 안 붙는다" 는 신고가 들어오면 원격으로 볼 방법이 없다.
  후보(백신 격리 / 32·64비트 어긋남 / 다운로드 깨짐 / 위치 잘못 / MOTW 차단)를
  한 번에 갈라야 왕복이 줄어든다.

  ⚠️ 읽기만 한다. 파일을 고치거나 지우지 않는다.
  ⚠️ 개인정보는 찍지 않는다(사용자명은 경로에 들어갈 수 있어 그대로 둔다).

  쓰는 법 (더블클릭 말고 아래 한 줄):
    powershell -NoProfile -ExecutionPolicy Bypass -File sherbet_diag.ps1

  ⚠️ 결과 파일은 **바탕화면에 저장된다**. 실행한 폴더가 아니다 —
     받는 사람이 "결과가 어디 있냐" 로 한 번 더 왕복하지 않게 여기서 정해 준다.
#>

$ErrorActionPreference = 'Continue'

# 바탕화면(원드라이브 리디렉션 포함)을 실제로 물어본다. 없으면 다운로드, 그것도 없으면 임시폴더.
$deskDir = [Environment]::GetFolderPath('Desktop')
if (-not $deskDir -or -not (Test-Path $deskDir)) { $deskDir = Join-Path $env:USERPROFILE 'Downloads' }
if (-not (Test-Path $deskDir)) { $deskDir = $env:TEMP }
$script:OutFile = Join-Path $deskDir ('sherbet_진단결과_{0}.txt' -f (Get-Date -Format 'MMdd_HHmm'))
# UTF-8 로 시작해 둔다(한글 깨짐 방지).
Set-Content -Path $script:OutFile -Value '' -Encoding UTF8

function Line($s) {
    $t = [string]$s
    Write-Host $t
    Add-Content -Path $script:OutFile -Value $t -Encoding UTF8
}
function Head($s) { Line ""; Line ("=" * 60); Line $s; Line ("=" * 60) }

Head "1. 기본 정보"
Line ("시각      : " + (Get-Date -Format 'yyyy-MM-dd HH:mm:ss'))
$os = Get-CimInstance Win32_OperatingSystem
Line ("OS        : " + $os.Caption)
# ⚠️ 빌드 번호가 핵심이다. 우리 빌드는 VS2026(v145) 로 만드는데, 툴셋이 요구하는
#    최소 Windows 버전보다 낮으면 로더가 DLL 을 **아예 거부한다**(에러도 안 남는다).
#    순정 리쉐이드는 훨씬 낮은 툴셋이라 같은 PC 에서도 잘 붙는다.
Line ("OS 버전   : " + $os.Version + "  빌드 " + $os.BuildNumber)
Line ("아키텍처  : " + $os.OSArchitecture)
$cpu = Get-CimInstance Win32_Processor | Select-Object -First 1
Line ("CPU       : " + $cpu.Name.Trim())
# 없는 명령어를 쓰는 빌드는 로드 시점에 죽는다. 구형 CPU 판별용.
Line ("CPU 세대힌트: " + $cpu.Description)
Line ("PowerShell: " + $PSVersionTable.PSVersion)

Head "2. Sherbet DLL 찾기"
# FiveM 기본 위치 + 사용자가 옮겼을 수 있는 곳까지 훑는다.
$roots = @(
    "$env:LOCALAPPDATA\FiveM\FiveM.app\plugins",
    "$env:LOCALAPPDATA\FiveM\FiveM.app",
    "$env:USERPROFILE\Desktop",
    "$env:USERPROFILE\Downloads"
) | Where-Object { Test-Path $_ }

$found = @()
foreach ($r in $roots) {
    Get-ChildItem -Path $r -Filter 'dxgi.dll*' -File -Force -ErrorAction SilentlyContinue |
        ForEach-Object { $found += $_ }
}
if ($found.Count -eq 0) {
    Line "!! dxgi.dll 을 못 찾았습니다. 백신이 지웠거나 아직 설치 전입니다."
    Line "   확인한 폴더:"
    $roots | ForEach-Object { Line ("     " + $_) }
} else {
    foreach ($f in $found) {
        Line ""
        Line ("경로  : " + $f.FullName)
        Line ("크기  : {0:N0} bytes" -f $f.Length)
        Line ("수정  : " + $f.LastWriteTime)
        try {
            $h = (Get-FileHash $f.FullName -Algorithm SHA256).Hash.ToLower()
            Line ("sha256: " + $h)
        } catch { Line ("sha256: 읽기 실패 - " + $_.Exception.Message) }

        # PE 헤더로 32/64비트 판정. 게임과 안 맞으면 절대 안 붙는다.
        try {
            $fs = [IO.File]::OpenRead($f.FullName)
            $br = New-Object IO.BinaryReader($fs)
            $null = $br.ReadBytes(0x3C)
            $peOff = $br.ReadInt32()
            $fs.Position = $peOff
            $sig = $br.ReadBytes(4)
            $machine = $br.ReadUInt16()
            $br.Close(); $fs.Close()
            $sigOk = ($sig[0] -eq 0x50 -and $sig[1] -eq 0x45)
            $arch = switch ($machine) { 0x8664 { '64비트' } 0x014c { '32비트' } default { ('알수없음 0x{0:X}' -f $machine) } }
            Line ("PE    : 서명 " + $(if ($sigOk) { 'OK' } else { '깨짐(!!)' }) + " / " + $arch)
        } catch { Line ("PE    : 읽기 실패 - " + $_.Exception.Message) }

        # MOTW(인터넷에서 받은 표시). DLL 로드를 막지는 않지만 백신 반응을 부른다.
        $zone = Get-Item -Path ($f.FullName + ':Zone.Identifier') -ErrorAction SilentlyContinue
        Line ("MOTW  : " + $(if ($zone) { '있음(인터넷에서 받은 파일로 표시됨)' } else { '없음' }))
    }
}

Head "3. 게임 실행 파일 비트수"
$exes = Get-ChildItem "$env:LOCALAPPDATA\FiveM\FiveM.app" -Filter '*.exe' -Recurse -File -Force -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -match 'GTAProcess|FiveM' } | Select-Object -First 5
if (-not $exes) { Line "FiveM 실행 파일을 못 찾았습니다." }
foreach ($e in $exes) {
    try {
        $fs = [IO.File]::OpenRead($e.FullName); $br = New-Object IO.BinaryReader($fs)
        $null = $br.ReadBytes(0x3C); $peOff = $br.ReadInt32(); $fs.Position = $peOff
        $null = $br.ReadBytes(4); $m = $br.ReadUInt16(); $br.Close(); $fs.Close()
        Line ("{0,-40} {1}" -f $e.Name, $(switch ($m) { 0x8664 { '64비트' } 0x014c { '32비트' } default { '알수없음' } }))
    } catch { Line ("{0,-40} 읽기 실패" -f $e.Name) }
}

Head "4. ReShade 로그 (붙었는지 여부의 직접 증거)"
$logs = @("$env:LOCALAPPDATA\FiveM\FiveM.app\plugins\ReShade.log",
          "$env:LOCALAPPDATA\FiveM\FiveM.app\ReShade.log") | Where-Object { Test-Path $_ }
if (-not $logs) {
    Line "!! ReShade.log 가 없습니다 = DLL 이 **한 번도 로드되지 않았습니다.**"
} else {
    foreach ($l in $logs) {
        Line ("파일: " + $l + "   수정: " + (Get-Item $l).LastWriteTime)
        Line "--- 첫 3줄 ---"
        Get-Content $l -TotalCount 3 | ForEach-Object { Line $_ }
        Line "--- sherbet 관련 ---"
        Select-String -Path $l -Pattern 'sherbet' -ErrorAction SilentlyContinue |
            Select-Object -Last 15 | ForEach-Object { Line $_.Line }
        Line "--- 마지막 5줄 ---"
        Get-Content $l -Tail 5 | ForEach-Object { Line $_ }
    }
}

Head "4-B. 지금 게임이 켜져 있다면: DLL 이 실제로 물렸는가"
# ⚠️ 로그가 안 갱신되는 이유는 두 가지다 - '게임을 안 켰다' 와 '켰는데 로드 실패' 다.
#    로드에 실패하면 ReShade 가 초기화 전에 죽어서 로그 자체가 안 생기므로,
#    로그만 봐서는 둘을 구분할 수 없다. 실행 중인 프로세스의 모듈 목록이 유일한 답이다.
#    ★ 게임을 **켜 둔 채로** 이 스크립트를 돌려야 이 항목이 의미가 있다.
$gproc = Get-Process -ErrorAction SilentlyContinue | Where-Object { $_.ProcessName -match 'GTAProcess|FiveM' }
if (-not $gproc) {
    Line "게임이 실행 중이 아닙니다 - 이 항목은 건너뜁니다."
    Line "★ 게임을 켠 상태에서 이 스크립트를 다시 돌려 주세요. 그래야 로드 여부가 확정됩니다."
} else {
    foreach ($p in $gproc) {
        Line ""
        Line ("프로세스: {0} (PID {1})  시작 {2}" -f $p.ProcessName, $p.Id, $p.StartTime)
        try {
            $mods = $p.Modules | Where-Object { $_.ModuleName -match '^dxgi\.dll$' }
            if ($mods) {
                foreach ($m in $mods) {
                    Line ("  dxgi.dll 로드됨 : " + $m.FileName)
                    Line ("     제품버전     : " + $m.FileVersionInfo.ProductVersion)
                    Line ("     파일버전     : " + $m.FileVersionInfo.FileVersion)
                }
            } else {
                Line "  !! dxgi.dll 이 이 프로세스에 로드되지 않았습니다 = 로드 실패 확정"
            }
        } catch { Line ("  모듈 목록 조회 실패(권한): " + $_.Exception.Message) }
    }
}

Head "5. 백신 상태 및 격리 이력 (이게 제일 흔한 원인)"
# 실시간 보호가 켜져 있으면 파일이 자리에 있어도 **읽는 순간** 막을 수 있다.
# 그때는 격리 기록이 안 남기도 해서, 상태와 예외 목록을 같이 봐야 한다.
try {
    $mp = Get-MpPreference -ErrorAction Stop
    $st = Get-MpComputerStatus -ErrorAction Stop
    Line ("실시간 보호   : " + $(if ($st.RealTimeProtectionEnabled) { '켜짐 (파일 접근을 막을 수 있음)' } else { '꺼짐' }))
    Line ("변조 방지     : " + $st.IsTamperProtected)
    Line "제외 경로:"
    if ($mp.ExclusionPath) { $mp.ExclusionPath | ForEach-Object { Line ("  " + $_) } } else { Line "  (없음) <- FiveM plugins 폴더를 넣어 주세요" }
} catch { Line ("Defender 상태 조회 실패: " + $_.Exception.Message) }
Line ""
try {
    $det = Get-MpThreatDetection -ErrorAction Stop | Sort-Object InitialDetectionTime -Descending | Select-Object -First 15
    if (-not $det) { Line "Defender 탐지 기록 없음" }
    foreach ($d in $det) {
        $name = try { (Get-MpThreat -ThreatID $d.ThreatID -ErrorAction Stop).ThreatName } catch { '(이름 조회 실패)' }
        Line ("{0}  {1}  {2}" -f $d.InitialDetectionTime, $name, ($d.Resources -join ' | '))
    }
} catch { Line ("Defender 조회 실패(타사 백신일 수 있음): " + $_.Exception.Message) }

Line ""
Line "--- 설치된 백신 ---"
try {
    Get-CimInstance -Namespace root\SecurityCenter2 -ClassName AntiVirusProduct -ErrorAction Stop |
        ForEach-Object { Line ("  " + $_.displayName) }
} catch { Line "  조회 실패" }

Head "5-B. 코드 무결성 / 스마트 앱 컨트롤 (서명 없는 DLL 을 조용히 막는다)"
# ★ 이게 '디펜더 다 껐는데도 안 붙는다' 의 정체다.
#   스마트 앱 컨트롤(Win11)과 WDAC 는 **실시간 보호와 별개**로 동작한다.
#   서명 없는 이미지를 로드 자체에서 거부하고, 오류 메시지도 안 띄운다.
#   그래서 로그 파일조차 안 생긴다. 예외 경로도 안 통한다.
try {
    $ci = Get-ItemProperty -Path 'HKLM:\SYSTEM\CurrentControlSet\Control\CI\Policy' -ErrorAction Stop
    $sac = $ci.VerifiedAndReputablePolicyState
    $sacName = switch ($sac) { 0 { '꺼짐' } 1 { '켜짐 (!! 서명 없는 DLL 차단)' } 2 { '평가 모드 (차단할 수 있음)' } default { "알수없음($sac)" } }
    Line ("스마트 앱 컨트롤 : " + $sacName)
} catch { Line "스마트 앱 컨트롤 : 키 없음(이 윈도우에는 기능이 없거나 꺼짐)" }

try {
    $dg = Get-CimInstance -Namespace root\Microsoft\Windows\DeviceGuard -ClassName Win32_DeviceGuard -ErrorAction Stop
    Line ("메모리 무결성(HVCI): " + ($dg.SecurityServicesRunning -join ',') + "  구성:" + ($dg.SecurityServicesConfigured -join ','))
    Line ("코드무결성 정책    : " + $dg.CodeIntegrityPolicyEnforcementStatus)
} catch { Line "DeviceGuard 조회 실패(정상일 수 있음)" }

Line ""
Line "--- 우리 DLL 의 서명 상태 ---"
foreach ($f in $found) {
    try {
        $sig = Get-AuthenticodeSignature $f.FullName
        Line ("{0,-28} {1}  {2}" -f $f.Name, $sig.Status, $(if ($sig.SignerCertificate) { $sig.SignerCertificate.Subject } else { '(서명자 없음)' }))
    } catch { Line ("{0,-28} 서명 조회 실패" -f $f.Name) }
}

Line ""
Line "--- 코드 무결성 차단 기록 (막혔다면 여기 남는다) ---"
$got = $false
foreach ($ln in @('Microsoft-Windows-CodeIntegrity/Operational','Microsoft-Windows-AppLocker/EXE and DLL')) {
    try {
        $ev = Get-WinEvent -LogName $ln -MaxEvents 20 -ErrorAction Stop |
              Where-Object { $_.Message -match 'dxgi|FiveM' }
        foreach ($e in $ev) { $got = $true; Line ("[{0}] {1} id={2}" -f $ln, $e.TimeCreated, $e.Id); Line ("   " + ($e.Message -split "`n")[0]) }
    } catch { }
}
if (-not $got) { Line "  (관련 차단 기록 없음)" }

Head "6. 참고: 정품 파일 해시 (여기와 다르면 받다가 깨졌거나 다른 파일입니다)"
Line "sherbet-1.9.0  ReShade64.dll  ce6f625cb5aff70df58ee8131c28b5c8d19e3bf38875ad522846b9728e4c3317"
Line "sherbet-1.9.0  ReShade32.dll  1e777adcf1e9c8184ecaa1179ec39acc473bab27e62262b2d54cad5f36de170b"

Head "끝"
Line ""
Line "결과 파일이 여기에 저장됐습니다:"
Line ("  " + $script:OutFile)
Line "이 파일을 통째로 보내 주세요."
Write-Host ""
Write-Host "==================================================" -ForegroundColor Yellow
Write-Host " 결과 파일: $($script:OutFile)" -ForegroundColor Yellow
Write-Host "==================================================" -ForegroundColor Yellow
# 저장 위치를 탐색기로 열어 준다 — 경로를 못 찾아 되묻는 왕복을 없앤다.
try { Start-Process explorer.exe "/select,`"$($script:OutFile)`"" } catch { }
