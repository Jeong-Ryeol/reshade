# Sherbet 자동 업데이트 설계

작성일 2026-07-29 · 대상 브랜치 `sherbet-base`

새 기능을 추가해도 구매자가 DLL을 수동으로 교체하지 않고 최신 버전을 받게 한다.
지금은 콘텐츠(테마/프리셋/fx)만 서버에서 자동 배포되고 코드는 수동 배포다. 이 비대칭을 없앤다.

---

## 0. 확정 사항

사용자가 결정한 것. 설계는 이 안에서 최선을 찾는다.

| # | 결정 |
|---|---|
| 1 | **UX = 알림 후 버튼 클릭.** 오버레이에 새 버전 + 변경내역을 띄우고 구매자가 [업데이트]를 눌러야 다운로드·교체. 무음 자동 아님 |
| 2 | **호스팅 = 하이브리드.** 매니페스트는 홈서버, DLL 바이너리는 GitHub Releases |
| 3 | **안전망 = sha256 검증 + 원자적 교체 + `.bak` 보관 + 부팅마커 자동 롤백**(2회 연속 부팅 실패 시 자동 복원) |
| 4 | **발행 = 명시적 태그 푸시 때만.** 평소 커밋 푸시는 CI 빌드 검증만, 구매자에겐 알림 없음 |
| 5 | 실행 중인 DLL은 덮어쓸 수 없으므로 **게임 재시작 1회 후 적용** |
| 6 | **전원 공용 빌드로 통일.** 개인화는 서버 주도(닉네임 릴레이·전용 프리셋 배포는 이미 구현됨). 노드락의 서버측 대체는 후속 |
| 7 | **GitHub Releases 자산 공개 수용.** 소스가 이미 PUBLIC이고 `ONLINE_AUTH=1`이라 비구매자에겐 잠긴 채로만 뜬다 |

## 0.1 이 설계가 전제하지 **않는** 것 (정직한 한계)

- **GitHub 계정 침해는 방어 불가.** 바이너리 생산자가 GitHub Actions이므로, 쓰기 권한을 얻은 공격자는 *정상 절차로* 악성 DLL을 만들 수 있고 해시도 당연히 일치한다. 재현빌드나 코드서명이 필요한데 개발기가 맥이라 둘 다 불가능하다. 자기기만하지 않는다.
- **`LoadLibrary` 단계에서 죽는 새 버전은 자동 롤백이 원리적으로 불가능하다.** 카운터를 올리는 주체가 실패한 그 DLL 자신이기 때문. 부팅마커가 잡는 것은 "로드·init은 성공했으나 그 뒤에 죽는" 부분집합이다.
- **실제로 성립하는 보안 성질은 셋뿐이다:** (a) 절단·손상 방어(sha256+size), (b) 릴리스 이후 에셋 교체 방어 — *맥이 실제 바이트에서 직접 해시할 때만*, (c) 홈서버 단독 침해 방어 = **URL 전체 접두사 피닝**.

---

## 1. 아키텍처

```
[1 발행·맥]   sherbet_owner.h 의 SHERBET_VERSION 수정 + 변경내역 커밋 → 푸시
              → build.yml 이 빌드 검증 + 호스트 테스트만 (구매자 알림 없음)
              → git tag sherbet-1.4.0 && git push origin sherbet-1.4.0

[2 발행·CI]   release.yml (on: push: tags: ['sherbet-[0-9]*'])
              태그==SHERBET_VERSION assert → owner.h 무조건 치환
              → 32/64 Release 빌드 → SherbetVersion 심볼 확인
              → gh release create || gh release upload --clobber (멱등)

[3 매니페스트·맥]  tools/sherbet_admin.py → 6. 새 버전 배포
              gh release download 로 실제 배포 바이트 취득
              → 로컬 hashlib 로 sha256 직접 계산(CI 값은 대조용)
              → PE Machine + SherbetVersion 심볼 확인
              → gh release view --json body 로 변경내역
              → scp 로 홈서버 ~/sherbet-auth/content/update.json

[4 알림·클라]  게임 시작 → 프로세스 전역 싱글턴 워커 1개
              GET /sherbet-auth/update/manifest?arch=x64&cur=1.3.0   (미인증)
              → 배너 3곳: 홈탭 상단 / 인증 게이트 패널 안 / 스플래시 한 줄
              ※ 여기까지 디스크 무변경

[5 교체·클라]  [업데이트] 클릭 → 네임드 뮤텍스 → URL 피닝 → 프리플라이트
              → 파일로 스트리밍 다운(증분 sha256) → sha·size·PE 4중 검증
              → rename 2회 자기교체 → 사후 재해시 → 마커 state=pending

[6 부팅판정]   다음 실행 DllMain: (marker.version==내버전 && marker.exe==현재exe) 게이트
              통과 시에만 tries+1 → 2회면 .bak 자동 복원
              성공: on_present 300프레임 or 20초 → 마커 삭제
```

**CI는 홈서버 자격증명을 갖지 않는다.** GitHub Secrets에 홈서버 SSH 키를 넣는 순간 "GitHub 침해 = 홈서버 침해"가 되어 두 시스템을 나눈 의미가 사라진다.

---

## 2. 버전 체계

### 2.1 현재 상태 (조사 결과)

`res/version.h`는 커밋되지 않고 빌드 시 `tools/update_version.ps1`이 생성한다. 그런데:

