# Sherbet Phase 2a — 프리셋·fx 서버 배포 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 기존 FastAPI 인증 서버에 프리셋·fx 원격 배포를 추가한다 — `/content/me` 매니페스트에 권한 프리셋/이펙트를 포함시키고, `/content/file/<id>` 로 파일 바이트를 per-id 역할 재검증 후 서빙한다.

**Architecture:** Phase 1a 의 테마 로더(`load_themes`/`entitled_themes`)를 이름만 일반화(`load_manifest`/`entitled_items`)해 프리셋·이펙트에 재사용한다. `/content/me` 는 themes/presets/effects 세 배열을 역할 필터해 반환. `/content/file/<id>` 는 Bearer 검증→디스코드 역할 라이브 재조회→그 id 아이템의 role 재확인→`files/<id>` 바이트 서빙(경로탈출 방어). 순수 로직(로더·필터·조회)은 host pytest, 엔드포인트는 respx 로 디스코드 목킹.

**Tech Stack:** Python 3.12, FastAPI, PyJWT(HS256), httpx, respx, pytest. 서버 리포 `~/reshade/server`.

## Global Constraints

- **커밋 메시지에 Claude/AI 저작권·공동저자 문구 절대 금지** (CLAUDE.md). 커밋 본문 한국어, 무해한 설명만.
- **단일 워커 전제 유지** — `PendingStore`·mtime 캐시가 프로세스 메모리 기반(`server/README.md`). 새 캐시도 동일.
- **`/content/*` 는 세션토큰(Bearer) 없으면 401.** hwid 는 검사 안 함(콘텐츠는 신원만, `verify_token(settings, token, hwid=None)`).
- **역할 필터 규칙(스펙 §14.3):** 아이템의 `role` 이 falsy(None/빈값=무료)이거나 사용자 역할 ID 목록에 포함되면 통과. 응답에서 `role` 필드 제거.
- **`role` = 디스코드 숫자 역할 ID 문자열**(스펙 §13.4/§14.4). 이름 아님.
- **per-id 재검증(스펙 §14.3 핵심 보안):** `/content/file/<id>` 는 매니페스트 필터만 믿지 않고 파일 서빙 시점에 그 id 의 role 을 사용자가 보유하는지 반드시 재확인. 미보유 → 403.
- **경로탈출 방어:** `id` 는 presets.json+effects.json 화이트리스트에서만 조회. 서버는 `files/<id>` 를 고정 규칙으로만 구성(사용자 입력으로 경로 구성 금지). 최종 실경로가 `files/` 밖이면 거부.
- **디스코드 조회 실패 → 503**(재시도 신호), `(httpx.HTTPError, KeyError, ValueError)` 로 처리(기존 `/content/me` 패턴과 동일).
- **기존 48 테스트 계속 통과.** 이름 변경 시 호출부·테스트 동반 수정.

---

## File Structure

- **Modify `server/app/content.py`** — `load_themes`→`load_manifest`, `entitled_themes`→`entitled_items` 로 일반화(동작 동일, 이름만). `find_item(items, item_id)` + `is_entitled(item, role_ids)` 신규(파일 서빙용 순수 로직).
- **Modify `server/app/main.py`** — content import 갱신, `PRESETS_PATH`/`EFFECTS_PATH`/`FILES_DIR` + 캐시 글로벌 추가, `/content/me` 확장(presets/effects), `GET /content/file/{item_id}` 신규 라우트.
- **Create `server/content/presets.json`**, **`server/content/effects.json`** — 데모 배열(빈 배열 또는 예시 1개). **`server/content/files/`** — 파일 바이트 저장 디렉터리(데모 파일 1개).
- **Modify `server/tests/test_content.py`** — 이름 변경 반영 + `find_item`/`is_entitled` 테스트.
- **Modify `server/tests/test_endpoints.py`** — `/content/me` presets/effects 포함 테스트 + `/content/file/{id}` 401/403/404/200 테스트.
- **Modify `server/README.md`** — 프리셋/fx 배포 절차 문서.

---

