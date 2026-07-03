# Sherbet 인증 서버 (Phase 0a) 구현 계획

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 디스코드 OAuth로 로그인하고, 사용자의 길드 역할을 확인해, HWID에 묶인 세션 토큰을 발급/검증하는 FastAPI 서버를 만든다. (Sherbet 클라의 실행 게이트가 붙을 백엔드)

**Architecture:** FastAPI 앱. 클라가 브라우저로 `/auth/discord`를 열면 디스코드 OAuth로 리다이렉트 → `/auth/callback`이 code를 토큰으로 교환하고 봇 토큰으로 길드 역할을 조회 → `sherbet-buyer` 역할이 있으면 HWID에 묶인 JWT 세션 토큰을 발급하고 state별로 보관 → 클라는 `/auth/poll`로 토큰을 회수 → 이후 `/auth/verify`로 실행 시마다 토큰+HWID 검증 및 최신 역할 반환. 디스코드 호출은 httpx로, 테스트는 respx로 목킹해 Mac에서 전부 검증 가능.

**Tech Stack:** Python 3.11+, FastAPI, uvicorn, httpx, PyJWT, pydantic-settings, pytest, pytest-asyncio, respx.

## Global Constraints

- 서버 코드는 리포 내 `server/` 디렉토리에 둔다 (C++ 빌드와 무관, 같은 git 리포에서 버전관리).
- 모든 디스코드 외부호출은 httpx 사용. 테스트는 respx로 목킹 — 실제 디스코드 호출 없이 Mac에서 전부 pass 해야 함.
- 세션 토큰은 **PyJWT HMAC-SHA256**, 클레임에 `sub`(디코 user id), `hwid`, `roles`(문자열 배열), `exp` 포함. 검증 시 서명 + `exp` + `hwid` 일치 확인.
- 토큰 TTL 기본 **86400초(24h)**. 오프라인 그레이스(24h)는 클라 동작이며 서버는 무관.
- 역할 판정 기준 역할: `sherbet-buyer`(실행 게이트). 역할은 **역할 ID**로 설정하고 이름 매핑은 config로 주입 (디스코드 API는 멤버의 role id 배열을 줌).
- 비밀값(client secret, bot token, session secret 등)은 코드에 하드코딩 금지 — 환경변수/`.env`.
- 커밋 메시지에 AI/클로드 저작권 문구 금지.

## File Structure

- `server/requirements.txt` — 의존성
- `server/.env.example` — 필요한 환경변수 목록(값은 비움)
- `server/.gitignore` — `.env`, `__pycache__`, `.pytest_cache`
- `server/app/__init__.py` — 빈 패키지 마커
- `server/app/config.py` — pydantic-settings 설정 로더 (`Settings`)
- `server/app/tokens.py` — 세션 토큰 발급/검증 (`issue_token`, `verify_token`)
- `server/app/discord_roles.py` — 봇 토큰으로 길드 멤버 역할 조회 (`get_member_role_ids`, `has_buyer_role`)
- `server/app/oauth.py` — 디스코드 OAuth: authorize URL 생성, code→토큰 교환, user id 조회 (`build_authorize_url`, `exchange_code`, `get_user_id`)
- `server/app/store.py` — state→결과 임시 보관 (`PendingStore`)
- `server/app/main.py` — FastAPI 앱 + 라우트 (`/health`, `/auth/discord`, `/auth/callback`, `/auth/poll`, `/auth/verify`)
- `server/tests/__init__.py`
- `server/tests/conftest.py` — 공통 fixture (설정 오버라이드, TestClient)
- `server/tests/test_tokens.py`
- `server/tests/test_discord_roles.py`
- `server/tests/test_oauth.py`
- `server/tests/test_store.py`
- `server/tests/test_endpoints.py`

---

### Task 1: 프로젝트 스캐폴드 + 설정 로더

**Files:**
- Create: `server/requirements.txt`
- Create: `server/.env.example`
- Create: `server/.gitignore`
- Create: `server/app/__init__.py`
- Create: `server/app/config.py`
- Create: `server/tests/__init__.py`
- Create: `server/tests/conftest.py`
- Test: `server/tests/test_config.py`

**Interfaces:**
- Produces: `app.config.Settings` (pydantic BaseSettings) with fields:
  `discord_client_id: str`, `discord_client_secret: str`, `discord_bot_token: str`,
  `discord_guild_id: str`, `discord_redirect_uri: str`, `session_secret: str`,
  `role_buyer_id: str`, `token_ttl_seconds: int = 86400`.
  `get_settings() -> Settings` (lru_cache singleton).

