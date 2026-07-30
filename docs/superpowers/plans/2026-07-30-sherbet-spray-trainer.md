# 스프레이 트레이너 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** PVP 유저가 자기 반동 보정 패턴을 실시간·차트로 확인할 수 있게 한다.

**Architecture:** 판정 로직은 순수 헤더(`sherbet_spray.hpp`, 맥 호스트 테스트)에, 입력 수집은 `input_windows.cpp` 의 raw input 경로에, 표시는 `runtime_gui.cpp` 의 새 「에임」 탭과 오버레이 게이트 바깥 실시간 레이어에 둔다.

**Tech Stack:** C++17 순수 헤더 + Win32 raw input + ImGui 1.92.5

**설계 스펙:** `docs/superpowers/specs/2026-07-30-sherbet-spray-trainer-design.md` (커밋 `229443e0`)

## Global Constraints

- **판정 로직은 `source/sherbet_spray.hpp` 에만.** 순수 C++17 표준 라이브러리, Windows·ImGui 무의존. 맥 clang 으로 실제 단위테스트한다.
- 한글 UI 문자열은 `\x` UTF-8 이스케이프. **`\xNN` 뒤에 16진수 ASCII 가 바로 오면 `error C7744`** — 문자열 리터럴을 분리한다(`"\xC2\xB7" "FPS"`). 커밋 `c966d30e` 가 이 버그다.
- ImGui 1.92.5 + `IMGUI_DISABLE_OBSOLETE_FUNCTIONS` — obsolete API 금지.
- 빌드는 MSBuild `.vcxproj`. 새 파일은 `ReShade.vcxproj` + `.filters` 등록 필요.
- **입력을 주입하지 않는다.** 리쉐이드가 이미 후킹 중인 메시지를 읽기만 한다. 후킹 표면을 늘리지 않는다.
- **기본값은 두 토글 모두 꺼짐.**
- 커밋 메시지에 AI/클로드 저작권 문구 금지.
- 호스트 테스트 컴파일: `clang++ -std=c++17 -Wall -Isource tools/sherbet_spray_test.cpp -o /tmp/t && /tmp/t` — **`-DNDEBUG` 절대 금지**.

---

## File Structure

| 파일 | 책임 |
|---|---|
| `source/sherbet_spray.hpp` (신규) | 순수 로직 — 누적·구간나누기·링버퍼·통계 |
| `tools/sherbet_spray_test.cpp` (신규) | 맥 호스트 단위테스트 |
| `source/input.hpp` (수정) | raw 이동 누적 멤버 + 접근자 + 가용 플래그 |
| `source/input_windows.cpp` (수정) | `lLastX/lLastY` 누적 (2줄 + 절대좌표 예외) |
| `source/runtime.hpp` (수정) | recorder 멤버 + 설정 값 + 탭 상수 |
| `source/runtime.cpp` (수정) | config 저장/로드 |
| `source/runtime_gui.cpp` (수정) | 기록 호출 · 실시간 궤적 · 에임 탭 · 조준점 이전 |
| `ReShade.vcxproj` / `.filters` (수정) | 새 헤더 등록 |
| `.github/workflows/build.yml` (수정) | 호스트 테스트 목록에 추가 |

---

## Task 1: 순수 로직 + 호스트 테스트

**Files:** Create `source/sherbet_spray.hpp`, `tools/sherbet_spray_test.cpp`

**Interfaces:**
```cpp
namespace sherbet { namespace spray {
    struct shot    { float x = 0, y = 0; float t = 0; };
    struct segment { std::vector<shot> shots; };
    constexpr std::size_t kMaxSegments = 20;

    class recorder {
    public:
        void on_frame(float dt, int dx, int dy, bool fire);
        const segment *current() const;               // 진행 중이면 포인터, 없으면 nullptr
        const std::vector<segment> &history() const;  // 완료분, 최신이 뒤, 최대 kMaxSegments
        void clear();
        void set_gap_ms(int ms);                      // 기본 400
        int  gap_ms() const;
    };

    // 최근 n 구간의 마지막 점들의 중심으로부터 평균 거리(일관성 지표).
    // 구간이 2개 미만이면 false 반환, out 미변경.
    bool end_spread(const std::vector<segment> &hist, std::size_t n, float &out);
    // 구간의 평균 연사 간격(초). 발이 2개 미만이면 false.
    bool avg_interval(const segment &s, float &out);
}}
```

