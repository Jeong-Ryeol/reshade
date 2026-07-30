# Sherbet Auth Server

FastAPI 인증 서버. 디스코드 OAuth로 세션 토큰을 발급하고, 실행 시 역할을 재검증한다.

## 실행 (중요: 단일 워커)

```bash
uvicorn app.main:app --host 0.0.0.0 --port 8000 --workers 1
```

**반드시 `--workers 1`로 실행할 것.** `PendingStore`(OAuth state ↔ hwid/결과 매핑)는
프로세스 내 메모리에만 존재하며 워커 간에 공유되지 않는다. 워커를 2개 이상 띄우면
`/auth/start`가 처리된 워커와 `/auth/callback`/`/auth/poll`이 처리되는 워커가 달라져
state를 찾지 못하고 인증이 무작위로 실패한다. 수평 확장이 필요하면 먼저
`PendingStore`를 공유 저장소(예: Redis)로 옮겨야 한다.

## HTTPS / 리버스 프록시

전 구간 HTTPS로만 노출한다. 이 앱은 평문 HTTP로 뜨므로 앞단에 리버스 프록시
(nginx/Caddy 등)를 두고 TLS 종단 + 프록시를 붙인다. 세션 토큰·OAuth code가
평문으로 흐르면 안 된다.

## 레이트 리밋 (리버스 프록시 계층)

`/auth/start`와 `/auth/poll`은 **인증이 없는** 엔드포인트다. 폴링 남용·state 대량
생성으로 인한 메모리 압박을 막기 위해 리버스 프록시 계층에서 IP 기준 레이트 리밋을
거는 것을 권장한다. (`PendingStore`에 TTL 300s + 하드 캡 10000이 있지만 프록시
레이트 리밋이 1차 방어선이다.)

## 환경 변수 (`.env`)

`.env.example` 참고. 전부 필수(값이 있는 것 제외).

| 변수 | 설명 |
|---|---|
| `DISCORD_CLIENT_ID` | 디스코드 OAuth 앱 Client ID |
| `DISCORD_CLIENT_SECRET` | 디스코드 OAuth 앱 Client Secret |
| `DISCORD_BOT_TOKEN` | 길드 역할 조회용 봇 토큰 |
| `DISCORD_GUILD_ID` | 대상 길드 ID |
| `DISCORD_REDIRECT_URI` | OAuth 콜백 URI (`https://.../auth/callback`) |
| `SESSION_SECRET` | JWT 서명 시크릿 (**16자 이상** 필수 — 짧으면 부팅 거부) |
| `ROLE_BUYER_ID` | 실행 게이트용 구매자 역할 ID |
| `TOKEN_TTL_SECONDS` | 세션 토큰 만료(초), 기본 `86400` |

## 인증 흐름 (요약)

1. 클라이언트 → `POST /auth/start {"hwid": "..."}`. 서버가 추측 불가능한 `state`를
   생성하고 `authorize_url`을 반환한다. (state는 호출자가 고를 수 없다.)
2. 클라이언트가 `authorize_url`을 시스템 브라우저로 연다. 사용자가 디스코드 동의.
3. 디스코드 → `GET /auth/callback?code=&state=`. 서버가 code 교환 → 역할 확인 →
   세션 토큰을 `state`에 저장.
4. 클라이언트가 `GET /auth/poll?state=`로 폴링해 `ready`/`denied`를 수신.
5. 실행마다 `POST /auth/verify {"token","hwid"}`로 토큰+역할 재검증. 업스트림
   장애 시 `503 {"valid": null, "error": "upstream_unavailable"}` → 클라는 24h
   오프라인 유예를 적용한다(“구매자 아님”인 `{"valid": false}`와 구별할 것).

## 테스트

```bash
python -m pytest -q
```

## 원격 테마 (`/content/me`)