- [ ] **Step 1: 의존성·gitignore·env 예시 작성**

`server/requirements.txt`:
```
fastapi==0.115.0
uvicorn[standard]==0.30.6
httpx==0.27.2
PyJWT==2.9.0
pydantic-settings==2.5.2
pytest==8.3.3
pytest-asyncio==0.24.0
respx==0.21.1
```

`server/.gitignore`:
```
.env
__pycache__/
.pytest_cache/
*.pyc
```

`server/.env.example`:
```
DISCORD_CLIENT_ID=
DISCORD_CLIENT_SECRET=
DISCORD_BOT_TOKEN=
DISCORD_GUILD_ID=
DISCORD_REDIRECT_URI=https://wonryeol.asuscomm.com/auth/callback
SESSION_SECRET=
ROLE_BUYER_ID=
TOKEN_TTL_SECONDS=86400
```

`server/app/__init__.py`: (빈 파일)
`server/tests/__init__.py`: (빈 파일)

- [ ] **Step 2: 실패하는 테스트 작성**

`server/tests/test_config.py`:
```python
from app.config import Settings


def test_settings_reads_fields_and_defaults():
    s = Settings(
        discord_client_id="cid",
        discord_client_secret="csecret",
        discord_bot_token="btoken",
        discord_guild_id="123",
        discord_redirect_uri="https://x/callback",
        session_secret="sekret",
        role_buyer_id="456",
    )
    assert s.discord_client_id == "cid"
    assert s.role_buyer_id == "456"
    assert s.token_ttl_seconds == 86400  # default
```

- [ ] **Step 3: 테스트 실패 확인**

Run: `cd server && python -m pytest tests/test_config.py -v`
Expected: FAIL — `ModuleNotFoundError: No module named 'app.config'`

- [ ] **Step 4: config.py 구현**

`server/app/config.py`:
```python
from functools import lru_cache

from pydantic_settings import BaseSettings, SettingsConfigDict


class Settings(BaseSettings):
    model_config = SettingsConfigDict(env_file=".env", extra="ignore")

    discord_client_id: str
    discord_client_secret: str
    discord_bot_token: str
    discord_guild_id: str
    discord_redirect_uri: str
    session_secret: str
    role_buyer_id: str
    token_ttl_seconds: int = 86400


@lru_cache
def get_settings() -> Settings:
    return Settings()
```

- [ ] **Step 5: conftest.py 작성 (테스트용 설정 주입)**

`server/tests/conftest.py`:
```python
import pytest

from app.config import Settings


@pytest.fixture
def settings() -> Settings:
    return Settings(
        discord_client_id="test-cid",
        discord_client_secret="test-secret",
        discord_bot_token="test-bot-token",
        discord_guild_id="guild-1",
        discord_redirect_uri="https://test/auth/callback",
        session_secret="unit-test-session-secret",
        role_buyer_id="role-buyer",
        token_ttl_seconds=86400,
    )
```

- [ ] **Step 6: 테스트 통과 확인**

Run: `cd server && python -m pytest tests/test_config.py -v`
Expected: PASS

> 참고: `cd server && pip install -r requirements.txt` 를 최초 1회 실행. pytest는 `server/` 를 rootdir 로 잡도록 `server/pytest.ini` 없이 `cd server` 에서 실행한다(그래야 `app` 패키지가 import 됨).

- [ ] **Step 7: 커밋**

```bash
git add server/requirements.txt server/.env.example server/.gitignore server/app/__init__.py server/app/config.py server/tests/__init__.py server/tests/conftest.py server/tests/test_config.py
git commit -m "feat(server): 프로젝트 스캐폴드 + 설정 로더"
```

---

### Task 2: 세션 토큰 발급/검증 (HWID 결합)

**Files:**
- Create: `server/app/tokens.py`
- Test: `server/tests/test_tokens.py`

**Interfaces:**
- Consumes: `app.config.Settings` (fields `session_secret`, `token_ttl_seconds`).
- Produces:
  - `issue_token(settings: Settings, user_id: str, hwid: str, roles: list[str], now: int | None = None) -> str`
  - `verify_token(settings: Settings, token: str, hwid: str, now: int | None = None) -> dict | None`
    반환 페이로드 dict: `{"sub": str, "hwid": str, "roles": list[str], "exp": int, "iat": int}`.
    실패(서명불일치/만료/hwid불일치) 시 `None`.