**의미론 (테스트가 못 박을 것):**
- `on_frame` 은 매 프레임 호출. `dx/dy` 는 **이번 프레임의 raw 이동 누적**(호출 측이 읽고 리셋).
- 누적 오프셋은 **구간이 살아 있는 동안만** 쌓인다. 구간이 없을 때의 이동은 버린다(조준만 하는 중).
- `fire` 는 **상승 엣지**(호출 측이 계산). true 인 프레임에 현재 오프셋으로 점을 찍는다.
- 첫 발은 항상 `(0,0,0)` — 구간 시작이 곧 첫 발이다.
- 마지막 발 이후 누적 시간이 `gap_ms` 를 **넘으면**(`>`) 구간을 종료해 history 에 넣는다. 정확히 같으면 아직 유지한다.
- history 가 `kMaxSegments` 를 넘으면 **가장 오래된 것부터 버린다**.

- [ ] **Step 1: 실패하는 테스트 작성**

`tools/sherbet_spray_test.cpp` — 기존 `tools/sherbet_auth_test.cpp` 스타일(`<cassert>`, `static void test_*()`, `main()` 이 호출 후 `ALL PASS` 출력, BSD-3-Clause 헤더에 정렬(Jeong-Ryeol)).

반드시 포함할 케이스:
- 발사 없이 이동만 → 구간 없음, history 비어 있음
- 한 발 → 구간 1개, 점 1개가 `(0,0)`
- 연발: 이동+발사 반복 → 점들이 누적 오프셋을 정확히 반영 (손으로 계산한 값과 대조)
- **구간 경계 정확히:** gap 400ms 에서 `399ms → 유지`, `400ms → 유지`, `401ms → 종료`
- 종료 후 다음 발사는 새 구간이고 오프셋이 `(0,0)` 으로 리셋
- 구간이 없을 때 들어온 이동은 다음 구간에 새지 않는다
- **링버퍼:** 21개 구간을 만들면 history 는 20개이고 **가장 오래된 것이 빠졌다**(첫 구간의 특징으로 확인)
- `clear()` 후 전부 비어 있음
- `end_spread`: 구간 0개·1개 → false / 마지막 점이 모두 같으면 0 / 손계산 가능한 3구간 예시
- `avg_interval`: 발 1개 → false / 등간격 3발 → 그 간격
- `set_gap_ms` 로 임계값을 바꾸면 경계도 따라 바뀐다

- [ ] **Step 2: 테스트가 실패하는지 확인**

Run: `cd ~/reshade && clang++ -std=c++17 -Wall -Isource tools/sherbet_spray_test.cpp -o /tmp/sherbet_spray_test`
Expected: FAIL — `'sherbet_spray.hpp' file not found`

- [ ] **Step 3: 구현**

`source/sherbet_spray.hpp` 작성. 위 의미론을 그대로 구현한다. 부동소수 누적 오차를 피하려고 오프셋은 `float` 로 두되 입력은 정수로 받는다.

- [ ] **Step 4: 통과 확인**

Run: `clang++ -std=c++17 -Wall -Isource tools/sherbet_spray_test.cpp -o /tmp/t && /tmp/t`
Expected: `sherbet_spray: ALL PASS`
그리고 `-fsanitize=address,undefined` 로도 통과할 것.

- [ ] **Step 5: CI 목록에 추가 + 커밋**

`.github/workflows/build.yml` 의 host-tests 잡 테스트 목록에 `sherbet_spray_test` 추가.