- `sherbet-base` HEAD에서 `git describe --tags`가 **실패한다**(`fatal: No tags can describe`). 태그 109개가 전부 업스트림 리쉐이드 것이고 HEAD의 조상이 아니다.
- 실패하면 스크립트가 `0,0,0,0`으로 폴백하고 Release 빌드마다 `VERSION_BUILD`를 +1 한다.
- `version.h`는 빌드 스텝마다 재생성되므로 **32비트 빌드와 64비트 빌드의 버전이 서로 다르다**(0.0.0.1 / 0.0.0.2).

→ 업스트림 버전은 자동 업데이트의 식별자로 쓸 수 없다.

### 2.2 결정

**Sherbet 버전 = `source/sherbet_owner.h` 의 `#define SHERBET_VERSION "1.0.0"` 하나.**

새 버전 헤더를 만들지 않는다. `sherbet_owner.h`는 이미 셔벗 빌드 상수 파일이고, CI 치환 관례(기본값 라인만 `-replace` + 치환 검증)가 서 있으며 모든 셔벗 소스가 include 한다.

**태그 = `sherbet-1.4.0` — `v` 금지.**

`update_version.ps1:17`은 `git describe --tags` 결과에 `v(\d+)\.(\d+)\.(\d+)(-\d+-\w+)?`를 **앵커 없이** `-match` 한다. PowerShell `-match`는 부분문자열 매칭이다.

| 태그 | 매칭 | 결과 |
|---|---|---|
| `sherbet-v1.4.0` | ✅ (안에 `v1.4.0`) | ❌ 리쉐이드 `version.h` 탈취 |
| `sherbet-1.4.0` | ❌ (숫자 앞 `v` 없음) | ✅ 안전 |

**2차 방어:** `update_version.ps1:17`을 `git describe --tags --match "v[0-9]*"` 로 한 줄 굳힌다. 태그 규칙이 1차, 앵커가 2차. 한쪽이 무너져도 안전하다.

**주입하지 않고 검증한다 (헤더가 진실, 태그는 검증).**
`release.yml` 첫 스텝이 `태그접미사 == SHERBET_VERSION` 이고 `!= "0.0.0"` 임을 단언하고, 다르면 빌드를 실패시킨다. CI가 버전을 주입하면 소스가 영원히 거짓말하고 로컬 빌드 버전이 미정의가 된다. 이 가드가 없으면 **"서버는 1.4.0인데 DLL은 1.3.0" 무한 알림 루프**가 그대로 출고된다.

**바이너리에 버전을 못 박는다 (1줄).**
`dll_main.cpp:16`의 기존 관례를 따라 익스포트 심볼을 추가한다:

```cpp
extern "C" __declspec(dllexport) const char *SherbetVersion = SHERBET_VERSION;
```

`#define`만으로는 참조되지 않으면 바이너리에 한 바이트도 남지 않는다. 익스포트 심볼이면 확실히 남고, CI와 맥 배포도구가 바이트에서 찾아 "릴리스에 엉뚱한 아티팩트가 올라갔다"를 잡는다. 확인은 `findstr`이 아니라 python 바이트 검색으로 한다(NUL 포함 바이너리에서 `findstr`은 신뢰 불가).

**비교는 순수 함수.** `parse_version`(실패 시 false = 업데이트 없음) + `version_cmp`(정수 3필드 사전식). 문자열 비교 금지 — `1.10.0 < 1.9.0`이 된다.

---

## 3. 매니페스트

### 3.1 전달 경로: 별도 미인증 엔드포인트

`GET /sherbet-auth/update/manifest?arch=x64&cur=1.3.0`

**`/auth/verify`에 얹지 않는 이유:** `sherbet_auth.cpp:90`에서 보듯 캐시 토큰이 없으면 시작 verify 자체를 안 돌린다. 신규 설치·토큰 만료가 커버되지 않고, 무엇보다 **"로그인을 망가뜨린 빌드"는 영원히 고칠 수 없다** — 인증을 요구하면 업데이트가 가장 필요한 순간에 업데이트가 안 되는 데드락이 생긴다. 리포와 릴리스가 어차피 공개이므로 숨길 것도 없다. `/content/me`는 마켓 버튼을 눌러야만 도는 경로라 애초에 부적합하다.

**`/auth/verify`는 한 줄도 건드리지 않는다.** 업데이트 실패는 회복 가능하지만 로그인 실패는 제품이 죽는다.

### 3.2 서버 파일 `~/sherbet-auth/content/update.json`

```json
{
  "schema": 1,
  "version": "1.4.0",
  "min_version": "1.0.0",
  "allow_downgrade": false,
  "notice": "",
  "notes": "OSD 가로 배치 옵션 추가\n프리셋 다운로드 절단 자동 재시도",
  "builds": {
    "x64": {
      "url": "https://github.com/Jeong-Ryeol/reshade/releases/download/sherbet-1.4.0/ReShade64.dll",
      "size": 4312576,
      "sha256": "3f1c9a…소문자 64hex…e2b7"
    },
    "x86": { "url": "…/ReShade32.dll", "size": 3837952, "sha256": "9d40b1…5ac0" }
  }
}
```

mtime 캐시라 **서버 재시작 불필요**(기존 콘텐츠 운영 방식과 동일). 파일 없음/JSON 깨짐/schema 불일치 → 라우트가 503 → 클라는 조용히 "업데이트 없음"(fail-closed).