- [ ] **Step 1: 실패하는 테스트 작성**

`server/tests/test_tokens.py`:
```python
from app.tokens import issue_token, verify_token


def test_issue_and_verify_roundtrip(settings):
    tok = issue_token(settings, user_id="u1", hwid="HW1", roles=["sherbet-buyer"], now=1000)
    payload = verify_token(settings, tok, hwid="HW1", now=1000)
    assert payload is not None
    assert payload["sub"] == "u1"
    assert payload["hwid"] == "HW1"
    assert payload["roles"] == ["sherbet-buyer"]
    assert payload["exp"] == 1000 + settings.token_ttl_seconds


def test_verify_rejects_wrong_hwid(settings):
    tok = issue_token(settings, user_id="u1", hwid="HW1", roles=[], now=1000)
    assert verify_token(settings, tok, hwid="HW2", now=1000) is None


def test_verify_rejects_expired(settings):
    tok = issue_token(settings, user_id="u1", hwid="HW1", roles=[], now=1000)
    after = 1000 + settings.token_ttl_seconds + 1
    assert verify_token(settings, tok, hwid="HW1", now=after) is None


def test_verify_rejects_tampered_signature(settings):
    tok = issue_token(settings, user_id="u1", hwid="HW1", roles=[], now=1000)
    tampered = tok[:-2] + ("aa" if not tok.endswith("aa") else "bb")
    assert verify_token(settings, tampered, hwid="HW1", now=1000) is None
```

- [ ] **Step 2: 테스트 실패 확인**

Run: `cd server && python -m pytest tests/test_tokens.py -v`
Expected: FAIL — `ModuleNotFoundError: No module named 'app.tokens'`

- [ ] **Step 3: tokens.py 구현**

`server/app/tokens.py`:
```python
import time

import jwt

from app.config import Settings

_ALG = "HS256"


def _now(now: int | None) -> int:
    return int(time.time()) if now is None else now


def issue_token(
    settings: Settings,
    user_id: str,
    hwid: str,
    roles: list[str],
    now: int | None = None,
) -> str:
    iat = _now(now)
    payload = {
        "sub": user_id,
        "hwid": hwid,
        "roles": roles,
        "iat": iat,
        "exp": iat + settings.token_ttl_seconds,
    }
    return jwt.encode(payload, settings.session_secret, algorithm=_ALG)


def verify_token(
    settings: Settings,
    token: str,
    hwid: str,
    now: int | None = None,
) -> dict | None:
    try:
        payload = jwt.decode(
            token,
            settings.session_secret,
            algorithms=[_ALG],
            options={"verify_exp": False},  # 시간 주입 위해 수동 검사
        )
    except jwt.InvalidTokenError:
        return None
    if payload.get("hwid") != hwid:
        return None
    if int(payload.get("exp", 0)) < _now(now):
        return None
    return payload
```

- [ ] **Step 4: 테스트 통과 확인**

Run: `cd server && python -m pytest tests/test_tokens.py -v`
Expected: PASS (4 passed)

- [ ] **Step 5: 커밋**

```bash
git add server/app/tokens.py server/tests/test_tokens.py
git commit -m "feat(server): HWID 결합 세션 토큰 발급/검증"
```

---

### Task 3: 디스코드 길드 역할 조회

**Files:**
- Create: `server/app/discord_roles.py`
- Test: `server/tests/test_discord_roles.py`

**Interfaces:**
- Consumes: `app.config.Settings` (fields `discord_bot_token`, `discord_guild_id`, `role_buyer_id`).
- Produces:
  - `async get_member_role_ids(settings: Settings, user_id: str) -> list[str] | None`
    — 봇 토큰으로 `GET https://discord.com/api/v10/guilds/{guild}/members/{user}` 호출, `roles` 배열 반환. 멤버 아님(404) → `None`.
  - `async has_buyer_role(settings: Settings, user_id: str) -> bool`
  - `roles_snapshot(settings: Settings, role_ids: list[str]) -> list[str]`
    — 알려진 역할 id를 사람이 읽는 이름으로 매핑 (buyer만: `role_buyer_id` → `"sherbet-buyer"`). 미지의 id는 그대로 문자열 유지.

- [ ] **Step 1: 실패하는 테스트 작성**