## Task 1: content.py 일반화 + 파일 조회 순수 로직

**Files:**
- Modify: `server/app/content.py`
- Modify: `server/tests/test_content.py`

**Interfaces:**
- Consumes: 없음(표준 라이브러리).
- Produces:
  - `load_manifest(path: str, mtime_cache: dict) -> list[dict]` — (구 `load_themes`, 동작 동일: JSON 배열 로드 + mtime 캐시, 없음/깨짐 → `[]`).
  - `entitled_items(items: list[dict], role_ids: list[str]) -> list[dict]` — (구 `entitled_themes`: `role` falsy 또는 role_ids 포함분만, `role` 키 제거).
  - `find_item(items: list[dict], item_id: str) -> dict | None` — `id` 가 `item_id` 인 첫 아이템 or None.
  - `is_entitled(item: dict, role_ids: list[str]) -> bool` — `item["role"]` 가 falsy 이거나 role_ids 에 포함.

- [ ] **Step 1: 테스트 수정/추가 (`server/tests/test_content.py`)**

기존 `load_themes`/`entitled_themes` 를 참조하는 테스트를 새 이름으로 바꾸고, 아래 신규 테스트를 추가한다. 파일 전체를 아래로 교체:

```python
import app.content as content


def test_load_manifest_missing_returns_empty(tmp_path):
    cache = {}
    assert content.load_manifest(str(tmp_path / "nope.json"), cache) == []


def test_load_manifest_reads_and_caches(tmp_path):
    p = tmp_path / "m.json"
    p.write_text('[{"id":"a"}]', encoding="utf-8")
    cache = {}
    assert content.load_manifest(str(p), cache) == [{"id": "a"}]
    # 캐시 히트(파일 안 바뀜)
    assert content.load_manifest(str(p), cache) == [{"id": "a"}]
    assert cache["data"] == [{"id": "a"}]


def test_load_manifest_non_list_becomes_empty(tmp_path):
    p = tmp_path / "m.json"
    p.write_text('{"not":"list"}', encoding="utf-8")
    assert content.load_manifest(str(p), {}) == []


def test_load_manifest_corrupt_returns_empty(tmp_path):
    p = tmp_path / "m.json"
    p.write_text("{bad json", encoding="utf-8")
    assert content.load_manifest(str(p), {}) == []


def test_entitled_items_filters_and_strips_role():
    items = [
        {"id": "free", "role": None, "filename": "f.ini"},
        {"id": "gold", "role": "r-gold", "filename": "g.ini"},
        {"id": "plat", "role": "r-plat", "filename": "p.ini"},
    ]
    out = content.entitled_items(items, ["r-gold"])
    assert [i["id"] for i in out] == ["free", "gold"]
    assert all("role" not in i for i in out)


def test_find_item():
    items = [{"id": "a"}, {"id": "b"}]
    assert content.find_item(items, "b") == {"id": "b"}
    assert content.find_item(items, "zzz") is None


def test_is_entitled():
    assert content.is_entitled({"id": "x", "role": None}, []) is True   # 무료
    assert content.is_entitled({"id": "x"}, []) is True                 # role 키 없음=무료
    assert content.is_entitled({"id": "x", "role": "r1"}, ["r1"]) is True
    assert content.is_entitled({"id": "x", "role": "r1"}, ["r2"]) is False
    assert content.is_entitled({"id": "x", "role": "r1"}, None) is False
```

- [ ] **Step 2: 테스트 실패 확인**

Run: `cd server && python -m pytest tests/test_content.py -q`
Expected: FAIL — `AttributeError: module 'app.content' has no attribute 'load_manifest'` 등.

- [ ] **Step 3: content.py 구현 교체**

`server/app/content.py` 전체를 아래로 교체(로더·필터는 이름만 일반화, 조회 2함수 추가):