### 3.3 클라가 받는 평면 응답

```json
{ "schema": "1", "arch": "x64", "version": "1.4.0", "min_version": "1.0.0",
  "allow_downgrade": false, "size": "4312576", "sha256": "3f1c9a…e2b7",
  "url": "https://github.com/Jeong-Ryeol/reshade/releases/download/sherbet-1.4.0/ReShade64.dll",
  "notice": "", "notes": "…" }
```

**중첩 객체 금지.** `sherbet_auth_core.hpp:22`의 `json_string`은 중첩을 모르는 평면 substring 파서다. 새 파서를 쓰면 새 버그를 심는다. 아키텍처 분기는 키 접미사가 아니라 쿼리 파라미터로 하고(서버가 평탄화), 응답에 `arch`를 echo 해서 클라가 자기 것(`#ifdef _WIN64` 컴파일 타임 결정)과 다르면 거부한다.

**⚠️ 타입 규약 — 조용히 실패하는 지점.** `json_string`은 **따옴표로 감싼 문자열만** 읽는다. 숫자를 따옴표 없이 내면 `false`를 반환하고, 클라는 에러 없이 "업데이트 없음"으로 넘어간다(로그에도 안 남는다). 그래서:

- `schema` / `size` / `version` / `min_version` / `sha256` / `url` / `notes` / `notice` → **응답에서는 전부 문자열**. 라우트가 `str()`로 직렬화한다
- `allow_downgrade` → 유일한 예외. `json_bool_or_null`(`sherbet_auth_core.hpp:45`)이 읽으므로 **진짜 JSON 불리언**으로 낸다
- 서버 파일 `update.json`(§3.2)은 사람이 편집하므로 자연스러운 타입(숫자는 숫자)으로 두고, **변환은 라우트가 한다**
- `server/tests/test_update.py`가 "숫자형 값이 문자열로 나가는지"를 반드시 단언한다

### 3.4 클라 거부 조건 (전부 순수 함수, 맥 테스트)

`schema != 1` / `arch` 불일치 / `version` 파싱 실패 / `sha256`이 소문자 64hex 아님 / `size`가 1MiB\~32MiB 밖 / `url`이 정확히 `https://github.com/Jeong-Ryeol/reshade/releases/download/` 로 시작하지 않음 / `url`에 `..` 또는 `@` 포함 / `url`에 `%2e` / `%2E` / `%2f` / `%2F` 포함 / `url`에 제어문자 (0x00\~0x1F 또는 0x7F) 포함. **하나라도 어기면 거부.**

> **호스트만 화이트리스트하면 방어가 아니다.** `github.com`은 누구나 5초 만에 리포를 만들고 릴리스를 올릴 수 있는 멀티테넌트 호스트다. 조건이 "우리 릴리스에 올려야 한다"가 아니라 "아무 GitHub 릴리스에나 올리면 된다"로 무너진다. **URL 전체 접두사 피닝이 홈서버 단독 침해 방어의 유일한 근거다.**

### 3.5 `min_version` / `allow_downgrade`

- `cur < min_version` → 배너가 빨간색 + [디스코드 문의]. **다만 세션 단위 닫기를 허용하고 오버레이·이펙트를 절대 막지 않는다.** 오타 한 번(예: `9.9.9`)으로 전 고객 UI를 잠그는 것은 `sherbet_nodelock.hpp:131`의 "기록 실패 → 잠그지 않음" 원칙에 정면으로 어긋난다. 값 없음/파싱 실패 시 `is_mandatory`는 **반드시 false**.
- `allow_downgrade` 기본 false. true일 때만 현재보다 낮은 버전을 "권장 버전으로 되돌리기"로 제안한다. **이것이 킬스위치가 실제로 도달하는 유일한 경로다**(§7).

---

## 4. 자기교체

### 4.1 전제와 그 검증

Windows 로더는 이미지 파일을 `FILE_SHARE_READ|FILE_SHARE_DELETE`로 열기 때문에 **매핑된 DLL은 삭제는 실패하고 같은 볼륨 rename은 성공한다.** 따라서 자기교체 = rename 2회.

맥에서 검증 불가이므로 **코드 한 줄 쓰기 전에 5초 실험을 강제한다**: 게임 실행 중 cmd에서 `ren dxgi.dll dxgi.dll.bak`. 성공 = 전제 실증. 실패하면 설계를 폐기하고 다시 짠다.
추가로 `tools/win_rename_probe.cpp`(Windows CI 하네스)로 가정을 자동 검증으로 승격한다.

### 4.2 경로와 파일 배치

**자기 경로 = `g_reshade_dll_path`**(`dll_main.cpp:111`에서 `GetModuleFileNameW`로 설정). `_config_path.parent_path()`는 `INSTALL/BasePath`·`RESHADE_BASE_PATH_OVERRIDE`로 DLL 위치와 달라질 수 있어 **절대 금지**.

모든 임시파일을 **DLL과 같은 디렉터리에만** 만든다 → 크로스 볼륨 `MoveFile`(= copy+delete, 사용 중 파일에서 실패)이 구조적으로 불가능해진다. `%TEMP%` 경유 금지.

`<self>.sherbet-new.part` / `<self>.sherbet-new` / `<self>.sherbet-bak` / `<self>.sherbet-failed` / `sherbet.update` / `Sherbet-복구안내.txt`

