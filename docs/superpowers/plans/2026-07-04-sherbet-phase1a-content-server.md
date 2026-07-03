# Sherbet Phase 1a — 원격 테마 콘텐츠 서버 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 기존 FastAPI 인증 서버에 `GET /content/me` 를 추가해, 로그인한 사용자의 디스코드 역할에 맞는 원격 테마 JSON 목록을 반환한다 (서버 `themes.json` 편집 + 역할 부여만으로 새 테마 배포, 재빌드 0).

**Architecture:** Bearer JWT를 hwid 없이 검증(서명+만료)해 `sub`를 얻고, 디스코드 역할을 라이브 재조회한 뒤, 디스크의 `content/themes.json`(어드민 편집, mtime 캐시)에서 각 테마의 `role`(숫자 역할 ID)이 null이거나 사용자 역할에 포함된 것만 골라 `{"themes":[...]}` 로 반환한다. 순수 필터 로직은 네트워크 없이 pytest로, 엔드포인트는 respx로 디스코드를 목킹해 검증한다.

**Tech Stack:** Python 3.12, FastAPI, PyJWT(HS256), httpx, pytest, pytest-asyncio, respx. 리포 `~/reshade/server`.

## Global Constraints

- 리포 서버 디렉터리 `~/reshade/server`. 브랜치 `sherbet-base`. Phase 0a(인증 서버)는 완료·배포됨 — 기존 모듈을 **확장**한다(새로 안 만듦).
- 재활용: `app/tokens.py`(`verify_token(settings, token, hwid, now)` — hwid 필수라 hwid-옵셔널 변형 추가), `app/discord_roles.py`(`get_member_role_ids(settings, user_id) -> list[str] | None`, 404→None, 그 외 httpx 에러는 raise), `app/config.py`(`Settings`, `get_settings()`).
- **themes.json의 `role` 은 디스코드 역할의 숫자 ID 문자열**(`role_buyer_id` 처럼). `get_member_role_ids` 가 숫자 ID를 반환하므로 이름이 아니라 ID로 매칭. `null`/생략이면 무료.
- 테스트: 기존 `server/tests/` 패턴(conftest.py `settings` 픽스처, respx로 `https://discord.com/api/v10` 목킹). 실행 `cd server && python -m pytest -q`(현재 37 passed).
- 단일 워커 배포(전역 상태) 유지. `/content/*` 는 Bearer 없으면 401.
- **커밋 메시지에 클로드/AI 저작권·공동작성 문구 금지.**
- 이 플랜은 Phase 1의 **서버 절반(1a)**. 클라(1b 동적 테마)는 별도 플랜. `/content/me` 응답에 `presets`/`effects` 는 넣지 않는다(Phase 2).

---

## File Structure

**신규:**
- `server/content/themes.json` — 어드민이 편집하는 원격 테마 정의 배열(초기엔 데모 1개). 서버 배포물의 일부.
- `server/app/content.py` — themes.json 로더(mtime 캐시) + 역할 필터 순수 함수. 네트워크 없음 → 단위테스트 대상.
- `server/tests/test_content.py` — content.py 순수 로직 테스트.

**수정:**
- `server/app/tokens.py` — `verify_token` 을 hwid 옵셔널로(또는 hwid 없이 검증하는 헬퍼 추가).
- `server/app/main.py` — `GET /content/me` 라우트.
- `server/tests/test_tokens.py` — hwid-옵셔널 검증 케이스.
- `server/tests/test_endpoints.py` — `/content/me` 엔드포인트 테스트.
- `server/README.md` — `/content/me` + themes.json 편집법 문서.

---

## Task 1: `verify_token` hwid 옵셔널 (콘텐츠는 신원만 필요)

**Files:**
- Modify: `server/app/tokens.py`
- Test: `server/tests/test_tokens.py`