`server/tests/test_discord_roles.py`:
```python
import httpx
import pytest
import respx

from app.discord_roles import get_member_role_ids, has_buyer_role, roles_snapshot

API = "https://discord.com/api/v10"


@pytest.mark.asyncio
@respx.mock
async def test_get_member_role_ids_returns_roles(settings):
    route = respx.get(f"{API}/guilds/guild-1/members/u1").mock(
        return_value=httpx.Response(200, json={"roles": ["role-buyer", "role-x"]})
    )
    ids = await get_member_role_ids(settings, "u1")
    assert ids == ["role-buyer", "role-x"]
    assert route.called
    assert route.calls.last.request.headers["Authorization"] == "Bot test-bot-token"


@pytest.mark.asyncio
@respx.mock
async def test_get_member_role_ids_none_when_not_member(settings):
    respx.get(f"{API}/guilds/guild-1/members/u1").mock(
        return_value=httpx.Response(404, json={"message": "Unknown Member"})
    )
    assert await get_member_role_ids(settings, "u1") is None


@pytest.mark.asyncio
@respx.mock
async def test_has_buyer_role_true(settings):
    respx.get(f"{API}/guilds/guild-1/members/u1").mock(
        return_value=httpx.Response(200, json={"roles": ["role-buyer"]})
    )
    assert await has_buyer_role(settings, "u1") is True


@pytest.mark.asyncio
@respx.mock
async def test_has_buyer_role_false_when_missing(settings):
    respx.get(f"{API}/guilds/guild-1/members/u1").mock(
        return_value=httpx.Response(200, json={"roles": ["role-x"]})
    )
    assert await has_buyer_role(settings, "u1") is False


def test_roles_snapshot_maps_buyer(settings):
    assert roles_snapshot(settings, ["role-buyer", "role-x"]) == ["sherbet-buyer", "role-x"]
```

- [ ] **Step 2: 테스트 실패 확인**

Run: `cd server && python -m pytest tests/test_discord_roles.py -v`
Expected: FAIL — `ModuleNotFoundError: No module named 'app.discord_roles'`

- [ ] **Step 3: discord_roles.py 구현**

`server/app/discord_roles.py`:
```python
import httpx

from app.config import Settings

_API = "https://discord.com/api/v10"


async def get_member_role_ids(settings: Settings, user_id: str) -> list[str] | None:
    url = f"{_API}/guilds/{settings.discord_guild_id}/members/{user_id}"
    headers = {"Authorization": f"Bot {settings.discord_bot_token}"}
    async with httpx.AsyncClient(timeout=10.0) as client:
        resp = await client.get(url, headers=headers)
    if resp.status_code == 404:
        return None
    resp.raise_for_status()
    return list(resp.json().get("roles", []))


async def has_buyer_role(settings: Settings, user_id: str) -> bool:
    role_ids = await get_member_role_ids(settings, user_id)
    if role_ids is None:
        return False
    return settings.role_buyer_id in role_ids


def roles_snapshot(settings: Settings, role_ids: list[str]) -> list[str]:
    mapping = {settings.role_buyer_id: "sherbet-buyer"}
    return [mapping.get(rid, rid) for rid in role_ids]
```

- [ ] **Step 4: 테스트 통과 확인**

Run: `cd server && python -m pytest tests/test_discord_roles.py -v`
Expected: PASS (5 passed)

- [ ] **Step 5: 커밋**

```bash
git add server/app/discord_roles.py server/tests/test_discord_roles.py
git commit -m "feat(server): 봇 토큰으로 길드 역할 조회"
```

---

### Task 4: 디스코드 OAuth (authorize URL / code 교환 / user id)

**Files:**
- Create: `server/app/oauth.py`
- Test: `server/tests/test_oauth.py`

**Interfaces:**
- Consumes: `app.config.Settings` (fields `discord_client_id`, `discord_client_secret`, `discord_redirect_uri`).
- Produces:
  - `build_authorize_url(settings: Settings, state: str) -> str` — 디스코드 authorize URL (scope `identify guilds.members.read`).
  - `async exchange_code(settings: Settings, code: str) -> str` — code→access_token 교환, access_token 문자열 반환.
  - `async get_user_id(access_token: str) -> str` — `GET /users/@me` 로 user id 반환.

- [ ] **Step 1: 실패하는 테스트 작성**