### 4.3 교체 시점: 클릭 직후 즉시 (DETACH 아님)

`DLL_PROCESS_DETACH`로 미루지 않는다. FiveM은 `TerminateProcess`·크래시 종료가 흔해 detach가 안 도는 일이 잦고, 그러면 다음 attach로 밀려 **재시작 2회 + 세션 전체 디스크/메모리 불일치**가 되어 오히려 나쁘며, DllMain에서 4MB 해시(BCrypt/힙)를 돌리다 종료 데드락이 난다.

즉시 교체의 대가는 "이번 세션 동안 디스크의 dxgi.dll이 매핑된 이미지와 다르다" 하나뿐이고, ReShade는 이미 서명 없는 프록시로 로드되는 물건이라 새 위험이 아니다.

### 4.4 절차

| 단계 | 내용 | 실패 시 디스크 상태 |
|---|---|---|
| **S0** | 렌더 스레드: `begin_update()`. `has_owner()\|\|SHERBET_NODELOCK`이면 즉시 중단. **워커 재대입 전 반드시 `join_worker()`**(끝났지만 joinable인 스레드에 재대입 = `std::terminate` = 게임 즉사). `_busy.exchange(true)` | 무변화 |
| **S1** | 네임드 뮤텍스 `Local\Sherbet.Update.<fnv1a(소문자 DIR)>` 즉시 획득 시도. 실패면 "다른 게임 창에서 업데이트 중" | 무변화 |
| **S2** | `url_allowed()` 전체 접두사 피닝 | 무변화 |
| **S3** | 프리플라이트: `GetDriveTypeW`가 `DRIVE_REMOTE`면 중단 / `CREATE_NEW\|FILE_FLAG_DELETE_ON_CLOSE`로 쓰기 실측 / `GetDiskFreeSpaceExW`로 `size*3+32MiB` 확인 | 무변화 |
| **S4** | 잔재 정리. `.sherbet-new`가 있고 sha가 일치하면 S5\~S7 건너뛰고 S8로 | 원본 정상 |
| **S5** | `http::get_to_file`로 `.part`에 스트리밍. bearer는 **반드시 nullptr**. 청크마다 취소 확인 + 증분 SHA-256 + 32MiB 상한. 실패 시 200/600/1200ms 백오프 3회 | `.part`만 사라짐 |
| **S6** | **검증:** 수신 크기 == `size` && sha256 == `sha256` && `pe_check`(앞 4KB) | 원본 정상 |
| **S7** | `MoveFileExW(.part → .sherbet-new)` = **안전 정지 상태** | 원본 정상 |
| **S8** | 묵은 `.sherbet-bak` 제거(READONLY 해제, 5회 재시도, 최종 실패면 `.sherbet-bak.old`로 밀어냄 — 다음 시작의 정리 대상) | 원본 정상 |
| **S9** | `Sherbet-복구안내.txt` 기록(**실제 파일명 삽입**) → 마커 `state=swapping`/`version`/`prev`/`bak`/`exe`/`sha` | 원본 정상 |
| **S10** | ★ `MoveFileExW(self → .sherbet-bak, dwFlags=0)`. `REPLACE_EXISTING` 금지 | 원본 그대로, 게임 정상 |
| **S11** | ★ `MoveFileExW(.sherbet-new → self, dwFlags=0)`. 실패면 즉시 `.bak → self` 원복 5회 | 원복 성공=원래 버전 유지 |
| **S12** | 사후검증: 새 self를 다시 읽어 sha256 재확인(이제 매핑 안 됨). 불일치(AV 격리/변조)면 즉시 롤백 + 블랙리스트 | — |
| **S13** | 마커 `state=pending`/`tries=0`, 복구안내 삭제, 뮤텍스 해제 | — |
| **S14** | UI: "업데이트 완료 — 게임을 껐다 켜면 v1.4.0이 적용돼요" | — |

**S10\~S11 사이가 self가 없는 유일한 위험 구간(1ms 미만)이며, 프록시 이름이면 게임은 System32의 진짜 dxgi.dll로 폴백해 켜진다.**

### 4.5 AV·안티치트 표면 최소화 (코드에 금지 목록으로 주석화)

자식 프로세스 spawn ❌ / `CreateRemoteThread` ❌ / 자기 이미지 `VirtualProtect`·`WriteProcessMemory` ❌ / 새 DLL을 현재 프로세스에 `LoadLibrary` ❌ / **`MOVEFILE_DELAY_UNTIL_REBOOT` ❌**(`PendingFileRenameOperations`는 Defender가 아는 악성 지표) / **UAC 승격 ❌**(게임 오버레이발 UAC는 그 자체로 악성 신호).

WinInet으로 바이트만 쓰므로 MOTW(`Zone.Identifier`)가 안 붙어 SmartScreen 프롬프트도 없다.

---

## 5. 부팅마커와 자동 롤백

### 5.1 마커 `<DLL 폴더>/sherbet.update`

`sherbet.auth`와 같은 key=value 줄 포맷.

```
state=pending          # pending | swapping | rolledback | rollback_failed
version=1.4.0          # 이 마커가 서술하는 바이너리
prev=1.3.0
bak=dxgi.dll.sherbet-bak
exe=FiveM_b3095_GTAProcess.exe   # 교체 당시 g_target_executable_path.filename()
sha=3f1c9a…e2b7        # 블랙리스트 키
tries=1
```