**Interfaces:**
- Produces: `verify_token(settings, token, hwid=None, now=None) -> dict | None` — `hwid` 가 `None` 이면 hwid 클레임 검사를 건너뛰고 서명+만료만 본다. 기존 호출부(`hwid` 를 넘기는 `/auth/verify`)는 그대로 동작.

- [ ] **Step 1: 실패 테스트 추가 — `server/tests/test_tokens.py`**

기존 파일 끝에 추가:
```python
def test_verify_token_skips_hwid_when_none(settings):
    from app.tokens import issue_token, verify_token
    tok = issue_token(settings, "user-1", "HW-A", ["sherbet-buyer"])
    # hwid=None → hwid 불일치여도(검사 안 함) 통과, sub 확인
    payload = verify_token(settings, tok, hwid=None)
    assert payload is not None and payload["sub"] == "user-1"


def test_verify_token_none_hwid_still_checks_expiry(settings):
    from app.tokens import issue_token, verify_token
    # 이미 만료된 토큰(now 주입) → hwid=None 이어도 만료로 거부
    tok = issue_token(settings, "user-1", "HW-A", ["sherbet-buyer"], now=1000)
    assert verify_token(settings, tok, hwid=None, now=10**12) is None
```

- [ ] **Step 2: 실패 확인**

Run: `cd server && python -m pytest tests/test_tokens.py -q`
Expected: FAIL — `verify_token()` 이 `hwid` 위치인자를 요구(또는 None 처리 없음).

- [ ] **Step 3: 구현 — `server/app/tokens.py` 의 `verify_token` 수정**

시그니처와 hwid 검사만 바꾼다(나머지 동일):
```python
def verify_token(
    settings: Settings,
    token: str,
    hwid: str | None = None,
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
    if hwid is not None and payload.get("hwid") != hwid:  # hwid=None 이면 신원만 검증(콘텐츠 배포용)
        return None
    if int(payload.get("exp", 0)) < _now(now):
        return None
    return payload
```

- [ ] **Step 4: 통과 확인(회귀 포함)**

Run: `cd server && python -m pytest tests/test_tokens.py -q`
Expected: PASS(신규 2개 + 기존 hwid 케이스 전부).

- [ ] **Step 5: Commit**

```bash
git add server/app/tokens.py server/tests/test_tokens.py
git commit -m "auth: verify_token hwid 옵셔널(콘텐츠 배포는 신원만 검증)"
```

---

## Task 2: `content.py` — themes.json 로더 + 역할 필터 (순수 로직)

**Files:**
- Create: `server/app/content.py`
- Create: `server/content/themes.json`
- Test: `server/tests/test_content.py`

**Interfaces:**
- Produces:
  - `load_themes(path: str, mtime_cache: dict) -> list[dict]` — path의 JSON 배열을 읽어 반환. `mtime_cache`(호출자 소유 dict)로 파일 mtime 이 안 바뀌면 캐시 반환, 바뀌면 다시 읽음. 파일 없음/깨짐 → `[]`.
  - `entitled_themes(themes: list[dict], role_ids: list[str]) -> list[dict]` — 각 테마의 `role` 이 falsy(None/"")이거나 `role_ids` 에 포함되면 포함. 반환 항목은 클라 소비용으로 `role` 키를 제거한 얕은 복사.

- [ ] **Step 1: 데모 `server/content/themes.json` 작성**

```json
[
  {
    "id": "aurora",
    "display_name": "Aurora Sky",
    "colors": {
      "bg0": "#0a1f1aff", "bg1": "#0f2e24ff", "bg2": "#123a2cff",
      "panel": "#122e26b8", "panel_alt": "#183a2fd9",
      "chip": "#134233ff", "border": "#5df0c040",
      "text": "#eafff6ff", "text_dim": "#8fbfaeff",
      "accent": "#5df0c0ff", "accent2": "#a7ffe3ff",
      "glow": "#5df0c073"
    },
    "particle": "leaf",
    "hue_cycle": false,
    "role": "900000000000000001"
  }
]
```

- [ ] **Step 2: 실패 테스트 — `server/tests/test_content.py`**