`server/tests/test_oauth.py`:
```python
from urllib.parse import parse_qs, urlparse

import httpx
import pytest
import respx

from app.oauth import build_authorize_url, exchange_code, get_user_id

API = "https://discord.com/api/v10"


def test_build_authorize_url(settings):
    url = build_authorize_url(settings, state="nonce123")
    parsed = urlparse(url)
    q = parse_qs(parsed.query)
    assert parsed.netloc == "discord.com"
    assert q["client_id"] == ["test-cid"]
    assert q["state"] == ["nonce123"]
    assert q["response_type"] == ["code"]
    assert q["redirect_uri"] == ["https://test/auth/callback"]
    assert "identify" in q["scope"][0]
    assert "guilds.members.read" in q["scope"][0]


@pytest.mark.asyncio
@respx.mock
async def test_exchange_code(settings):
    route = respx.post(f"{API}/oauth2/token").mock(
        return_value=httpx.Response(200, json={"access_token": "at-xyz", "token_type": "Bearer"})
    )
    token = await exchange_code(settings, code="the-code")
    assert token == "at-xyz"
    body = parse_qs(route.calls.last.request.content.decode())
    assert body["grant_type"] == ["authorization_code"]
    assert body["code"] == ["the-code"]


@pytest.mark.asyncio
@respx.mock
async def test_get_user_id(settings):
    respx.get(f"{API}/users/@me").mock(
        return_value=httpx.Response(200, json={"id": "user-42", "username": "x"})
    )
    assert await get_user_id("at-xyz") == "user-42"
```

- [ ] **Step 2: 테스트 실패 확인**

Run: `cd server && python -m pytest tests/test_oauth.py -v`
Expected: FAIL — `ModuleNotFoundError: No module named 'app.oauth'`

- [ ] **Step 3: oauth.py 구현**

`server/app/oauth.py`:
```python
from urllib.parse import urlencode

import httpx

from app.config import Settings

_API = "https://discord.com/api/v10"
_SCOPE = "identify guilds.members.read"


def build_authorize_url(settings: Settings, state: str) -> str:
    params = {
        "client_id": settings.discord_client_id,
        "response_type": "code",
        "redirect_uri": settings.discord_redirect_uri,
        "scope": _SCOPE,
        "state": state,
        "prompt": "none",
    }
    return f"https://discord.com/oauth2/authorize?{urlencode(params)}"


async def exchange_code(settings: Settings, code: str) -> str:
    data = {
        "client_id": settings.discord_client_id,
        "client_secret": settings.discord_client_secret,
        "grant_type": "authorization_code",
        "code": code,
        "redirect_uri": settings.discord_redirect_uri,
    }
    async with httpx.AsyncClient(timeout=10.0) as client:
        resp = await client.post(f"{_API}/oauth2/token", data=data)
    resp.raise_for_status()
    return resp.json()["access_token"]


async def get_user_id(access_token: str) -> str:
    headers = {"Authorization": f"Bearer {access_token}"}
    async with httpx.AsyncClient(timeout=10.0) as client:
        resp = await client.get(f"{_API}/users/@me", headers=headers)
    resp.raise_for_status()
    return resp.json()["id"]
```

- [ ] **Step 4: 테스트 통과 확인**

Run: `cd server && python -m pytest tests/test_oauth.py -v`
Expected: PASS (3 passed)

- [ ] **Step 5: 커밋**

```bash
git add server/app/oauth.py server/tests/test_oauth.py
git commit -m "feat(server): 디스코드 OAuth authorize/exchange/user-id"
```

---

### Task 5: state→결과 임시 보관소 (device-flow 폴링)

**Files:**
- Create: `server/app/store.py`
- Test: `server/tests/test_store.py`

**Interfaces:**
- Produces: `PendingStore` 클래스
  - `put_pending(state: str, hwid: str) -> None` — 로그인 시작 시 state와 그 요청의 hwid를 등록.
  - `get_hwid(state: str) -> str | None` — 콜백에서 state에 묶인 hwid 조회.
  - `set_result(state: str, token: str) -> None` — 콜백 완료 시 발급 토큰 저장.
  - `set_denied(state: str, reason: str) -> None` — 역할 없음 등 실패 저장.
  - `pop_result(state: str) -> dict | None` — 클라 폴링: `{"status": "ready", "token": ...}` / `{"status": "denied", "reason": ...}` / 아직이면 `{"status": "pending"}` / 미지의 state면 `None`. ready/denied는 1회 반환 후 삭제.

- [ ] **Step 1: 실패하는 테스트 작성**