모든 read-modify-write는 **네임드 뮤텍스 아래**에서 `.part` 작성 후 `MoveFileExW` 원자 교체로 하고, **모르는 키를 보존한다**(writer가 셋이라 통째 덮어쓰면 서로 지운다).

### 5.2 3중 게이트 — 설계의 급소

`DLL_PROCESS_ATTACH`에서 "다른 ReShade 인스턴스 이미 로드됨" 검사(`dll_main.cpp:199` 부근 `return FALSE`) **다음**, hooks 설치 **앞**에서 `on_process_attach(self)`를 호출한다. 이 경로는 **원시 Win32만** 쓴다 — CRT 스트림·`std::filesystem`·`new`·crypto 금지.

세 게이트를 모두 통과할 때만 `tries+1`:

1. `marker.version == SHERBET_VERSION` — 지금 매핑된 바이너리를 서술하는 마커인가
2. **`marker.exe == g_target_executable_path.filename()`** — 교체 당시 그리던 실행파일인가
3. 네임드 뮤텍스 보유

**2번이 핵심이다.** 구매자는 DLL을 `dxgi.dll`로 넣으므로 `is_dxgi == true` → `dll_main.cpp:127`의 "설정파일 없으면 `return FALSE`" 블록이 **통째로 스킵**되고, 그 폴더에서 dxgi를 임포트하는 FiveM 런처·게임·NUI 서브프로세스가 **전부 완전히 로드된다.** exe 게이트가 없으면 **정상 버전이 첫 실행에 `tries` 3을 찍고 무조건 롤백된다.**

### 5.3 "부팅 성공"의 정의

`runtime::on_present`(`runtime.cpp:816` `_frame_count++` 지점)에서 **프로세스 전역 static 래치 1회** + (`_frame_count >= 300` 또는 첫 present 이후 20초).

- `runtime_gui.cpp`의 `_sherbet_auth.tick()` 옆은 **절대 안 된다.** 그 블록은 `if (_show_overlay)` + `ImGui::Begin("Sherbet###Viewport")` **안**이라, Home 키를 안 누른 세션이 통째로 "실패"가 되는 100% 오탐이 난다.
- `==` 이 아니라 `>=`. `_frame_count`는 `runtime.cpp:595`에서 (재)초기화마다 0으로 리셋된다(전체화면 전환·해상도 변경).
- runtime은 스왑체인당 하나이므로 **프로세스 전역 static 래치**가 필요하다.
- 삭제는 §5.2의 (1)(2) 게이트를 통과하고 `state==pending`인 마커만. `rolledback`은 건드리지 않는다(지우면 블랙리스트가 날아가 무한 루프).

### 5.4 롤백 절차

| | 내용 |
|---|---|
| **R3** | `state==swapping` = 교체 중단. self 존재+크기>0이면 성공으로 보고 `pending`으로 승격. self 없고 `.bak` 있으면 복원 후 `rolledback` |
| **R4** | `rolledback`/`rollback_failed`면 `bad_ver`/`bad_sha`를 프로세스 전역 static에 담고 리턴. **마커를 지우지 않는다** |
| **R5** | `pending`이면 3중 게이트 검사 |
| **R6** | `tries+1`을 **먼저 디스크에 쓴다**(쓰기 전 크래시하면 카운트가 안 늘어 미탐) |
| **R8** | `decide_boot`이 rollback이면 **먼저 재료 검증**: `.bak` 존재 + `pe_check` 통과. 실패면 `rollback_failed`만 기록하고 **아무 파일도 안 건드린다** — self를 먼저 밀어내고 `.bak`이 없는 걸 뒤늦게 알면 디렉터리에 DLL이 아예 없게 된다 |
| **R9** | `self → .sherbet-failed`(증거 보관 1개) → `.bak → self`. 두 번째 실패 시 첫 번째 즉시 되돌림. 둘 다 실패면 `rollback_failed`만 남기고 조용히 계속 실행(**브릭 방지 원칙**) |
| **R10** | 마커 `rolledback`/`bad_ver`/`bad_sha` + **같은 값을 프로세스 전역 static에도 즉시 채운다.** 이 한 줄이 없으면 마커를 막 쓴 그 세션에 배너가 다시 떠서 방금 롤백한 불량 DLL을 재설치하는 루프가 된다 |
| **R11** | 현재 세션은 매핑된 불량 DLL로 계속 실행. `return FALSE`로 로딩을 중단하면 **dxgi 프록시 export가 사라져 게임 자체가 안 켜진다.** 대신 `g_sherbet_safe_mode`로 `update_effects`를 조기 반환시키고 "이전 버전으로 되돌렸습니다" 패널만 그린다 |
| **R13** | **롤백 배너에 `[그래도 다시 시도]`** — `sherbet.update`를 삭제해 블랙리스트를 해제한다. 오탐 비용이 클릭 1회로 떨어지므로 자동 판정을 더 정교하게 만들 이유가 사라진다. **구현 시 절대 빼지 말 것** |

`decide_boot`(순수 함수): 마커 없음 → `none` / `pending && tries+1 >= 2` → `rollback` / `pending` → `count` / `rolledback\|rollback_failed` → `none`.

---

## 6. 클라이언트 구조

컨트롤러는 auth에 얹지 않고 **프로세스 전역 싱글턴**(`sherbet::update::instance()`)으로 둔다. runtime 멤버로 두면 스왑체인 2개일 때 워커 둘이 같은 `.part`를 서로 자른다.