```python
import json
from pathlib import Path

from app.content import load_themes, entitled_themes


def _write(tmp_path, data):
    p = tmp_path / "themes.json"
    p.write_text(json.dumps(data), encoding="utf-8")
    return str(p)


def test_load_missing_returns_empty(tmp_path):
    assert load_themes(str(tmp_path / "nope.json"), {}) == []


def test_load_and_mtime_cache(tmp_path):
    path = _write(tmp_path, [{"id": "a", "role": None}])
    cache = {}
    first = load_themes(path, cache)
    assert first == [{"id": "a", "role": None}]
    # 파일 안 바뀌면 같은 (캐시된) 객체 반환
    assert load_themes(path, cache) is first


def test_load_broken_json_returns_empty(tmp_path):
    p = tmp_path / "themes.json"
    p.write_text("{not json", encoding="utf-8")
    assert load_themes(str(p), {}) == []


def test_entitled_free_and_role_gated():
    themes = [
        {"id": "free", "role": None, "display_name": "F"},
        {"id": "gold", "role": "111", "display_name": "G"},
        {"id": "plat", "role": "222", "display_name": "P"},
    ]
    out = entitled_themes(themes, ["111"])
    ids = [t["id"] for t in out]
    assert ids == ["free", "gold"]  # free(무료) + gold(역할 111 보유); plat 제외
    # 반환에는 role 키가 없어야(클라에 노출 안 함)
    assert all("role" not in t for t in out)


def test_entitled_empty_roles_gets_only_free():
    themes = [{"id": "free", "role": None}, {"id": "gold", "role": "111"}]
    assert [t["id"] for t in entitled_themes(themes, [])] == ["free"]
```

- [ ] **Step 3: 실패 확인**

Run: `cd server && python -m pytest tests/test_content.py -q`
Expected: FAIL — `app.content` 없음.

- [ ] **Step 4: 구현 — `server/app/content.py`**

```python
import json
import os


def load_themes(path: str, mtime_cache: dict) -> list[dict]:
    """themes.json(배열)을 읽는다. mtime_cache 로 파일이 안 바뀌면 캐시 반환.
    파일 없음/깨짐이면 빈 목록(서비스가 죽지 않게)."""
    try:
        mtime = os.path.getmtime(path)
    except OSError:
        mtime_cache.pop("mtime", None)
        mtime_cache.pop("data", None)
        return []
    if mtime_cache.get("mtime") == mtime and "data" in mtime_cache:
        return mtime_cache["data"]
    try:
        with open(path, encoding="utf-8") as f:
            data = json.load(f)
    except (OSError, ValueError):
        return []
    if not isinstance(data, list):
        data = []
    mtime_cache["mtime"] = mtime
    mtime_cache["data"] = data
    return data


def entitled_themes(themes: list[dict], role_ids: list[str]) -> list[dict]:
    """role 이 falsy(무료)이거나 사용자 역할 ID 목록에 포함된 테마만, role 키를 제거해 반환."""
    role_set = set(role_ids or [])
    out = []
    for t in themes:
        role = t.get("role")
        if not role or role in role_set:
            out.append({k: v for k, v in t.items() if k != "role"})
    return out
```

- [ ] **Step 5: 통과 확인**

Run: `cd server && python -m pytest tests/test_content.py -q`
Expected: PASS(5개).

- [ ] **Step 6: Commit**

```bash
git add server/app/content.py server/content/themes.json server/tests/test_content.py
git commit -m "content: themes.json 로더(mtime 캐시) + 역할 필터 순수 로직(+테스트)"
```

---

## Task 3: `GET /content/me` 엔드포인트

**Files:**
- Modify: `server/app/main.py`
- Test: `server/tests/test_endpoints.py`

**Interfaces:**
- Consumes: `verify_token(settings, token, hwid=None)`(Task 1), `load_themes`/`entitled_themes`(Task 2), `get_member_role_ids`(기존).
- Produces: `GET /content/me` — 헤더 `Authorization: Bearer <jwt>`. 200 `{"themes":[...]}` / 401(토큰 없음·무효·만료) / 503(디스코드 재조회 실패).

