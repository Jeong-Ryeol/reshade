# Sherbet 온라인 인증 + 원격 콘텐츠 — 운영 체크리스트

**코드는 전부 완성·CI green·배포됨.** 여기 남은 항목은 **오너(정렬) 또는 윈도우 환경에서만 가능한 운영 작업**이다(코드 아님). 설계: `docs/superpowers/specs/2026-07-03-sherbet-online-auth-design.md`.

## 현재 상태 요약

| 구성 | 상태 |
|---|---|
| 인증 서버(Phase 0a) | ✅ 홈서버 배포·라이브 (`https://wonryeol.asuscomm.com/sherbet-auth/`) |
| 클라 인증 게이트(Phase 0b) | ✅ CI green (인게임 E2E 미검증) |
| 원격 테마(Phase 1) | ✅ 서버·클라 완료, CI green, 서버 배포 |
| 원격 프리셋·fx(Phase 2) | ✅ 서버·클라 완료, CI green, 서버 배포 |
| 티켓봇 역할 자동부여(Phase 3) | ✅ 배포·라이브 (`/grant` 등), 계층 설정만 남음 |

---

## 1. 디스코드 — 봇 역할 계층 (⚠️ 이거 안 하면 /grant 실패)

봇은 이미 **역할 관리(Manage Roles)** 권한이 있지만, 봇 역할이 부여할 역할보다 아래에 있으면 부여가 거부된다(확인됨: 현재 `구매자`보다 아래).

- **서버 설정 → 역할**에서 **`JeongRyeol Ticket`** 역할을 **`구매자` 및 모든 상품 역할보다 위로** 드래그.

## 2. 디스코드 — 상품 역할 만들고 봇에 등록

각 판매 상품(테마/프리셋/fx)마다:
1. **서버 설정 → 역할**에서 역할 생성(예: `sherbet-strawberry`). 위치는 봇 역할 아래.
2. 역할 우클릭 → **ID 복사**(개발자 모드).
3. 디스코드에서 `/product-add key:strawberry name:딸기 테마 role:@sherbet-strawberry`.
   - 한 상품에 여러 역할(테마+프리셋+fx)을 묶으려면 같은 `key`로 `/product-add`를 역할마다 반복(역할이 append 됨).
4. `/product-list`로 확인. 구매자 역할은 이미 프리시드됨(`/setbuyer`로 변경 가능).

판매 시: `/grant member:@구매자 product:strawberry` → 구매자 역할 + 상품 역할 자동 부여. 환불 시 `/revoke`.

## 3. 서버 — 실제 콘텐츠 업로드 (재빌드 불필요)

홈서버 `~/sherbet-auth/content/`:

- **테마:** `themes.json` 배열에 항목 추가(스키마 §13.1: id/display_name/colors 12색 `#rrggbbaa`/particle/hue_cycle/role).
- **프리셋:** 파일을 `files/<id>`(확장자 없이, 예: `files/strawberry-grade`)로 두고 `presets.json`에 `{id, filename, role, display_name}` 추가. `filename`은 클라가 디스크에 쓸 이름(예: `Strawberry.ini`).
- **fx:** 파일을 `files/<id>`로 두고 `effects.json`에 항목 추가. fx가 LUT `.png`/`.fxh`를 딸리면 각각 별도 effects 항목으로.
- **`role`은 디스코드 숫자 역할 ID 문자열**(따옴표), `null`=무료. 이름 쓰면 항상 거부(fail-closed).
- 서버 재시작 불필요(mtime 캐시). 배포법 상세: `server/README.md`.
- 파일 추가 후: 해당 구매자에게 `/grant`로 역할 부여하면 그 사람 클라의 "내 전용 불러오기"에 뜬다.

## 4. 윈도우 — 판매용 DLL 빌드 + 인게임 E2E (Mac 불가)

- **빌드:** GitHub Actions `build.yml`을 `workflow_dispatch`로 실행하되 **`online_auth=true`**(원하면 `nodelock`/테마/구매자 인자도). 산출 DLL이 온라인 인증 활성 빌드.
- **인게임 확인:**
  1. 게임에 DLL 넣고 실행 → 오버레이에 **로그인 패널** 뜨는지(이펙트 잠김).
  2. 로그인 버튼 → 브라우저 디스코드 OAuth → 승인 → Sherbet로 복귀 → 인증됨(이펙트 언락).
  3. 마켓 → **"내 전용 불러오기"** → 권한 테마 언락 + 프리셋/fx 다운로드 → 프리셋 "적용" → fx `reload` 반영 확인.
  4. 24h 그레이스: 서버 잠깐 내리고도 켜지는지(선택).

---

## 참고 — 잔여 코드 Minor(비차단, 나중에)

- `sherbet_content.cpp` `fetch_file`/`fetch`: write 후 `ofstream` 실패 미체크(디스크 풀 시 성공으로 보고). 기존 패턴.
- 서버 `/content/file`: 403 vs 404로 미보유 id 존재 여부 노출(불투명 id라 감수).
- 서버 `get_member_role_ids` 무캐시 → 요청마다 디스코드 API. 단TTL 캐시 후보(전 엔드포인트 공통).
- `sherbet_theme_json.hpp` 색 파서 평면 substring 검색(현 12키 스키마 안전).