### 새 파일

| 파일 | 역할 |
|---|---|
| `source/sherbet_update_core.hpp` | **순수 로직 전부.** Windows/스레드/ImGui 무의존, 맥 clang으로 진짜 단위테스트 |
| `source/sherbet_update.hpp/.cpp` | Win32 글루. 네임드 뮤텍스, 프리플라이트, 스트리밍 다운로드+검증, rename 2회 교체, 마커 IO, 롤백 |
| `tools/sherbet_update_test.cpp` | 맥 호스트 단위테스트(`sherbet_auth_test.cpp`와 같은 assert 스타일) |
| `tools/sherbet_swap_sim.cpp` | **가짜 파일시스템 위 교체 시뮬레이터** |
| `tools/win_rename_probe.cpp` | Windows CI 하네스 — 매핑된 DLL rename 가정 검증 |
| `.github/workflows/release.yml` | 태그 전용 릴리스 |
| `server/app/update.py` + `server/tests/test_update.py` | 매니페스트 라우터 |
| `server/content/update.example.json` | 형식 예시(실운용 파일은 커밋 안 함 — 킬스위치 조작이 리포 사본과 어긋나면 안 됨) |
| `docs/sherbet-업데이트-런북.md` | 발행·킬스위치·롤백·손님 수동 복구 |

### `sherbet_update_core.hpp` 인터페이스

```cpp
version3 / parse_version / version_cmp
struct info { bool ok; std::string version, url, sha256, min_version, notes, notice;
              unsigned long long size; bool allow_downgrade; };
info parse_manifest(const std::string& body, const char* arch);  // json_string 재사용
bool url_allowed(const std::string& url);                        // 전체 접두사 피닝 + '..'/'@' 거부
bool split_https_url(const std::string&, std::string& host, std::string& path);
bool should_offer(const std::string& cur, const info&, const std::string& bad_ver, const std::string& bad_sha);
bool is_mandatory(const std::string& cur, const info&);           // 미정의 입력 → false
sha256_ctx / sha256_init / sha256_update / sha256_final_hex       // 벤더링, 스트리밍
bool pe_check(const unsigned char* head, size_t len, bool want_x64);
boot_marker / serialize_marker / parse_marker                     // 모르는 키 보존
enum class boot_action { none, count, rollback };
boot_action decide_boot(const boot_marker&, int max_tries = 2);
enum class disk_state; disk_state classify(bool has_self, bool has_new, bool has_bak, const boot_marker&);
```

**SHA-256을 벤더링하는 이유:** `ReShade.vcxproj`에 `AdditionalDependencies`가 **하나도 없다.** `bcrypt.lib`를 새로 걸면 링크 성공 여부를 7분 CI 왕복으로만 알 수 있고, BCrypt는 CNG 공급자 테이블·레지스트리·힙을 건드려 **DllMain에서 호출하는 것이 명시적으로 안전하지 않다.** 벤더링하면 링크 의존성 0이고 맥에서 NIST 벡터로 검증된다.

### 수정 파일

`sherbet_owner.h`(버전) · `dll_main.cpp`(익스포트 심볼 + `on_process_attach`) · `runtime.cpp`(`on_present` 부팅 성공 판정) · `runtime.hpp` · `runtime_gui.cpp`(`tick()` + 배너 3곳) · `sherbet_http.hpp/.cpp`(`get_to_file` + 32MiB 상한 + 다운로드 전용 타임아웃) · `ReShade.vcxproj` + `.filters`(**누락하면 CI가 7분 뒤 링크 에러**) · `update_version.ps1` · `tools/sherbet_admin.py` · `server/app/content.py`(`load_json_object`) · `server/app/main.py`(라우터 등록만) · `build.yml`(테스트 잡 추가) · 문서 2개

### 배너 배치 (알림이 실제로 도달하게)

(a) 홈 탭 최상단 (b) **인증 게이트 패널 안**(로그인 깨진 빌드 구제) (c) 부팅 스플래시 한 줄. 오버레이 안에만 두면 Home을 안 누르는 구매자에게 영영 도달하지 않는다.

---

## 7. 릴리스 파이프라인