- [ ] **Step 1: 실패 테스트 — `server/tests/test_endpoints.py` 끝에 추가**

```python
import app.content as content_module


def _themes_file(tmp_path, data):
    import json
    p = tmp_path / "themes.json"
    p.write_text(json.dumps(data), encoding="utf-8")
    return str(p)


@respx.mock
def test_content_me_returns_entitled(settings, tmp_path, monkeypatch):
    _reset(); _override(settings)
    try:
        # themes.json 경로를 테스트 파일로
        path = _themes_file(tmp_path, [
            {"id": "free", "role": None, "display_name": "F", "colors": {}, "particle": "leaf", "hue_cycle": False},
            {"id": "gold", "role": "role-gold", "display_name": "G", "colors": {}, "particle": "spark", "hue_cycle": False},
            {"id": "plat", "role": "role-plat", "display_name": "P", "colors": {}, "particle": "heart", "hue_cycle": False},
        ])
        monkeypatch.setattr(main_module, "THEMES_PATH", path)
        main_module.THEMES_CACHE.clear()
        tok = issue_token(settings, "user-9", "HW9", ["sherbet-buyer"])
        respx.get(f"{API}/guilds/guild-1/members/user-9").mock(
            return_value=httpx.Response(200, json={"roles": ["role-buyer", "role-gold"]}))
        client = TestClient(app)
        r = client.get("/content/me", headers={"Authorization": f"Bearer {tok}"})
        assert r.status_code == 200
        ids = [t["id"] for t in r.json()["themes"]]
        assert ids == ["free", "gold"]  # plat 제외(역할 없음)
        assert all("role" not in t for t in r.json()["themes"])
    finally:
        _reset()


def test_content_me_no_bearer_401(settings):
    _reset(); _override(settings)
    try:
        client = TestClient(app)
        assert client.get("/content/me").status_code == 401
    finally:
        _reset()


def test_content_me_bad_token_401(settings):
    _reset(); _override(settings)
    try:
        client = TestClient(app)
        r = client.get("/content/me", headers={"Authorization": "Bearer not.a.jwt"})
        assert r.status_code == 401
    finally:
        _reset()


@respx.mock
def test_content_me_discord_down_503(settings, tmp_path, monkeypatch):
    _reset(); _override(settings)
    try:
        path = _themes_file(tmp_path, [{"id": "free", "role": None}])
        monkeypatch.setattr(main_module, "THEMES_PATH", path)
        main_module.THEMES_CACHE.clear()
        tok = issue_token(settings, "user-9", "HW9", ["sherbet-buyer"])
        respx.get(f"{API}/guilds/guild-1/members/user-9").mock(
            return_value=httpx.Response(500, json={"error": "boom"}))
        client = TestClient(app)
        r = client.get("/content/me", headers={"Authorization": f"Bearer {tok}"})
        assert r.status_code == 503
    finally:
        _reset()
```

- [ ] **Step 2: 실패 확인**

Run: `cd server && python -m pytest tests/test_endpoints.py -q`
Expected: FAIL — `/content/me` 없음(404) + `main_module.THEMES_PATH`/`THEMES_CACHE` 미정의.

- [ ] **Step 3: 구현 — `server/app/main.py`**