`server/tests/test_store.py`:
```python
from app.store import PendingStore


def test_pending_then_ready_flow():
    s = PendingStore()
    s.put_pending("st1", "HW1")
    assert s.get_hwid("st1") == "HW1"
    assert s.pop_result("st1") == {"status": "pending"}
    s.set_result("st1", "tok-abc")
    assert s.pop_result("st1") == {"status": "ready", "token": "tok-abc"}
    # 1회 소비 후 사라짐
    assert s.pop_result("st1") is None


def test_denied_flow():
    s = PendingStore()
    s.put_pending("st2", "HW2")
    s.set_denied("st2", "no_buyer_role")
    assert s.pop_result("st2") == {"status": "denied", "reason": "no_buyer_role"}
    assert s.pop_result("st2") is None


def test_unknown_state():
    s = PendingStore()
    assert s.get_hwid("nope") is None
    assert s.pop_result("nope") is None
```

- [ ] **Step 2: 테스트 실패 확인**

Run: `cd server && python -m pytest tests/test_store.py -v`
Expected: FAIL — `ModuleNotFoundError: No module named 'app.store'`

- [ ] **Step 3: store.py 구현**

`server/app/store.py`:
```python
import threading


class PendingStore:
    def __init__(self) -> None:
        self._lock = threading.Lock()
        self._hwid: dict[str, str] = {}
        self._result: dict[str, dict] = {}

    def put_pending(self, state: str, hwid: str) -> None:
        with self._lock:
            self._hwid[state] = hwid

    def get_hwid(self, state: str) -> str | None:
        with self._lock:
            return self._hwid.get(state)

    def set_result(self, state: str, token: str) -> None:
        with self._lock:
            self._result[state] = {"status": "ready", "token": token}

    def set_denied(self, state: str, reason: str) -> None:
        with self._lock:
            self._result[state] = {"status": "denied", "reason": reason}

    def pop_result(self, state: str) -> dict | None:
        with self._lock:
            if state not in self._hwid:
                return None
            if state in self._result:
                res = self._result.pop(state)
                self._hwid.pop(state, None)
                return res
            return {"status": "pending"}
```

- [ ] **Step 4: 테스트 통과 확인**

Run: `cd server && python -m pytest tests/test_store.py -v`
Expected: PASS (3 passed)

- [ ] **Step 5: 커밋**

```bash
git add server/app/store.py server/tests/test_store.py
git commit -m "feat(server): device-flow 폴링용 state 보관소"
```

---

### Task 6: FastAPI 앱 + 엔드포인트 조립

**Files:**
- Create: `server/app/main.py`
- Test: `server/tests/test_endpoints.py`

**Interfaces:**
- Consumes: `get_settings` (config), `build_authorize_url`/`exchange_code`/`get_user_id` (oauth), `get_member_role_ids`/`has_buyer_role`/`roles_snapshot` (discord_roles), `issue_token`/`verify_token` (tokens), `PendingStore` (store).
- Produces: FastAPI `app` with routes:
  - `GET /health` → `{"status": "ok"}`
  - `GET /auth/discord?state=<>&hwid=<>` → 302 리다이렉트 to authorize URL; store에 (state→hwid) 등록.
  - `GET /auth/callback?code=<>&state=<>` → code 교환 → user id → 역할 조회 → buyer 있으면 토큰 발급 후 `set_result`, 없으면 `set_denied`. 브라우저에 간단 HTML 응답.
  - `GET /auth/poll?state=<>` → `pop_result(state)` 그대로 반환. 미지의 state → 404.
  - `POST /auth/verify` (body `{"token": str, "hwid": str}`) → 토큰 검증 + 최신 역할 재조회. `{"valid": bool, "roles": [...], "sub": str}` 또는 `{"valid": false}`.

- [ ] **Step 1: 실패하는 테스트 작성**