```python
import json
import os


def load_manifest(path: str, mtime_cache: dict) -> list[dict]:
    """콘텐츠 매니페스트(JSON 배열)를 읽는다. mtime_cache 로 파일이 안 바뀌면 캐시 반환.
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


def is_entitled(item: dict, role_ids: list[str]) -> bool:
    """item 의 role 이 falsy(무료)이거나 사용자 역할 ID 목록에 포함되면 True."""
    role = item.get("role")
    return (not role) or role in set(role_ids or [])


def entitled_items(items: list[dict], role_ids: list[str]) -> list[dict]:
    """권한 있는 아이템만, role 키를 제거해 반환."""
    return [{k: v for k, v in it.items() if k != "role"}
            for it in items if is_entitled(it, role_ids)]


def find_item(items: list[dict], item_id: str) -> dict | None:
    """id 가 item_id 인 첫 아이템, 없으면 None."""
    for it in items:
        if it.get("id") == item_id:
            return it
    return None
```

- [ ] **Step 4: 테스트 통과 확인**

Run: `cd server && python -m pytest tests/test_content.py -q`
Expected: PASS (8 passed).

- [ ] **Step 5: Commit**

```bash
git add server/app/content.py server/tests/test_content.py
git commit -m "content: 로더/필터 일반화(load_manifest/entitled_items) + find_item/is_entitled

Phase 1a 테마 로더를 프리셋·fx 재사용 위해 이름만 일반화(동작 동일).
파일 서빙용 순수 조회 함수(find_item/is_entitled) 추가."
```

---

## Task 2: `/content/me` 확장 (presets/effects 포함)

**Files:**
- Modify: `server/app/main.py`
- Modify: `server/tests/test_endpoints.py`
- Create: `server/content/presets.json`, `server/content/effects.json`

**Interfaces:**
- Consumes: `load_manifest`/`entitled_items`(Task 1), 기존 `verify_token`/`get_member_role_ids`.
- Produces: `GET /content/me` 응답 `{ "themes":[...], "presets":[...], "effects":[...] }`.

- [ ] **Step 1: 데모 매니페스트 생성**

`server/content/presets.json`:
```json
[]
```
`server/content/effects.json`:
```json
[]
```
(빈 배열 — 실제 아이템은 운영 중 추가. 스키마는 README·§14.2 참조.)

- [ ] **Step 2: 테스트 추가 (`server/tests/test_endpoints.py`)**

기존 `test_content_me_returns_entitled` 아래에 추가(같은 `_themes_file` 헬퍼 패턴 사용, presets/effects 도 임시파일로):

```python
def _write_json(tmp_path, name, data):
    import json
    p = tmp_path / name
    p.write_text(json.dumps(data), encoding="utf-8")
    return str(p)


@respx.mock
def test_content_me_includes_presets_and_effects(settings, tmp_path, monkeypatch):
    _reset(); _override(settings)
    try:
        monkeypatch.setattr(main_module, "THEMES_PATH", _write_json(tmp_path, "themes.json", []))
        monkeypatch.setattr(main_module, "PRESETS_PATH", _write_json(tmp_path, "presets.json", [
            {"id": "free-p", "role": None, "filename": "free.ini", "display_name": "F"},
            {"id": "gold-p", "role": "role-gold", "filename": "gold.ini", "display_name": "G"},
            {"id": "plat-p", "role": "role-plat", "filename": "plat.ini", "display_name": "P"},
        ]))
        monkeypatch.setattr(main_module, "EFFECTS_PATH", _write_json(tmp_path, "effects.json", [
            {"id": "gold-fx", "role": "role-gold", "filename": "gold.fx", "display_name": "GFX"},
        ]))
        main_module.THEMES_CACHE.clear(); main_module.PRESETS_CACHE.clear(); main_module.EFFECTS_CACHE.clear()
        tok = issue_token(settings, "user-9", "HW9", ["sherbet-buyer"])
        respx.get(f"{API}/guilds/guild-1/members/user-9").mock(
            return_value=httpx.Response(200, json={"roles": ["role-buyer", "role-gold"]}))
        client = TestClient(app)
        r = client.get("/content/me", headers={"Authorization": f"Bearer {tok}"})
        assert r.status_code == 200
        body = r.json()
        assert [p["id"] for p in body["presets"]] == ["free-p", "gold-p"]
        assert [e["id"] for e in body["effects"]] == ["gold-fx"]
        assert all("role" not in p for p in body["presets"])
        assert all("role" not in e for e in body["effects"])
    finally:
        _reset()
```