1. **[맥]** `SHERBET_VERSION`을 1.4.0으로 수정 + 변경내역 같은 커밋 → 푸시 → `build.yml`이 빌드 검증 + 호스트 테스트
2. **[맥]** CI green 확인 후 `git tag sherbet-1.4.0 && git push origin sherbet-1.4.0`
3. **[CI]** 태그↔`SHERBET_VERSION` assert + `!= "0.0.0"` assert
4. **[CI]** `sherbet_owner.h` 무조건 치환: `ONLINE_AUTH=1`(**공개 릴리스에 게이트 없는 DLL이 올라가면 제품이 공짜가 된다**), `NODELOCK=0`(**노드락 켜진 공용 빌드는 전원 브릭**), `OWNER`/`ORDER_NO` 빈 문자열, `DEFAULT_THEME=mint`, `res/presets/personal.ini` 부재 확인
5. **[CI]** msbuild Release 32/64 (Setup은 배포물이 아니므로 생략). 산출물 바이트에 `SherbetVersion` 문자열 확인(python)
6. **[CI]** `gh release create … || gh release upload … --clobber`(멱등: 업로드 중 러너가 죽어도 재실행 가능). `permissions: contents: write` + `env: GH_TOKEN: ${{ github.token }}` **필수**
7. **[맥]** 릴리스 에셋 URL이 실제 200인지 확인. **이 순서를 지킬 것** — 홈서버 매니페스트를 먼저 올리면 아직 없는 파일을 받으러 가는 구매자가 생긴다
8. **[맥]** `sherbet_admin.py` → 6번: `gh release download`로 실제 바이트 취득 → **로컬 hashlib로 sha256 직접 계산**하고 CI 값과 대조(불일치면 중단 — **CI 출력 복붙 금지**) → PE Machine 확인 → `SherbetVersion` 문자열 == 태그 확인 → `gh release view --json body`로 변경내역 → `update.json` 조립 → scp
9. **[검증]** 맥에서 밖에서 curl로 매니페스트 재확인
10. **[카나리]** 내 PC 1대에서 실제 업데이트 → 재시작 → About에 v1.4.0 → 300프레임 정상 → `sherbet.update` 소멸 확인. **통과 전에는 디스코드 공지 금지**
11. **[킬스위치]** 문제 시 `update.json`을 이전 버전 + `allow_downgrade=true`로 되돌린다(scp 한 번, 서명키·GitHub 불필요). 신규 제안이 즉시 멈추고 **이미 업데이트한 사람도 다음 실행에 스스로 강등된다**

> **명시적 금지:** CI가 홈서버에 SSH로 `update.json`을 쓰게 만들지 말 것. GitHub Secrets에 홈서버 키를 주는 순간 "GitHub 침해 = 홈서버 침해"가 되고, 공개 리포 CI에 홈 LAN 키가 들어가는 실제 위험만 새로 생긴다.

---

## 8. 실패 모드

| 시나리오 | 처리 |
|---|---|
| FiveM 런처·게임·NUI가 각각 dxgi.dll을 로드해 한 실행에 카운트 3 | 3중 게이트(exe + version + 뮤텍스)로 "유저가 인식하는 실행 1회"와 1:1 |
| 4MB DLL이 절반만 옴 (**fx로 이미 겪은 사고**) | `size` + `sha256`이 최종 게이트. 검증 전엔 `.part` 이름이라 잘린 DLL이 `dxgi.dll` 이름을 갖는 순간이 0 |
| 홈서버만 털려 url이 공격자 에셋을 가리킴 | URL 전체 접두사 피닝 |
| GitHub 계정 침해 | **방어 불가**(§0.1). 릴리스 이후 에셋 교체만 잡힌다 |
| x64 슬롯에 32비트 DLL을 넣는 운영 실수 | `pe_check`가 클라에서 거부 + 배포도구가 업로드 전 검사. sha256만으로는 못 본다 |
| 매니페스트 version과 실제 에셋 불일치 → 무한 배너 루프 | 배포도구가 태그에서 version 자동 도출 + `SherbetVersion` 심볼 대조 |
| DLL 폴더 쓰기 불가 / 네트워크 드라이브 | 프리플라이트로 중단, 수동 다운로드 링크만. **UAC 승격 절대 안 함** |
| AV가 파일을 잡아 백업 rename 실패 | 5회 재시도 후 `.new`·복구안내 삭제, 디스크 완전 무변화 |
| 백업은 됐는데 설치 rename 실패 | 즉시 원복 5회. 실패해도 프록시 폴백으로 게임은 켜지고 복구안내.txt가 실제 파일명으로 수동 rename 안내 |
| 교체 직후 AV가 새 파일 격리 | 사후 재해시 → 불일치면 즉시 롤백 + 블랙리스트 |
| 새 버전이 부팅 크래시 | 3중 게이트 통과 프로세스에서 2회 연속 시 자동 복원, 그 세션은 safe_mode |
| 새 버전이 `LoadLibrary`에서 죽음 | **자동 롤백 불가.** 4중 검증 + 프록시 폴백 + 복구안내.txt + 가이드 |
| 롤백해야 하는데 `.bak`이 없음 | self를 먼저 밀어내지 않는다. `rollback_failed`만 기록 |
| 정상인데 유저가 로딩 중 2회 강제종료(오탐) | `[그래도 다시 시도]` 클릭 1회. 성공 기준도 300프레임 **또는** 20초로 이르게 |
| 부팅은 되는데 기능만 깨짐(미탐) | 자동 탐지 안 함. 킬스위치(`allow_downgrade`)로 대응 |
| 게임 2개 동시 업데이트 / 마커 동시 기록 | 네임드 뮤텍스 + 프로세스 전역 싱글턴 + 모르는 키 보존 |
| 다운로드 중 취소·게임 종료 | `_stop` 청크마다 확인 → `.part` 삭제. 소멸자가 `_stop` 후 join |
| `update.json` 없음·깨짐·홈서버 다운·오프라인 | 503/전송실패 → 조용히 "업데이트 없음". **인증의 24h 오프라인 유예에 영향 없음** |
| 큰 HTML 에러 페이지로 메모리 폭주 | 파일 스트리밍 + 32MiB 상한 |
| 느린 회선에서 5초 타임아웃 초과 | 다운로드 경로만 수신 20초 + 총 5분 캡으로 분리 |

---

## 9. 테스트 전략