상단 import 에 추가:
```python
import os
from fastapi import Header
from app.content import load_themes, entitled_themes
```
`store = PendingStore()` 근처(모듈 전역)에 추가:
```python
# 원격 테마 정의 파일 경로 + mtime 캐시(요청마다 파일 stat, 안 바뀌면 캐시)
THEMES_PATH = os.path.join(os.path.dirname(os.path.dirname(__file__)), "content", "themes.json")
THEMES_CACHE: dict = {}
```
라우트 추가(다른 라우트들 아래):
```python
@app.get("/content/me")
async def content_me(
    authorization: str | None = Header(default=None),
    settings: Settings = Depends(get_settings),
):
    if not authorization or not authorization.startswith("Bearer "):
        raise HTTPException(status_code=401, detail="missing_bearer")
    token = authorization[len("Bearer "):]
    payload = verify_token(settings, token, hwid=None)  # 콘텐츠는 신원만 검증
    if payload is None:
        raise HTTPException(status_code=401, detail="invalid_token")
    try:
        role_ids = await get_member_role_ids(settings, payload["sub"])
    except (httpx.HTTPError, KeyError, ValueError):
        return JSONResponse(status_code=503, content={"error": "upstream_unavailable"})
    themes = load_themes(THEMES_PATH, THEMES_CACHE)
    return {"themes": entitled_themes(themes, role_ids or [])}
```

- [ ] **Step 4: 통과 확인(전체 회귀)**

Run: `cd server && python -m pytest -q`
Expected: PASS(기존 37 + Task1 2 + Task2 5 + Task3 4 = 48).

- [ ] **Step 5: Commit**

```bash
git add server/app/main.py server/tests/test_endpoints.py
git commit -m "content: GET /content/me — 역할 기반 원격 테마 매니페스트(401/503 처리)"
```

---

## Task 4: 문서 — themes.json 편집 + 엔드포인트

**Files:**
- Modify: `server/README.md`

**Interfaces:** 없음(문서).

- [ ] **Step 1: `server/README.md` 에 섹션 추가**

파일 끝에 추가:
```markdown

## 원격 테마 (`/content/me`)

- `GET /content/me` — 헤더 `Authorization: Bearer <세션토큰>`. 로그인 사용자의 디스코드 역할에 맞는 테마 JSON 목록 `{"themes":[...]}` 반환. 토큰 무효/만료 → 401, 디스코드 조회 실패 → 503.
- **새 테마 배포(재빌드 불필요):**
  1. `server/content/themes.json` 배열에 테마 항목 추가(스키마: 설계 문서 §13.1 — id/display_name/colors 12색 #rrggbbaa/particle/hue_cycle/role).
  2. `role` 은 디스코드 역할의 **숫자 ID**(무료면 `null`). 역할을 새로 만들었다면 개발자 모드로 역할 우클릭 → ID 복사.
  3. 구매자에게 디스코드에서 해당 역할 부여.
  4. 끝. 서버 재시작 불필요(파일 mtime 이 바뀌면 다음 요청에서 자동 반영).
- themes.json 은 요청마다 stat 되며, 내용이 안 바뀌면 메모리 캐시를 쓴다(단일 워커 전제).
```

- [ ] **Step 2: Commit**

```bash
git add server/README.md
git commit -m "docs: server README에 /content/me + themes.json 편집법 추가"
```

---

## Self-Review 메모(작성자)

- **스펙 커버리지:** §13.1 스키마=Task 2 themes.json/데모. §13.2 /content/me(Bearer·hwid없이 검증·역할 라이브 재조회·필터·401/503)=Task 1+2+3. §13.7 1a-server 범위=이 플랜 전부. presets/effects 제외(Phase 2) 준수.
- **타입 일관성:** `verify_token(...,hwid=None)`(T1) ↔ `/content/me`(T3) 호출 일치. `load_themes(path, mtime_cache)`/`entitled_themes(themes, role_ids)`(T2) ↔ main.py 호출 일치. `THEMES_PATH`/`THEMES_CACHE` 전역명 T3 내부 일관.
- **역할=숫자 ID:** themes.json `role` 은 디스코드 숫자 역할 ID(테스트에서 "role-gold" 등 문자열로 목킹하지만 실제로도 문자열 ID). `get_member_role_ids` 반환과 동일 타입(str) 비교 — OK.
- **위협 모델:** 하드 게이트(서버가 미권한 테마 JSON 자체를 안 줌) — 클라 신뢰와 무관하게 서버가 배포 통제. 스펙 §13.6 부합.