- [ ] **Step 3: 테스트 실패 확인**

Run: `cd server && python -m pytest tests/test_endpoints.py::test_content_me_includes_presets_and_effects -q`
Expected: FAIL — `AttributeError: ... has no attribute 'PRESETS_PATH'` 또는 KeyError `presets`.

- [ ] **Step 4: main.py 구현**

`server/app/main.py` 상단 import 를 갱신(구 `entitled_themes, load_themes` → 신규):
```python
from app.content import entitled_items, find_item, is_entitled, load_manifest
```

전역 경로/캐시(기존 `THEMES_PATH`/`THEMES_CACHE` 아래)에 추가:
```python
_CONTENT_DIR = os.path.join(os.path.dirname(os.path.dirname(__file__)), "content")
THEMES_PATH = os.path.join(_CONTENT_DIR, "themes.json")
PRESETS_PATH = os.path.join(_CONTENT_DIR, "presets.json")
EFFECTS_PATH = os.path.join(_CONTENT_DIR, "effects.json")
FILES_DIR = os.path.join(_CONTENT_DIR, "files")
THEMES_CACHE: dict = {}
PRESETS_CACHE: dict = {}
EFFECTS_CACHE: dict = {}
```
(기존 `THEMES_PATH`/`THEMES_CACHE` 정의 줄은 위 블록으로 대체 — 중복 정의 금지.)

`/content/me` 라우트의 마지막 두 줄(`themes = load_themes(...)` / `return {...}`)을 교체:
```python
    themes = entitled_items(load_manifest(THEMES_PATH, THEMES_CACHE), role_ids or [])
    presets = entitled_items(load_manifest(PRESETS_PATH, PRESETS_CACHE), role_ids or [])
    effects = entitled_items(load_manifest(EFFECTS_PATH, EFFECTS_CACHE), role_ids or [])
    return {"themes": themes, "presets": presets, "effects": effects}
```

- [ ] **Step 5: 전체 테스트 통과 확인**

Run: `cd server && python -m pytest -q`
Expected: PASS (기존 + 신규 전부). 기존 `test_content_me_returns_entitled` 도 여전히 통과(themes 키 유지).

- [ ] **Step 6: Commit**

```bash
git add server/app/main.py server/tests/test_endpoints.py server/content/presets.json server/content/effects.json
git commit -m "content: /content/me 를 presets/effects 로 확장

themes/presets/effects 세 매니페스트를 각각 역할 필터해 반환.
로더/캐시 글로벌 추가(단일 워커 전제)."
```

---

## Task 3: `GET /content/file/<id>` — per-id 역할 재검증 + 바이트 서빙

**Files:**
- Modify: `server/app/main.py`
- Modify: `server/tests/test_endpoints.py`
- Create: `server/content/files/demo-free-preset` (데모 바이트)

**Interfaces:**
- Consumes: `load_manifest`/`find_item`/`is_entitled`(Task 1), `verify_token`/`get_member_role_ids`, `PRESETS_PATH`/`EFFECTS_PATH`/`FILES_DIR`(Task 2).
- Produces: `GET /content/file/{item_id}` → 200(octet-stream 바이트) / 401 / 403 / 404 / 503.

- [ ] **Step 1: 데모 파일 생성**

`server/content/files/demo-free-preset` (내용은 유효 프리셋 흉내):
```
Techniques=Sherbet_Demo
```
(파일명 = id `demo-free-preset`. presets.json 에 매핑 항목은 Step 4 테스트에서 임시파일로 주입하므로 실제 배포는 운영 시.)

- [ ] **Step 2: 테스트 추가 (`server/tests/test_endpoints.py`)**