**현재 CI는 테스트를 하나도 돌리지 않는다**(`build.yml`이 checkout/python/msbuild/upload뿐). 호스트 테스트를 CI 잡으로 승격하는 것이 이 계획의 **필수 전제**다 — 직접 짠 SHA-256이 조용히 틀리면 전 고객이 업데이트 불능이 된다.

### 1층 · 맥 clang (즉시 + CI ubuntu 잡)

- **SHA-256**: NIST 벡터(`""`, `"abc"`, 448bit, `'a'×1000000`) + 패딩 경계 55/56/63/64/119/120 + 홀수 청크 스트리밍이 일괄 해시와 같은지. **패딩 버그가 사는 곳이 정확히 여기다**
- **version**: 파싱 실패, `1.10.0 > 1.9.0`(문자열 비교면 틀리는 케이스), 자릿수 다름, 공백, 거대 정수
- **parse_manifest**: 정상 / 키 전무 / schema·arch 불일치 / sha 64hex 아님 / size 범위 밖 / 503 바디 / `\n` 이스케이프 / `"version"`이 `"min_version"`에 오탐하지 않는지 / **잘린 입력 전수**
- **url_allowed**: 정확한 접두사 통과, `github.com/attacker/evil/…` 거부, `github.com.evil.kr` 거부, `http://` 거부, `..`·`@` 거부, 빈 문자열 거부
- **pe_check**: 손으로 만든 x64/x86 헤더, MZ 아님, 4KB 미만, `e_lfanew` 범위 초과, Machine 불일치, `IMAGE_FILE_DLL` 비트 없음
- **마커**: 왕복, 깨진 줄 무시, **모르는 키 보존**, `tries` 비정수
- **`decide_boot` 진리표**, **`should_offer` 표**(같음/구버전/신버전/`bad_ver`+`bad_sha` 일치/버전 같고 sha 다름→재제안/`allow_downgrade` 조합), `is_mandatory` 미정의→false
- **`sherbet_swap_sim.cpp`**: `std::map` 가짜 파일시스템에서 S4\~S13을 돌리며 **단계 K마다 "실패"와 "전원차단"을 주입**하고, 모든 K에서 `classify`+`decide_boot`+repair가 (a) self가 유효 PE이거나 (b) self 부재라면 `.bak`/`.new`로부터 rename 1회로 복구 가능함을 단언. `.sherbet-bak.old` 잔재 정리도 수렴 조건. **상태머신을 순수함수로 뺀 이유가 이것이고, 이 기능에서 가장 값어치 있는 테스트다**
- **server pytest**: arch 미지정/불량, 파일 없음·깨짐·비dict → 503, 값이 문자열로 직렬화되는지, mtime 캐시, x64/x86 비혼입

### 2층 · CI(약 7분)에서만

- MSVC 컴파일·링크(**vcxproj + .filters 등록 누락은 여기서만 드러난다**)
- ImGui 1.92.5 obsolete API 미사용, 한글 `\xNN` 이스케이프 경계(C7744 — 커밋 `c966d30e`에서 이미 밟은 지뢰)
- `win_rename_probe.cpp`: 매핑된 DLL의 `MoveFileExW` 성공 / `DeleteFileW` 실패를 `GetLastError`로 단언
- `release.yml` 자체: **태그를 밀기 전에** `workflow_dispatch` + `dry_run`으로 YAML·permissions·GH_TOKEN 오류를 태운다. 태그는 한 번 밀면 되돌리기가 지저분하다

### 3층 · 윈도우 실기 (오너 직접)

0. **코드 한 줄 쓰기 전 5초 실험:** 게임 실행 중 `ren dxgi.dll dxgi.dll.bak`. 실패하면 설계 폐기
1. **exe 게이트 실측:** FiveM 켠 상태에서 어떤 프로세스가 dxgi.dll을 로드하는지 확인 + 정상 업데이트 후 첫 실행에서 `tries`가 정확히 1인지. **이 설계에서 가장 조용히 틀릴 수 있는 지점**
2. 실제 업데이트 → 재시작 → About에 새 버전 → 300프레임 뒤 `sherbet.update` 소멸 → `.sherbet-bak` 1개만
3. **롤백 리허설:** 일부러 깨진 DLL을 릴리스해 2회 실패 → 자동 복원 확인 → `[그래도 다시 시도]` 확인

---

## 10. 첫 배포에 대한 주의

**자동 업데이트는 "이미 업데이터가 들어있는 DLL"에게만 통한다.** 지금 팔린 DLL에는 업데이터가 없으므로, **업데이터가 들어간 첫 릴리스는 기존 구매자 전원에게 디스코드로 1회 수동 배포해야 한다.** 이후부터 자동이다. 어떤 설계를 골라도 피할 수 없고, 대신 딱 한 번이다.

## 11. 후속 (이 스펙 범위 밖)

- **노드락의 서버측 대체.** 확정사항 6에 따라 개인화를 서버 주도로 옮기면 `SHERBET_NODELOCK` 빌드 인자가 필요 없어진다. 현재 판매 빌드는 이미 `NODELOCK=0` 공용 DLL이므로 급하지 않다. 그때까지 클라의 `has_owner()||SHERBET_NODELOCK` 가드는 안전망으로 유지한다
- **매니페스트 서명.** 필드만 더하면 되는 **순수 가산 변경**이라 나중에 얹을 수 있다. v1에서는 재생공격 방어가 신규 설치에서 무력화되는 문제 때문에 채택하지 않는다
