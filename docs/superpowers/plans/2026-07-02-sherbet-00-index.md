# SHERBET 구현 계획 — 인덱스 (플랜 분해)

스펙: `docs/superpowers/specs/2026-07-02-sherbet-ui-remodel-design.md`
작업 브랜치: `sherbet-base` (upstream = crosire/reshade, 기준 f191dc03)

## 왜 여러 플랜으로 나누는가

스펙은 서로 독립적으로 빌드·검증 가능한 여러 서브시스템을 담고 있다. 각 플랜은 **끝나면 CI가 green으로 빌드되고 눈으로 확인 가능한 상태**를 만든다. 순서대로 실행한다.

## 검증 모델 (중요 — 일반 프로젝트와 다름)

- Mac에서는 **컴파일/실행 불가**. 단위 테스트 러너도 없다(오버레이 UI DLL).
- 따라서 각 태스크의 "테스트"는 두 가지다:
  1. **CI 빌드 green** — `.github/workflows/build.yml`이 32/64비트 모두 컴파일 성공 (푸시하면 자동 실행, `gh run watch`로 확인)
  2. **시각 확인** — 마일스톤마다 CI 아티팩트(DLL)를 Windows에서 게임/테스트앱에 붙여 스크린샷
- 규칙: **빨간 CI 위에 다음 태스크를 쌓지 않는다.** 각 태스크 끝 = 커밋 + push + CI green 확인.

## 플랜 순서

| # | 플랜 | 산출물(끝났을 때 상태) |
|---|------|------------------------|
| 1 | `...-01-foundation.md` | 리브랜드 스캐폴딩. `sherbet_theme`/`sherbet_owner` 존재, 스플래시·창 제목이 "Sherbet by 정렬"로 표시, CI green. 아직 레이아웃은 원본 유지 |
| 2 | `...-02-fonts-skin-engine.md` | Jua/Gaegu 폰트 임베드 + `sherbet_ui` 스킨 엔진(스타일/배경/파티클/글로우) 적용. 오버레이가 테마 색으로 물듦 |
| 3 | `...-03-custom-widgets.md` | 커스텀 위젯(토글 스위치·그라디언트 슬라이더·카드·아이콘 레일). 홈 탭이 카드 리스트로 |
| 4 | `...-04-layout-tabs.md` | 좌측 아이콘 레일 + 탭 4개(홈/마켓/설정/정보)로 재편. 애드온/통계/로그 탭 숨김, 로그는 설정>고급 |
| 5 | `...-05-osd-convenience.md` | OSD 위치/크기/드래그 커스텀, 즐겨찾기 필터, 원클릭 프리셋 전환, 비교 슬라이더 |
| 6 | `...-06-license-market.md` | 노드락 + 오프라인 언락코드 + 테마 마켓 + 프리셋 마켓(개인 세팅 이어받기) + About 크레딧/경고 + 주문용 빌드 워크플로 |

각 플랜은 이전 플랜의 산출물(함수/타입)을 `Consumes`로 참조한다. 플랜 1부터 순서대로.

## 전역 제약 (모든 플랜 공통)

- 컴파일러: MSVC (VS2022), C++17. ImGui는 ReShade 번들 버전(`deps/imgui`)만 사용.
- 외부 라이브러리 추가 금지. 아이콘은 ForkAwesome(`res/fonts/forkawesome.h`), 도형은 `ImDrawList`.
- 폰트는 OFL 라이선스(Jua/Gaegu)만. 배포 DLL 임베드 안전.
- 이모지 렌더 금지.
- 핵심 기능 로직(이펙트 컴파일/프리셋/렌더러 후킹) 수정 금지 — 스킨/레이아웃/신규 탭만.
- 커밋 메시지에 Claude 저작권 문구 넣지 않음(사용자 전역 규칙).
- 디스코드 URL 고정: `https://discord.gg/5NGR7XVFta`. 제작자명: `정렬`.