```python
def _files_dir(tmp_path, mapping):
    d = tmp_path / "files"
    d.mkdir()
    for fid, content in mapping.items():
        (d / fid).write_bytes(content)
    return str(d)


@respx.mock
def test_content_file_serves_entitled_bytes(settings, tmp_path, monkeypatch):
    _reset(); _override(settings)
    try:
        monkeypatch.setattr(main_module, "PRESETS_PATH", _write_json(tmp_path, "presets.json", [
            {"id": "gold-p", "role": "role-gold", "filename": "gold.ini"},
        ]))
        monkeypatch.setattr(main_module, "EFFECTS_PATH", _write_json(tmp_path, "effects.json", []))
        monkeypatch.setattr(main_module, "FILES_DIR", _files_dir(tmp_path, {"gold-p": b"Techniques=X\n"}))
        main_module.PRESETS_CACHE.clear(); main_module.EFFECTS_CACHE.clear()
        tok = issue_token(settings, "u1", "HW", ["sherbet-buyer"])
        respx.get(f"{API}/guilds/guild-1/members/u1").mock(
            return_value=httpx.Response(200, json={"roles": ["role-buyer", "role-gold"]}))
        client = TestClient(app)
        r = client.get("/content/file/gold-p", headers={"Authorization": f"Bearer {tok}"})
        assert r.status_code == 200
        assert r.content == b"Techniques=X\n"
    finally:
        _reset()


@respx.mock
def test_content_file_403_when_role_missing(settings, tmp_path, monkeypatch):
    # 매니페스트엔 있지만 사용자가 그 role 미보유 → 403 (id 추측 방어)
    _reset(); _override(settings)
    try:
        monkeypatch.setattr(main_module, "PRESETS_PATH", _write_json(tmp_path, "presets.json", [
            {"id": "gold-p", "role": "role-gold", "filename": "gold.ini"},
        ]))
        monkeypatch.setattr(main_module, "EFFECTS_PATH", _write_json(tmp_path, "effects.json", []))
        monkeypatch.setattr(main_module, "FILES_DIR", _files_dir(tmp_path, {"gold-p": b"secret"}))
        main_module.PRESETS_CACHE.clear(); main_module.EFFECTS_CACHE.clear()
        tok = issue_token(settings, "u2", "HW", ["sherbet-buyer"])
        respx.get(f"{API}/guilds/guild-1/members/u2").mock(
            return_value=httpx.Response(200, json={"roles": ["role-buyer"]}))  # role-gold 없음
        client = TestClient(app)
        r = client.get("/content/file/gold-p", headers={"Authorization": f"Bearer {tok}"})
        assert r.status_code == 403
    finally:
        _reset()


@respx.mock
def test_content_file_404_unknown_id(settings, tmp_path, monkeypatch):
    _reset(); _override(settings)
    try:
        monkeypatch.setattr(main_module, "PRESETS_PATH", _write_json(tmp_path, "presets.json", []))
        monkeypatch.setattr(main_module, "EFFECTS_PATH", _write_json(tmp_path, "effects.json", []))
        monkeypatch.setattr(main_module, "FILES_DIR", _files_dir(tmp_path, {}))
        main_module.PRESETS_CACHE.clear(); main_module.EFFECTS_CACHE.clear()
        tok = issue_token(settings, "u3", "HW", ["sherbet-buyer"])
        respx.get(f"{API}/guilds/guild-1/members/u3").mock(
            return_value=httpx.Response(200, json={"roles": ["role-buyer"]}))
        client = TestClient(app)
        r = client.get("/content/file/ghost", headers={"Authorization": f"Bearer {tok}"})
        assert r.status_code == 404
    finally:
        _reset()


def test_content_file_no_bearer_401(settings):
    _reset(); _override(settings)
    try:
        client = TestClient(app)
        assert client.get("/content/file/whatever").status_code == 401
    finally:
        _reset()


@respx.mock
def test_content_file_free_item_no_role(settings, tmp_path, monkeypatch):
    # role:null 무료 아이템 → 인증만으로 200
    _reset(); _override(settings)
    try:
        monkeypatch.setattr(main_module, "PRESETS_PATH", _write_json(tmp_path, "presets.json", [
            {"id": "free-p", "role": None, "filename": "free.ini"},
        ]))
        monkeypatch.setattr(main_module, "EFFECTS_PATH", _write_json(tmp_path, "effects.json", []))
        monkeypatch.setattr(main_module, "FILES_DIR", _files_dir(tmp_path, {"free-p": b"free-bytes"}))
        main_module.PRESETS_CACHE.clear(); main_module.EFFECTS_CACHE.clear()
        tok = issue_token(settings, "u4", "HW", ["sherbet-buyer"])
        respx.get(f"{API}/guilds/guild-1/members/u4").mock(
            return_value=httpx.Response(200, json={"roles": ["role-buyer"]}))
        client = TestClient(app)
        r = client.get("/content/file/free-p", headers={"Authorization": f"Bearer {tok}"})
        assert r.status_code == 200 and r.content == b"free-bytes"
    finally:
        _reset()
```