```bash
git add source/sherbet_spray.hpp tools/sherbet_spray_test.cpp .github/workflows/build.yml
git commit -m "스프레이 기록 순수 로직 추가

사격 방식을 분류하지 않고 무발사 간격으로만 구간을 나눈다 — 임계값 판별은
실전에서 오분류가 잦아(급한 탭 두 번이 버스트로, 렉이 연발을 쪼갬) 통계를 오염시킨다.
같은 코드에서 탭/버스트/연발이 저절로 다른 모양으로 나온다."
```

---

## Task 2: raw 마우스 이동 수집

**Files:** Modify `source/input.hpp`, `source/input_windows.cpp`

**⚠️ 이 태스크가 기능 전체의 전제다.** 스펙 §3 참조.

**Interfaces:**
```cpp
// input.hpp (public)
int  raw_mouse_delta_x();     // 읽고 0 으로 리셋
int  raw_mouse_delta_y();     // 읽고 0 으로 리셋
bool raw_mouse_available() const;  // raw input 상대이동을 한 번이라도 받았으면 true
```

- [ ] **Step 1: `input.hpp` 에 멤버·접근자 추가**

`_mouse_position` 근처에 `int _raw_mouse_delta[2] = {};` 와 `bool _raw_mouse_seen = false;` 를 둔다.
접근자는 읽고 리셋하는 형태 — 호출자가 프레임당 한 번만 읽는다는 전제를 주석으로 명시할 것.

- [ ] **Step 2: `input_windows.cpp` 의 `RIM_TYPEMOUSE` 블록에 누적 추가**

`source/input_windows.cpp:181` 의 `case RIM_TYPEMOUSE:` 안, **버튼 처리와 같은 위치**에 넣는다.

```cpp
// 상대 이동 누적. 게임이 마우스를 캡처하면 커서(_mouse_position)는 움직이지 않으므로
// 스프레이 트레이너가 쓸 수 있는 이동량은 raw input 의 lLastX/lLastY 뿐이다.
// MOUSE_MOVE_ABSOLUTE 는 태블릿·원격데스크톱 등에서 오며 델타가 아니라 좌표라 건너뛴다.
if ((raw_data.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE) == 0 &&
    (raw_data.data.mouse.lLastX != 0 || raw_data.data.mouse.lLastY != 0))
{
    input->_raw_mouse_delta[0] += raw_data.data.mouse.lLastX;
    input->_raw_mouse_delta[1] += raw_data.data.mouse.lLastY;
    input->_raw_mouse_seen = true;
}
```

**⚠️ 184행의 조기 탈출을 확인할 것:**
```cpp
if (raw_input_window == s_raw_input_windows.end() || (raw_input_window->second & 0x2) == 0)
    break;  // 레거시 마우스 메시지가 켜져 있으면 여기서 처리하지 않음
```
이 `break` 보다 **앞에** 누적을 넣을지 뒤에 넣을지 판단하고 이유를 보고서에 쓸 것. 뒤에 두면 레거시 메시지를 쓰는 게임에서 이동량을 못 얻고, 앞에 두면 이동이 두 번 세어질 위험이 있는지 검토해야 한다(버튼 처리가 뒤에 있는 이유를 읽고 판단).

- [ ] **Step 3: 기존 입력 동작 회귀 검토**

`_keys` · `_mouse_position` · `_mouse_wheel_delta` 의 기존 갱신 경로를 한 줄도 바꾸지 않았는지 확인한다. 오버레이 조작과 핫키가 그대로 동작해야 한다.

- [ ] **Step 4: 커밋 + CI green**

```bash
git add source/input.hpp source/input_windows.cpp
git commit -m "raw 마우스 상대이동 누적 추가

리쉐이드가 raw input 의 버튼·휠은 읽으면서 lLastX/lLastY 는 읽고 버리고 있었다.
게임이 마우스를 캡처하면 커서 위치는 움직이지 않으므로, 스프레이 트레이너가
쓸 수 있는 이동량은 이것뿐이다. 절대좌표 모드(태블릿·원격데스크톱)는 제외한다."
git push
```