`server/tests/test_endpoints.py`:
```python
import httpx
import respx
from fastapi.testclient import TestClient

import app.main as main_module
from app.config import get_settings
from app.main import app
from app.store import PendingStore
from app.tokens import issue_token

API = "https://discord.com/api/v10"


def _override(settings):
    app.dependency_overrides[get_settings] = lambda: settings


def _reset():
    app.dependency_overrides.clear()
    main_module.store = PendingStore()  # 테스트 간 상태 격리


def test_health(settings):
    _override(settings)
    try:
        client = TestClient(app)
        assert client.get("/health").json() == {"status": "ok"}
    finally:
        _reset()


def test_auth_discord_redirects_and_registers_state(settings):
    _override(settings)
    try:
        _reset(); _override(settings)
        client = TestClient(app)
        r = client.get("/auth/discord", params={"state": "s1", "hwid": "HW1"},
                       follow_redirects=False)
        assert r.status_code == 307 or r.status_code == 302
        assert "discord.com/oauth2/authorize" in r.headers["location"]
        assert main_module.store.get_hwid("s1") == "HW1"
    finally:
        _reset()


@respx.mock
def test_callback_issues_token_for_buyer(settings):
    _reset(); _override(settings)
    try:
        main_module.store.put_pending("s2", "HW2")
        respx.post(f"{API}/oauth2/token").mock(
            return_value=httpx.Response(200, json={"access_token": "at"}))
        respx.get(f"{API}/users/@me").mock(
            return_value=httpx.Response(200, json={"id": "user-9"}))
        respx.get(f"{API}/guilds/guild-1/members/user-9").mock(
            return_value=httpx.Response(200, json={"roles": ["role-buyer"]}))
        client = TestClient(app)
        r = client.get("/auth/callback", params={"code": "c", "state": "s2"})
        assert r.status_code == 200
        poll = client.get("/auth/poll", params={"state": "s2"}).json()
        assert poll["status"] == "ready"
        assert poll["token"]
    finally:
        _reset()


@respx.mock
def test_callback_denies_non_buyer(settings):
    _reset(); _override(settings)
    try:
        main_module.store.put_pending("s3", "HW3")
        respx.post(f"{API}/oauth2/token").mock(
            return_value=httpx.Response(200, json={"access_token": "at"}))
        respx.get(f"{API}/users/@me").mock(
            return_value=httpx.Response(200, json={"id": "user-x"}))
        respx.get(f"{API}/guilds/guild-1/members/user-x").mock(
            return_value=httpx.Response(200, json={"roles": ["role-other"]}))
        client = TestClient(app)
        client.get("/auth/callback", params={"code": "c", "state": "s3"})
        poll = client.get("/auth/poll", params={"state": "s3"}).json()
        assert poll["status"] == "denied"
        assert poll["reason"] == "no_buyer_role"
    finally:
        _reset()


def test_poll_unknown_state_404(settings):
    _reset(); _override(settings)
    try:
        client = TestClient(app)
        assert client.get("/auth/poll", params={"state": "ghost"}).status_code == 404
    finally:
        _reset()


@respx.mock
def test_verify_valid_token_rechecks_roles(settings):
    _reset(); _override(settings)
    try:
        tok = issue_token(settings, "user-9", "HW9", ["sherbet-buyer"], now=1000)
        respx.get(f"{API}/guilds/guild-1/members/user-9").mock(
            return_value=httpx.Response(200, json={"roles": ["role-buyer", "role-x"]}))
        client = TestClient(app)
        r = client.post("/auth/verify", json={"token": tok, "hwid": "HW9"})
        body = r.json()
        assert body["valid"] is True
        assert body["sub"] == "user-9"
        assert "sherbet-buyer" in body["roles"]
    finally:
        _reset()


def test_verify_bad_hwid(settings):
    _reset(); _override(settings)
    try:
        tok = issue_token(settings, "user-9", "HW9", ["sherbet-buyer"], now=1000)
        client = TestClient(app)
        r = client.post("/auth/verify", json={"token": tok, "hwid": "WRONG"})
        assert r.json() == {"valid": False}
    finally:
        _reset()
```

- [ ] **Step 2: 테스트 실패 확인**

Run: `cd server && python -m pytest tests/test_endpoints.py -v`
Expected: FAIL — `ModuleNotFoundError: No module named 'app.main'`

- [ ] **Step 3: main.py 구현**