- [ ] **Step 3: 테스트 실패 확인**

Run: `cd server && python -m pytest tests/test_endpoints.py -k content_file -q`
Expected: FAIL — 라우트 없음(404 for all, or route-not-found).

- [ ] **Step 4: 라우트 구현 (`server/app/main.py`)**

import 에 `FileResponse` 추가:
```python
from fastapi.responses import FileResponse, HTMLResponse, JSONResponse
```

`/content/me` 라우트 아래에 신규 라우트 추가:
```python
@app.get("/content/file/{item_id}")
async def content_file(
    item_id: str,
    authorization: str | None = Header(default=None),
    settings: Settings = Depends(get_settings),
):
    if not authorization or not authorization.startswith("Bearer "):
        raise HTTPException(status_code=401, detail="missing_bearer")
    token = authorization[len("Bearer "):]
    payload = verify_token(settings, token, hwid=None)
    if payload is None:
        raise HTTPException(status_code=401, detail="invalid_token")

    # 매니페스트(프리셋+이펙트)에서 id 조회 — 화이트리스트
    items = load_manifest(PRESETS_PATH, PRESETS_CACHE) + load_manifest(EFFECTS_PATH, EFFECTS_CACHE)
    item = find_item(items, item_id)
    if item is None:
        raise HTTPException(status_code=404, detail="unknown_item")

    # per-id 역할 재검증 — 매니페스트 필터만 믿지 않는다
    try:
        role_ids = await get_member_role_ids(settings, payload["sub"])
    except (httpx.HTTPError, KeyError, ValueError):
        return JSONResponse(status_code=503, content={"error": "upstream_unavailable"})
    if not is_entitled(item, role_ids or []):
        raise HTTPException(status_code=403, detail="not_entitled")

    # 경로탈출 방어: files/<id> 고정 구성 + 실경로가 FILES_DIR 안인지 확인
    base = os.path.realpath(FILES_DIR)
    target = os.path.realpath(os.path.join(base, item_id))
    if os.path.commonpath([base, target]) != base or not os.path.isfile(target):
        raise HTTPException(status_code=404, detail="file_missing")
    return FileResponse(target, media_type="application/octet-stream")
```

- [ ] **Step 5: 전체 테스트 통과 확인**

Run: `cd server && python -m pytest -q`
Expected: PASS (전부). 특히 `content_file` 5개 통과.

- [ ] **Step 6: Commit**

```bash
git add server/app/main.py server/tests/test_endpoints.py server/content/files/demo-free-preset
git commit -m "content: GET /content/file/<id> — per-id 역할 재검증 후 바이트 서빙

Bearer 검증→디코 역할 라이브재조회→그 id 아이템 role 재확인(403)→
files/<id> octet-stream. 화이트리스트 id + 실경로 FILES_DIR 내부 확인으로
경로탈출 차단. 401/403/404/503."
```

---

## Task 4: README 문서 + 배포 확인

**Files:**
- Modify: `server/README.md`

**Interfaces:** 없음(문서).

- [ ] **Step 1: README 에 프리셋/fx 절 추가**