---

## Task 3: 「에임」 탭 + 입력 진단 + 조준점 이전

**Files:** Modify `source/runtime.hpp`, `source/runtime.cpp`, `source/runtime_gui.cpp`

**⚠️ 입력 진단이 스펙 §3 의 실물 검증을 대체한다.** 별도 검증 빌드 없이, 사용자가 탭을 열면 raw input 가용 여부가 즉시 보인다. 테스트용 임시 UI 가 아니라 영구 기능이다 — 나중에 구매자가 "궤적이 안 그려져요" 할 때 이 화면 하나로 진단이 끝난다.

- [ ] **Step 1: 레일에 탭 추가**

`source/runtime_gui.cpp:1821` 부근 배열에 마켓 다음 자리로:
```cpp
{ "##tab_aim", ICON_FK_CROSSHAIRS, 5 },
```
`switch (_sherbet_tab)`(1849행 부근)에 `case 5:` 추가. 기존 탭 번호(0~4)는 건드리지 않는다 — config 에 저장된 값이 있을 수 있다.

- [ ] **Step 2: 입력 진단 섹션**

에임 탭 최상단에 접히지 않는 작은 상태 표시:
- 좌클릭 감지 횟수 (세션 누적)
- 마우스 이동: `raw_mouse_available()` 이면 ✅ + 현재 누적값, 아니면 ❌ + `"이 게임에서는 마우스 이동을 읽을 수 없어요 — 궤적 대신 발사 기록만 표시됩니다"`

- [ ] **Step 3: 커스텀 조준점 설정 이전**

설정탭의 조준점 `CollapsingHeader` 블록을 에임 탭으로 **이동**(복사 아님). 설정탭 원래 자리에는 한 줄만 남긴다: `"조준점 설정은 왼쪽 에임 탭으로 옮겼어요"`.

ImGui ID 충돌에 주의 — 옮기면서 `##` 접미사가 다른 위젯과 겹치지 않는지 확인.

- [ ] **Step 4: 커밋 + CI green**

---

## Task 4: 기록 배선 + 실시간 궤적

**Files:** Modify `source/runtime.hpp`, `source/runtime.cpp`, `source/runtime_gui.cpp`

- [ ] **Step 1: recorder 멤버와 설정**

`runtime.hpp` 에 `sherbet::spray::recorder _sherbet_spray;` 와 설정 값들:
```cpp
bool  _sherbet_spray_live  = false;
bool  _sherbet_spray_chart = false;
float _sherbet_spray_scale = 1.0f;
int   _sherbet_spray_gap_ms = 400;
unsigned int _sherbet_spray_clicks = 0;   // 진단용 세션 누적
```
`runtime.cpp` 의 config 저장/로드에 `[OVERLAY] SherbetSprayLive/Chart/Scale/GapMs` 추가 — 기존 Sherbet 설정들과 같은 패턴을 따를 것.

- [ ] **Step 2: 매 프레임 기록**

**오버레이 게이트(`if (_show_overlay)`, 1512행) 바깥**, 커스텀 조준점 블록(1173행) 근처에서:
```
좌클릭 상승 엣지 계산 (이전 프레임 상태를 멤버로 보관)
오버레이가 열려 있으면 기록하지 않는다 — UI 조작 이동이 궤적을 오염시킨다
_sherbet_spray.on_frame(dt, raw_dx, raw_dy, fire_edge)
```

`raw_mouse_delta_x/y()` 는 **읽고 리셋**이므로 프레임당 정확히 한 번만 호출해야 한다.

- [ ] **Step 3: 실시간 궤적 (토글 ①)**

`ImGui::GetForegroundDrawList()` — 조준점과 같은 방식. 화면 중앙 기준, `_sherbet_spray_scale` 배율 적용. 현재 구간의 점을 선으로 잇고 각 점에 작은 원. 마지막 발사 후 2초 페이드아웃.

