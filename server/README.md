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