`server/README.md` 의 `## 원격 테마 (/content/me)` 절 아래에 추가:

```markdown
## 원격 프리셋·이펙트 (`/content/me` 확장 + `/content/file/<id>`)

- `GET /content/me` 는 `{"themes":[...], "presets":[...], "effects":[...]}` 를 반환한다. presets/effects 는 각각 `content/presets.json`·`content/effects.json` 을 역할 필터한 결과(`role` 필드 제거).
- `GET /content/file/<id>` — 헤더 `Authorization: Bearer <세션토큰>`. **그 id 아이템의 `role` 을 사용자가 보유해야만** 파일 바이트(`application/octet-stream`)를 준다(매니페스트 필터만으로는 부족 — id 추측 방어). 미보유 → 403, 미등록 id → 404, 디코 조회 실패 → 503.
- **매니페스트 스키마**(presets.json / effects.json, 배열):
  ```json
  [{ "id": "strawberry-grade", "filename": "Strawberry.ini", "role": "1408966226637750405", "display_name": "딸기 보정" }]
  ```
  - `id` = 파일명과 분리한 불투명 키(파일 추측 방지). `content/files/<id>` 로 매핑된다.
  - `filename` = 클라가 디스크에 쓸 이름(프리셋=`Sherbet-Presets/`, 이펙트=`Sherbet-Fx/`).
  - `role` = 디스코드 **숫자 역할 ID 문자열**(따옴표), `null`=무료. 숫자 없이 이름 쓰면 항상 거부(fail-closed).
- **새 프리셋/fx 배포(재빌드 불필요):**
  1. 파일을 `content/files/<id>` 로 둔다(파일명 = id, 확장자 없이).
  2. `presets.json`(또는 `effects.json`)에 위 스키마로 항목 추가.
  3. 구매자에게 디스코드에서 해당 역할 부여.
  4. 끝. mtime 캐시라 다음 요청에 자동 반영.
- **주의(fx 다중 파일):** fx 가 LUT `.png`/`.fxh` 를 딸리면 각각을 별도 effects 아이템으로 추가한다(클라가 모두 `Sherbet-Fx/` 에 떨궈 ReShade 가 검색경로에서 발견).
```

- [ ] **Step 2: 전체 테스트 재확인(회귀)**

Run: `cd server && python -m pytest -q`
Expected: PASS 전부.

- [ ] **Step 3: Commit**

```bash
git add server/README.md
git commit -m "docs: server README에 /content/file + presets/effects 매니페스트 편집법 추가"
```

---

## Self-Review (작성자 체크)

- **스펙 커버리지:** §14.2(저장소/매니페스트)=Task 2·4 데모+문서, §14.3(`/content/me` 확장·`/content/file` per-id 재검증·경로안전)=Task 2·3, §14.7(401/403/404/503·경로탈출)=Task 3. §14.4(역할 숫자ID)=README 명시. ✅
- **타입 일관성:** `load_manifest`/`entitled_items`/`find_item`/`is_entitled`(Task 1) → main.py 소비(Task 2·3) 시그니처 일치. `PRESETS_PATH`/`EFFECTS_PATH`/`FILES_DIR`/`*_CACHE`(Task 2) → Task 3 소비 일치. ✅
- **플레이스홀더:** 모든 스텝 실제 코드/명령. 없음. ✅
- **글로벌 제약:** 커밋 무해·한국어, 단일워커 캐시, per-id 재검증, 경로탈출 방어 전부 태스크에 반영. ✅
- **주의(구현 시):** Task 1 의 content.py 교체로 `load_themes`/`entitled_themes` 이름이 사라지므로, main.py(Task 2 에서 갱신 전까지)·다른 참조가 깨진다 — Task 1→2 순서를 지키면 Task 2 에서 import 갱신으로 해소. 인라인 실행이면 Task 1 직후 전체 pytest 는 test_endpoints 의 구 import 로 실패할 수 있음 → Task 1 커밋 시엔 `pytest tests/test_content.py` 만 돌리고, 전체 그린은 Task 2 완료 후 확인.