`server/app/main.py`:
```python
from fastapi import Depends, FastAPI, HTTPException
from fastapi.responses import HTMLResponse, RedirectResponse
from pydantic import BaseModel

from app.config import Settings, get_settings
from app.discord_roles import get_member_role_ids, has_buyer_role, roles_snapshot
from app.oauth import build_authorize_url, exchange_code, get_user_id
from app.store import PendingStore
from app.tokens import issue_token, verify_token

app = FastAPI(title="Sherbet Auth")
store = PendingStore()


class VerifyBody(BaseModel):
    token: str
    hwid: str


@app.get("/health")
def health() -> dict:
    return {"status": "ok"}


@app.get("/auth/discord")
def auth_discord(state: str, hwid: str, settings: Settings = Depends(get_settings)):
    store.put_pending(state, hwid)
    return RedirectResponse(build_authorize_url(settings, state))


@app.get("/auth/callback", response_class=HTMLResponse)
async def auth_callback(code: str, state: str, settings: Settings = Depends(get_settings)):
    hwid = store.get_hwid(state)
    if hwid is None:
        raise HTTPException(status_code=400, detail="unknown_state")
    access_token = await exchange_code(settings, code)
    user_id = await get_user_id(access_token)
    role_ids = await get_member_role_ids(settings, user_id)
    if role_ids is None or settings.role_buyer_id not in role_ids:
        store.set_denied(state, "no_buyer_role")
        return HTMLResponse("<h2>인증 실패</h2><p>구매자 역할이 없습니다. 창을 닫아주세요.</p>", status_code=200)
    roles = roles_snapshot(settings, role_ids)
    token = issue_token(settings, user_id, hwid, roles)
    store.set_result(state, token)
    return HTMLResponse("<h2>인증 완료</h2><p>Sherbet으로 돌아가세요. 이 창은 닫아도 됩니다.</p>")


@app.get("/auth/poll")
def auth_poll(state: str) -> dict:
    result = store.pop_result(state)
    if result is None:
        raise HTTPException(status_code=404, detail="unknown_state")
    return result


@app.post("/auth/verify")
async def auth_verify(body: VerifyBody, settings: Settings = Depends(get_settings)) -> dict:
    payload = verify_token(settings, body.token, body.hwid)
    if payload is None:
        return {"valid": False}
    role_ids = await get_member_role_ids(settings, payload["sub"])
    if role_ids is None or settings.role_buyer_id not in role_ids:
        return {"valid": False}
    return {"valid": True, "sub": payload["sub"], "roles": roles_snapshot(settings, role_ids)}
```

- [ ] **Step 4: 테스트 통과 확인**

Run: `cd server && python -m pytest tests/test_endpoints.py -v`
Expected: PASS (7 passed)

- [ ] **Step 5: 전체 스위트 통과 확인**

Run: `cd server && python -m pytest -v`
Expected: PASS (전체, 약 22 passed)

- [ ] **Step 6: 로컬 수동 스모크 (선택, 목킹 없이 앱 부팅만 확인)**

Run: `cd server && DISCORD_CLIENT_ID=x DISCORD_CLIENT_SECRET=x DISCORD_BOT_TOKEN=x DISCORD_GUILD_ID=x DISCORD_REDIRECT_URI=https://x/auth/callback SESSION_SECRET=x ROLE_BUYER_ID=x python -c "from app.main import app; print('app ok', [r.path for r in app.routes])"`
Expected: `app ok [...'/health', '/auth/discord', '/auth/callback', '/auth/poll', '/auth/verify'...]`

- [ ] **Step 7: 커밋**

```bash
git add server/app/main.py server/tests/test_endpoints.py
git commit -m "feat(server): FastAPI 인증 엔드포인트 조립 (discord/callback/poll/verify)"
```

---

## Self-Review

**1. Spec coverage (스펙 §8 API 계약 대비):**
- `/auth/discord` (OAuth 시작) → Task 6 ✅
- `/auth/verify` (토큰+역할 재검증) → Task 6 ✅
- 세션토큰 HWID 결합 → Task 2 ✅
- 봇 경유 역할 조회 → Task 3 ✅
- `sherbet-buyer` 게이트 → Task 3/6 ✅
- 24h TTL → Task 2 (`token_ttl_seconds=86400`) ✅
- 콜백 device-flow(브라우저→서버→클라 회수) → Task 5 store + Task 6 poll ✅
- 갭: `/content/*` 는 Phase 1/2 범위 → 이 계획에서 제외(정상). 오프라인 그레이스는 클라 동작 → Phase 0b.

**2. Placeholder scan:** "TBD/적절히 처리" 류 없음. 모든 코드 스텝에 실제 코드 포함. ✅

**3. Type consistency:**
- `Settings` 필드명 전 태스크 일관 (`role_buyer_id`, `discord_guild_id`, `session_secret`, `token_ttl_seconds`) ✅
- `issue_token`/`verify_token` 시그니처 Task 2 정의 == Task 6 사용 ✅
- `get_member_role_ids`/`has_buyer_role`/`roles_snapshot` Task 3 == Task 6 사용 ✅
- `PendingStore` 메서드명 Task 5 정의 == Task 6 사용 (`put_pending`/`get_hwid`/`set_result`/`set_denied`/`pop_result`) ✅
- `build_authorize_url`/`exchange_code`/`get_user_id` Task 4 == Task 6 ✅

문제 없음.