- `GET /content/me` — 헤더 `Authorization: Bearer <세션토큰>`. 로그인 사용자의 디스코드 역할에 맞는 테마 JSON 목록 `{"themes":[...]}` 반환. 토큰 무효/만료 → 401, 디스코드 조회 실패 → 503.
- **새 테마 배포(재빌드 불필요):**
  1. `server/content/themes.json` 배열에 테마 항목 추가(스키마: 설계 문서 §13.1 — id/display_name/colors 12색 #rrggbbaa/particle/hue_cycle/role).
  2. `role` 은 디스코드 역할의 **숫자 ID를 따옴표로 감싼 문자열**(예: `"1408966226637750405"`, 무료면 `null`). 디스코드는 역할 ID를 문자열로 반환하므로 따옴표 없이 숫자로 쓰면 매칭이 안 돼 항상 거부(fail-closed)된다. 역할을 새로 만들었다면 개발자 모드로 역할 우클릭 → ID 복사.
  3. 구매자에게 디스코드에서 해당 역할 부여.
  4. 끝. 서버 재시작 불필요(파일 mtime 이 바뀌면 다음 요청에서 자동 반영).
- themes.json 은 요청마다 stat 되며, 내용이 안 바뀌면 메모리 캐시를 쓴다(단일 워커 전제).

## 원격 프리셋·이펙트 (`/content/me` 확장 + `/content/file/<id>`)

- `GET /content/me` 는 `{"themes":[...], "presets":[...], "effects":[...]}` 를 반환한다. presets/effects 는 각각 `content/presets.json`·`content/effects.json` 을 역할 필터한 결과(`role` 필드 제거).
- `GET /content/file/<id>` — 헤더 `Authorization: Bearer <세션토큰>`. **그 id 아이템의 `role` 을 사용자가 보유해야만** 파일 바이트(`application/octet-stream`)를 준다(매니페스트 필터만으로는 부족 — id 추측 방어). 미보유 → 403, 미등록 id → 404, 디코 조회 실패 → 503.
- **매니페스트 스키마**(presets.json / effects.json, 배열):
  ```json
  [{ "id": "strawberry-grade", "filename": "Strawberry.ini", "role": "1408966226637750405", "display_name": "딸기 보정" }]
  ```
  - `id` = 파일명과 분리한 불투명 키(파일 추측 방지). `content/files/<id>` 로 매핑된다(파일명 = id, 확장자 없이).
  - `filename` = 클라가 디스크에 쓸 이름(프리셋=`Sherbet-Presets/`, 이펙트=`Sherbet-Fx/`).
  - `role` = 디스코드 **숫자 역할 ID 문자열**(따옴표), `null`=무료. 숫자 없이 이름 쓰면 항상 거부(fail-closed).
- **새 프리셋/fx 배포(재빌드 불필요):**
  1. 파일을 `content/files/<id>` 로 둔다(파일명 = id, 확장자 없이).
  2. `presets.json`(또는 `effects.json`)에 위 스키마로 항목 추가.
  3. 구매자에게 디스코드에서 해당 역할 부여.
  4. 끝. mtime 캐시라 다음 요청에 자동 반영.
- **주의(fx 다중 파일):** fx 가 LUT `.png`/`.fxh` 를 딸리면 각각을 별도 effects 아이템으로 추가한다(클라가 모두 `Sherbet-Fx/` 에 떨궈 ReShade 가 검색경로에서 발견).

## 조준점 마켓 (`/content/me` 의 `crosshairs`)

**파일 배포가 없다.** 상품의 실체가 발로란트 공유 코드 한 줄이라, 판매는 역할 부여만으로 끝난다.

- `GET /content/me` 응답에 `{"crosshairs":[...]}` 가 함께 실린다. 클라는 오버레이 「마켓」 탭 > 조준점 세그먼트에 카드로 진열하고, 카드마다 그 코드를 실제 크기로 미리 그린다.
- 테마처럼 **잠긴 항목도 진열**한다(`"unlocked": false`). 다만 **잠긴 항목에는 `code` 를 싣지 않는다** — 코드가 곧 상품이라, 잠긴 채로 내려보내면 클라 캐시 파일(`sherbet.themes`)만 열어도 가져갈 수 있다. 잠긴 카드는 이름·제작자·태그만 보이고 자물쇠가 뜬다.
- **매니페스트 스키마**(`content/crosshairs.json`, 배열):
  ```json
  [{ "id": "pro-01", "display_name": "프로 조준점 · 점", "author": "TenZ", "tag": "프로",
     "code": "0;P;c;5;o;1;d;1;z;3;0b;0;1b;0", "role": "1408966226637750405" }]
  ```
  - `id` = 카드 키(불투명, 겹치면 먼저 나온 것만 산다). `display_name` 이 없으면 id 가 이름이 된다.
  - `author` / `tag` 는 선택. 표시에만 쓰인다.
  - `code` = 발로란트에서 복사한 공유 코드 원문. 클라가 **붙여넣기 코드와 같은 파서**로 검증하며, 통과하지 못하면 카드는 남되 '사용 불가' 로 뜬다(조용히 사라지지 않는다).
  - `role` = 디스코드 **숫자 역할 ID 문자열**(따옴표), `null`=무료.
  - **중첩 객체 금지 / `code`·이름류는 반드시 문자열.** 클라 파서는 평면 substring 파서다(설계 `docs/superpowers/specs/2026-07-29-sherbet-auto-update-design.md` §3.3). 라우터가 `str()` 로 직렬화하므로 숫자를 적어도 문자열로 나가지만, 중첩 객체를 넣으면 값이 엉뚱하게 읽힌다.
- **「프로 조준점 팩」 첫 배포 절차:**
  1. 디스코드에서 역할을 만들고 ID를 복사한다(개발자 모드 → 역할 우클릭 → ID 복사).
  2. 홈서버의 `~/sherbet-auth/content/crosshairs.json` 에 항목을 추가한다(위 스키마, `role` 에 그 ID).
  3. 구매자에게 `/grant @유저 <상품키>` 로 역할 부여. **파일을 올릴 것도, 재빌드할 것도 없다.**
  4. 끝. mtime 캐시라 다음 요청에 자동 반영된다. 구매자는 마켓에서 「내 전용 불러오기」를 누르면 잠금이 풀린 카드를 본다.
- 리포에 커밋된 `server/content/crosshairs.json` 은 무료 기본 5종이다. 이 파일의 모든 코드는 `server/tests/test_endpoints.py` 가 **진짜 클라 파서**(`tools/sherbet_xhmarket_check.cpp`)에 먹여 적용 가능한지 확인한다 — 코드를 한 글자 잘못 붙여넣으면 CI 가 잡는다. 운영 서버의 `crosshairs.json` 은 CI 밖이므로, 새 코드를 넣기 전에 리포 파일에 먼저 넣고 테스트를 돌리는 것을 권한다.