`raw_mouse_available()` 이 false 면 궤적을 그리지 않는다(점이 전부 원점에 겹칠 뿐이라 오히려 혼란).

- [ ] **Step 4: 커밋 + CI green**

---

## Task 5: 차트 (토글 ②)

**Files:** Modify `source/runtime_gui.cpp`

- [ ] **Step 1: 차트 렌더**

에임 탭 안, 스프레이 섹션:
- 격자 배경 + 중앙 십자
- 구간 목록: `#7 · 20발 · 92ms 간격` 형태. 클릭하면 그 구간만 강조
- **최근 5개 겹쳐보기** 체크박스 — 과거 구간은 흐리게, 선택 구간은 진하게
- 통계: 발수 / 평균 RPM / **종료 지점 편차**(스펙 §5.2 정의 — 최근 N 구간 마지막 점들의 중심으로부터 평균 거리, 구간 2개 미만이면 미표시)
- `[기록 지우기]` 버튼

- [ ] **Step 2: 토글과 배율 슬라이더**

`화면에 실시간 표시` / `오버레이에 차트 표시` 체크박스 2개(독립), `궤적 배율` 슬라이더, `구간 나누기 간격(ms)` 슬라이더.

- [ ] **Step 3: 커밋 + CI green**

---

## Task 6: 릴리스 1.2.0

- [ ] **Step 1: 전 스위트 + CI**

호스트 테스트 전부(`sherbet_auth_test` `sherbet_update_test` `sherbet_swap_sim` `sherbet_spray_test` 등) `-Wall` 과 `-fsanitize=address,undefined` 양쪽 통과. CI 세 잡 green.

- [ ] **Step 2: 버전 올리고 태그**

`source/sherbet_owner.h` 의 `SHERBET_VERSION` 을 `1.2.0` 으로. 커밋·푸시 후 CI green 확인.
```bash
git tag sherbet-1.2.0 && git push origin sherbet-1.2.0
```
**태그에 `v` 를 넣지 말 것** — `sherbet-v1.2.0` 은 `tools/update_version.ps1` 의 앵커 없는 `-match` 에 걸려 리쉐이드 `res/version.h` 를 탈취한다.

- [ ] **Step 3: 매니페스트 배포**

릴리스 에셋을 **다시 받아 해시를 직접 계산**하고(CI 출력 복붙 금지 — 릴리스 이후 에셋 교체를 잡는 유일한 지점), PE 아키텍처와 바이너리 안의 버전 문자열을 대조한 뒤 홈서버 `~/sherbet-auth/content/update.json` 에 배치한다. **`min_version` 은 값이 없으면 키 자체를 넣지 않는다** — 빈 문자열을 내보내면 클라가 매니페스트 전체를 거부한다.

배포 후 밖에서 확인:
```bash
curl -s 'https://wonryeol.asuscomm.com/sherbet-auth/update/manifest?arch=x64&cur=1.1.0'
```

---

## Self-Review

**1. 스펙 커버리지** — §1~§10 전부. §11(범위 밖)은 의도적으로 미구현.

**2. 플레이스홀더** — 없음. Task 2 Step 2 의 "앞/뒤 판단"은 구현자가 코드를 읽고 결정할 사항으로, 판단 기준과 보고 의무를 명시했다.

**3. 타입 일관성** — `recorder`/`shot`/`segment`/`end_spread`/`avg_interval`(T1) → T4·T5 에서 사용. `raw_mouse_delta_x/y`/`raw_mouse_available`(T2) → T3 진단·T4 기록에서 사용.

**4. 순서 근거** — T2(입력)를 T3(진단 UI)보다 먼저 두어야 진단이 실제 값을 보여줄 수 있다. T1(순수 로직)은 다른 것과 독립이라 먼저 해도 되고, 테스트가 붙어 있어 가장 안전한 출발점이다.
