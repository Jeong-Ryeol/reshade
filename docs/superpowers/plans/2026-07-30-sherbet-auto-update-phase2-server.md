# Sherbet 자동 업데이트 — 2단계: 서버 매니페스트 + 릴리스 파이프라인

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 새 버전을 태그 하나로 발행하고, 홈서버가 그 사실을 클라에게 알려줄 수 있게 만든다.

**Architecture:** CI(`release.yml`)가 태그 푸시에 반응해 GitHub Releases에 DLL을 올린다. 맥의 관리자 도구가 **실제 배포된 바이트를 다시 받아 직접 해시를 계산**하고 홈서버 `content/update.json`에 배치한다. 홈서버의 새 라우터 `GET /update/manifest`가 그 파일을 아키텍처별 **평면 문자열 JSON**으로 평탄화해 내려준다. CI는 홈서버 자격증명을 갖지 않는다.

**Tech Stack:** FastAPI(기존 `sherbet-auth`), pytest, GitHub Actions, Python 관리자 CLI

**설계 스펙:** `docs/superpowers/specs/2026-07-29-sherbet-auto-update-design.md` §3(매니페스트), §7(릴리스 파이프라인)
**선행 단계:** 1단계 완료 — `source/sherbet_update_core.hpp`의 `parse_manifest`가 이 응답의 유일한 소비자다.

## Global Constraints

- **⚠️ 타입 규약 — 어기면 조용히 실패한다.** 클라의 `json_string`(`source/sherbet_auth_core.hpp:22`)은 **따옴표로 감싼 문자열만** 읽는다. 숫자를 따옴표 없이 내면 `false`를 반환하고 클라는 에러 없이 "업데이트 없음"으로 넘어간다 — 로그에도 안 남는다.
  - **문자열로 직렬화:** `schema` / `version` / `min_version` / `sha256` / `url` / `size` / `notes` / `notice` / `arch`
  - **진짜 JSON 불리언:** `allow_downgrade` 하나뿐 (`json_bool_or_null`이 읽는다)
- **키 유일성.** 클라의 `has_unique_keys`는 바디 전체에서 각 키 니들(`"schema"` `"arch"` `"version"` `"min_version"` `"sha256"` `"url"` `"size"` `"allow_downgrade"`)의 **원문 등장 횟수**를 세고, 2회 이상이면 매니페스트 전체를 거부한다. 반드시 표준 JSON 직렬화(`JSONResponse`/`json.dumps`)를 쓸 것 — 손으로 문자열을 조립하면 `notes` 안의 따옴표가 이 방어를 무너뜨린다.
- **URL 접두사 피닝.** 클라는 `https://github.com/Jeong-Ryeol/reshade/releases/download/` 로 **정확히** 시작하지 않는 URL을 거부한다. `..`, `@`, `%2e`, `%2E`, `%2f`, `%2F`, 제어문자(0x00~0x1F, 0x7F)가 하나라도 있으면 거부한다.
- **크기 범위.** `size`는 1MiB(1048576) 이상 32MiB(33554432) 이하만 통과.
- **`sha256`은 소문자 64자 hex.**
- **`/auth/verify`와 `/content/me`는 한 줄도 건드리지 않는다.** 업데이트 실패는 회복 가능하지만 로그인 실패는 제품이 죽는다.
- **CI에 홈서버 SSH 키를 넣지 않는다.** GitHub Secrets에 홈 LAN 키를 주는 순간 "GitHub 침해 = 홈서버 침해"가 되어 두 시스템을 나눈 의미가 사라진다.
- 커밋 메시지에 AI/클로드 저작권 문구 금지.
- 홈서버 접속은 관리자 도구의 기존 `SSH_HOST` 상수를 그대로 쓴다(`update.json`은 1KB 미만이라 DDNS로도 안전하다).

---

## File Structure

| 파일 | 책임 |
|---|---|
| `server/app/content.py` (수정) | `load_json_object` 추가 — dict 전용, `load_manifest`와 같은 mtime 캐시·fail-soft 규약 |
| `server/app/update.py` (신규) | `GET /update/manifest` 라우터. arch 검증 → 평탄화 → 문자열 직렬화 |
| `server/app/main.py` (수정) | 라우터 등록 **한 줄만**. 기존 엔드포인트 무변경 |
| `server/tests/test_update.py` (신규) | 타입 규약·거부 경로·mtime 캐시 검증 |
| `server/content/update.example.json` (신규) | 형식 예시. 실운용 파일은 커밋하지 않는다(킬스위치 조작이 리포 사본과 어긋나면 안 됨) |
| `.github/workflows/release.yml` (신규) | 태그 전용 릴리스 |
| `tools/sherbet_admin.py` (수정) | 메뉴 "6. 새 버전 배포" |
| `docs/sherbet-업데이트-런북.md` (신규) | 발행·킬스위치·롤백 절차 |

---

## Task 1: 서버 — dict 매니페스트 로더

**Files:**
- Modify: `server/app/content.py`
- Create: `server/tests/test_update.py`

**Interfaces:**
- Consumes: 없음
- Produces: `load_json_object(path: str, mtime_cache: dict) -> dict` — 파일 없음/깨짐/비-dict면 `{}`

- [ ] **Step 1: 실패하는 테스트 작성**

`server/tests/test_update.py` 생성:

```python
import json
import os

from app.content import load_json_object


def test_load_json_object_reads_dict(tmp_path):
    p = tmp_path / "update.json"
    p.write_text(json.dumps({"schema": 1, "version": "1.4.0"}), encoding="utf-8")
    cache = {}
    assert load_json_object(str(p), cache) == {"schema": 1, "version": "1.4.0"}


def test_load_json_object_missing_file(tmp_path):
    assert load_json_object(str(tmp_path / "nope.json"), {}) == {}


def test_load_json_object_broken_json(tmp_path):
    p = tmp_path / "update.json"
    p.write_text("{not json", encoding="utf-8")
    assert load_json_object(str(p), {}) == {}


def test_load_json_object_rejects_non_dict(tmp_path):
    # 배열을 주면 빈 dict — load_manifest 의 거울상
    p = tmp_path / "update.json"
    p.write_text(json.dumps([1, 2, 3]), encoding="utf-8")
    assert load_json_object(str(p), {}) == {}


def test_load_json_object_mtime_cache(tmp_path):
    p = tmp_path / "update.json"
    p.write_text(json.dumps({"version": "1.0.0"}), encoding="utf-8")
    cache = {}
    first = load_json_object(str(p), cache)
    assert first["version"] == "1.0.0"
    # 파일을 바꾸되 mtime 을 되돌리면 캐시가 살아 있어야 한다
    st = os.stat(str(p))
    p.write_text(json.dumps({"version": "9.9.9"}), encoding="utf-8")
    os.utime(str(p), (st.st_atime, st.st_mtime))
    assert load_json_object(str(p), cache)["version"] == "1.0.0"
    # mtime 이 진짜로 바뀌면 다시 읽는다
    os.utime(str(p), (st.st_atime, st.st_mtime + 10))
    assert load_json_object(str(p), cache)["version"] == "9.9.9"


def test_load_json_object_clears_cache_when_file_disappears(tmp_path):
    p = tmp_path / "update.json"
    p.write_text(json.dumps({"version": "1.0.0"}), encoding="utf-8")
    cache = {}
    load_json_object(str(p), cache)
    os.remove(str(p))
    assert load_json_object(str(p), cache) == {}
    assert "data" not in cache
```

- [ ] **Step 2: 테스트가 실패하는지 확인**

Run: `cd ~/reshade/server && python3 -m pytest tests/test_update.py -q`
Expected: FAIL — `ImportError: cannot import name 'load_json_object'`

- [ ] **Step 3: 최소 구현**

`server/app/content.py`의 `load_manifest` 바로 뒤에 추가:

```python
def load_json_object(path: str, mtime_cache: dict) -> dict:
    """JSON 객체(dict) 파일을 읽는다. load_manifest 와 같은 mtime 캐시·fail-soft 규약.
    파일 없음/깨짐/비-dict 이면 빈 dict — 업데이트 안내가 없을 뿐 서비스는 살아 있어야 한다."""
    try:
        mtime = os.path.getmtime(path)
    except OSError:
        mtime_cache.pop("mtime", None)
        mtime_cache.pop("data", None)
        return {}
    if mtime_cache.get("mtime") == mtime and "data" in mtime_cache:
        return mtime_cache["data"]
    try:
        with open(path, encoding="utf-8") as f:
            data = json.load(f)
    except (OSError, ValueError):
        return {}
    if not isinstance(data, dict):
        data = {}
    mtime_cache["mtime"] = mtime
    mtime_cache["data"] = data
    return data
```

- [ ] **Step 4: 테스트 통과 + 기존 서버 테스트 회귀 확인**

Run: `cd ~/reshade/server && python3 -m pytest -q`
Expected: 기존 테스트 전부 통과 + 새 테스트 6개 통과. 실패 0.

- [ ] **Step 5: 커밋**

```bash
git add server/app/content.py server/tests/test_update.py
git commit -m "서버: dict 매니페스트 로더 추가

update.json 은 배열이 아니라 객체라 load_manifest 를 못 쓴다.
같은 mtime 캐시·fail-soft 규약으로 load_json_object 를 추가한다 —
파일이 없거나 깨져도 업데이트 안내만 사라지고 서비스는 살아 있어야 한다."
```

---

## Task 2: 서버 — `/update/manifest` 라우터

**Files:**
- Create: `server/app/update.py`
- Create: `server/content/update.example.json`
- Modify: `server/app/main.py`
- Modify: `server/tests/test_update.py`

**Interfaces:**
- Consumes: Task 1의 `load_json_object`
- Produces: `router` (APIRouter). 경로 `GET /update/manifest?arch=<x64|x86>&cur=<version>`

**응답 계약(클라 `parse_manifest`가 유일한 소비자):** 아래 키만, 전부 문자열, 단 `allow_downgrade`는 불리언.

- [ ] **Step 1: 실패하는 테스트 작성**

`server/tests/test_update.py`에 추가:

```python
import pytest
from fastapi.testclient import TestClient


@pytest.fixture
def client(tmp_path, monkeypatch):
    from app import update as upd
    from app.main import app
    p = tmp_path / "update.json"
    p.write_text(json.dumps({
        "schema": 1,
        "version": "1.4.0",
        "min_version": "1.0.0",
        "allow_downgrade": False,
        "notice": "",
        "notes": "OSD 가로 배치 추가\n로그인 만료 버그 수정",
        "builds": {
            "x64": {
                "url": "https://github.com/Jeong-Ryeol/reshade/releases/download/sherbet-1.4.0/ReShade64.dll",
                "size": 4312576,
                "sha256": "3f1c9a4b5d6e7f80912a3b4c5d6e7f80912a3b4c5d6e7f80912a3b4c5d6e7f80",
            },
            "x86": {
                "url": "https://github.com/Jeong-Ryeol/reshade/releases/download/sherbet-1.4.0/ReShade32.dll",
                "size": 3837952,
                "sha256": "9d40b1000000000000000000000000000000000000000000000000000000005a",
            },
        },
    }), encoding="utf-8")
    monkeypatch.setattr(upd, "UPDATE_PATH", str(p))
    upd.UPDATE_CACHE.clear()
    return TestClient(app)


def test_manifest_x64(client):
    r = client.get("/update/manifest", params={"arch": "x64", "cur": "1.3.0"})
    assert r.status_code == 200
    b = r.json()
    assert b["arch"] == "x64"
    assert b["version"] == "1.4.0"
    assert b["url"].endswith("ReShade64.dll")


def test_all_scalars_are_strings_except_allow_downgrade(client):
    """⚠️ 이 테스트가 이 라우터의 존재 이유다.
    클라의 json_string 은 따옴표 있는 문자열만 읽는다. 숫자를 그대로 내면
    클라는 에러 없이 '업데이트 없음' 으로 넘어가고 로그에도 안 남는다."""
    b = client.get("/update/manifest", params={"arch": "x64"}).json()
    for key in ("schema", "arch", "version", "min_version", "sha256", "url", "size", "notes", "notice"):
        assert isinstance(b[key], str), f"{key} 는 문자열이어야 한다 (실제 {type(b[key]).__name__})"
    assert isinstance(b["allow_downgrade"], bool)
    assert b["schema"] == "1"
    assert b["size"] == "4312576"


def test_key_needles_appear_exactly_once(client):
    """클라의 has_unique_keys 는 바디 원문에서 각 니들의 등장 횟수를 센다.
    2회 이상이면 매니페스트 전체를 거부한다."""
    raw = client.get("/update/manifest", params={"arch": "x64"}).text
    for needle in ('"schema"', '"arch"', '"version"', '"min_version"',
                   '"sha256"', '"url"', '"size"', '"allow_downgrade"'):
        assert raw.count(needle) == 1, f"{needle} 가 {raw.count(needle)} 회 등장한다"


def test_notes_with_quotes_do_not_shadow(client, tmp_path, monkeypatch):
    """정직한 릴리스 노트에 따옴표가 들어가도 키가 가려지면 안 된다."""
    from app import update as upd
    data = json.loads(open(upd.UPDATE_PATH, encoding="utf-8").read())
    data["notes"] = '"url" 버튼과 "size" 표시를 고쳤어요'
    open(upd.UPDATE_PATH, "w", encoding="utf-8").write(json.dumps(data))
    upd.UPDATE_CACHE.clear()
    raw = client.get("/update/manifest", params={"arch": "x64"}).text
    assert raw.count('"url"') == 1
    assert raw.count('"size"') == 1


def test_x86_and_x64_do_not_leak(client):
    a = client.get("/update/manifest", params={"arch": "x64"}).json()
    b = client.get("/update/manifest", params={"arch": "x86"}).json()
    assert a["url"].endswith("ReShade64.dll") and b["url"].endswith("ReShade32.dll")
    assert a["sha256"] != b["sha256"] and a["size"] != b["size"]


def test_bad_arch(client):
    assert client.get("/update/manifest", params={"arch": "arm64"}).status_code == 400
    assert client.get("/update/manifest").status_code == 422  # arch 필수


def test_missing_file_is_503(client, monkeypatch):
    from app import update as upd
    monkeypatch.setattr(upd, "UPDATE_PATH", "/nonexistent/update.json")
    upd.UPDATE_CACHE.clear()
    assert client.get("/update/manifest", params={"arch": "x64"}).status_code == 503


def test_missing_arch_slot_is_503(client):
    from app import update as upd
    data = json.loads(open(upd.UPDATE_PATH, encoding="utf-8").read())
    del data["builds"]["x86"]
    open(upd.UPDATE_PATH, "w", encoding="utf-8").write(json.dumps(data))
    upd.UPDATE_CACHE.clear()
    assert client.get("/update/manifest", params={"arch": "x86"}).status_code == 503
    assert client.get("/update/manifest", params={"arch": "x64"}).status_code == 200


def test_no_auth_required(client):
    """인증을 요구하면 '로그인을 망가뜨린 빌드' 를 영원히 고칠 수 없다(스펙 §3.1)."""
    r = client.get("/update/manifest", params={"arch": "x64"})
    assert r.status_code == 200  # Authorization 헤더 없이도
```

- [ ] **Step 2: 테스트가 실패하는지 확인**

Run: `cd ~/reshade/server && python3 -m pytest tests/test_update.py -q`
Expected: FAIL — `ModuleNotFoundError: No module named 'app.update'`

- [ ] **Step 3: 최소 구현**

`server/app/update.py` 생성:

```python
import os

from fastapi import APIRouter, Query
from fastapi.responses import JSONResponse

from app.content import load_json_object

router = APIRouter()

UPDATE_PATH = os.path.join(
    os.path.dirname(os.path.dirname(__file__)), "content", "update.json")
UPDATE_CACHE: dict = {}

_ARCHS = ("x64", "x86")


@router.get("/update/manifest")
def update_manifest(arch: str = Query(...), cur: str | None = None):
    """클라가 시작할 때 무인증으로 묻는다.

    인증을 붙이지 않는 이유(스펙 §3.1): 캐시 토큰이 없으면 시작 verify 자체가 안 돌고,
    무엇보다 '로그인을 망가뜨린 빌드' 는 인증을 요구하는 순간 영원히 고칠 수 없다.
    리포도 릴리스도 공개라 숨길 것도 없다.

    ⚠️ 모든 스칼라를 문자열로 직렬화한다. 클라의 json_string 은 따옴표 있는 문자열만
    읽으므로, 숫자를 그대로 내면 클라가 조용히 '업데이트 없음' 으로 넘어간다.
    allow_downgrade 만 예외 — json_bool_or_null 이 읽는 진짜 불리언이다.
    """
    if arch not in _ARCHS:
        return JSONResponse(status_code=400, content={"error": "bad_arch"})

    doc = load_json_object(UPDATE_PATH, UPDATE_CACHE)
    if not doc:
        return JSONResponse(status_code=503, content={"error": "no_manifest"})

    builds = doc.get("builds")
    if not isinstance(builds, dict):
        return JSONResponse(status_code=503, content={"error": "no_builds"})
    slot = builds.get(arch)
    if not isinstance(slot, dict):
        return JSONResponse(status_code=503, content={"error": "no_build_for_arch"})

    # cur 은 집계용으로만 받는다 — 저장하지 않는다(새 PII 저장소를 만들지 않는다).
    return {
        "schema": str(doc.get("schema", 1)),
        "arch": arch,
        "version": str(doc.get("version", "")),
        "min_version": str(doc.get("min_version", "")),
        "allow_downgrade": bool(doc.get("allow_downgrade", False)),
        "size": str(slot.get("size", "")),
        "sha256": str(slot.get("sha256", "")),
        "url": str(slot.get("url", "")),
        "notice": str(doc.get("notice", "")),
        "notes": str(doc.get("notes", "")),
    }
```

`server/app/main.py`에 라우터 등록 — import 목록 아래, `app = FastAPI(...)` 바로 뒤에 **한 줄**:

```python
from app.update import router as update_router
app.include_router(update_router)
```

`server/content/update.example.json` 생성 (실운용 파일은 커밋하지 않는다):

```json
{
  "schema": 1,
  "version": "1.0.0",
  "min_version": "",
  "allow_downgrade": false,
  "notice": "",
  "notes": "첫 릴리스",
  "builds": {
    "x64": {
      "url": "https://github.com/Jeong-Ryeol/reshade/releases/download/sherbet-1.0.0/ReShade64.dll",
      "size": 4312576,
      "sha256": "여기에 소문자 64자 hex"
    },
    "x86": {
      "url": "https://github.com/Jeong-Ryeol/reshade/releases/download/sherbet-1.0.0/ReShade32.dll",
      "size": 3837952,
      "sha256": "여기에 소문자 64자 hex"
    }
  }
}
```

- [ ] **Step 4: 테스트 통과 + 전체 서버 테스트 회귀**

Run: `cd ~/reshade/server && python3 -m pytest -q`
Expected: 전부 통과. 기존 엔드포인트 테스트가 하나도 깨지지 않아야 한다.

- [ ] **Step 5: 클라 파서와 실제로 맞물리는지 교차 검증**

라우터 응답을 그대로 1단계의 `parse_manifest`에 먹여 본다. 이것이 두 층이 실제로 통하는지 확인하는 유일한 지점이다.

Run:
```bash
cd ~/reshade/server && python3 -c "
import json, sys
sys.path.insert(0, '.')
from fastapi.testclient import TestClient
" 2>/dev/null || true
cd ~/reshade && cat > /tmp/xcheck.cpp <<'EOF'
#include "sherbet_update_core.hpp"
#include <cstdio>
#include <fstream>
#include <sstream>
using namespace sherbet::update;
int main(int argc, char **argv) {
    std::ifstream in(argv[1]); std::stringstream ss; ss << in.rdbuf();
    const info u = parse_manifest(ss.str(), "x64");
    std::printf("ok=%d version=%s size=%llu\n", (int)u.ok, u.version.c_str(),
                (unsigned long long)u.size);
    return u.ok ? 0 : 1;
}
EOF
clang++ -std=c++17 -Isource /tmp/xcheck.cpp -o /tmp/xcheck
```
그다음 서버 테스트에서 응답 본문을 `/tmp/resp.json`으로 덤프하고 `/tmp/xcheck /tmp/resp.json` 실행.
Expected: `ok=1 version=1.4.0 size=4312576`

**이 스텝이 실패하면 멈추고 원인을 찾을 것** — 서버와 클라가 서로 다른 계약을 믿고 있다는 뜻이다.

- [ ] **Step 6: 커밋**

```bash
git add server/app/update.py server/app/main.py server/content/update.example.json server/tests/test_update.py
git commit -m "서버: /update/manifest 라우터 추가

무인증 별도 엔드포인트 — 인증을 요구하면 '로그인을 망가뜨린 빌드' 를 영원히
고칠 수 없다. 모든 스칼라를 문자열로 직렬화한다(클라 json_string 은 따옴표
문자열만 읽어서, 숫자로 내면 조용히 '업데이트 없음' 이 된다).
/auth/verify 와 /content/me 는 건드리지 않았다."
```

---

## Task 3: 릴리스 워크플로

**Files:**
- Create: `.github/workflows/release.yml`

**Interfaces:**
- Consumes: `source/sherbet_owner.h`의 `SHERBET_VERSION`
- Produces: GitHub Release `sherbet-<x.y.z>` with `ReShade64.dll`, `ReShade32.dll`

- [ ] **Step 1: 워크플로 작성**

`.github/workflows/release.yml`:

```yaml
name: release

# 태그를 밀었을 때만 돈다. 평소 커밋 푸시는 build.yml 이 검증만 하고 구매자에겐 알림이 안 간다.
# ⚠️ 태그는 반드시 v 없는 sherbet-1.4.0 형식. sherbet-v1.4.0 은 tools/update_version.ps1 의
#    앵커 없는 -match 에 걸려 리쉐이드 res/version.h 를 탈취한다.
on:
  push:
    tags:
      - 'sherbet-[0-9]*'
  workflow_dispatch:
    inputs:
      dry_run:
        description: '빌드·해시까지만 하고 릴리스는 만들지 않음'
        type: boolean
        default: true

permissions: {}

jobs:
  release:
    runs-on: windows-latest

    permissions:
      contents: write   # gh release create 에 필요. build.yml 의 contents: read 와 다르다.

    env:
      GH_TOKEN: ${{ github.token }}   # checkout 이 persist-credentials:false 라 명시 필요

    steps:
      - name: Checkout
        uses: actions/checkout@v6
        with:
          fetch-depth: 0
          persist-credentials: false
          submodules: true

      - name: Set up Python
        uses: actions/setup-python@v6
        with:
          python-version: '3.x'

      - name: Set up MSBuild
        uses: microsoft/setup-msbuild@v3

      - name: 태그와 SHERBET_VERSION 이 일치하는지 확인
        shell: pwsh
        run: |
          $header = Get-Content -Raw source/sherbet_owner.h
          if ($header -notmatch '#define SHERBET_VERSION "([0-9]+\.[0-9]+\.[0-9]+)"') {
            Write-Error "sherbet_owner.h 에서 SHERBET_VERSION 을 찾지 못했습니다"; exit 1
          }
          $ver = $matches[1]
          if ($ver -eq "0.0.0") {
            Write-Error "SHERBET_VERSION 이 0.0.0 입니다 — 버전을 올리고 커밋한 뒤 태그를 미세요"; exit 1
          }
          if ($env:GITHUB_REF -like 'refs/tags/*') {
            $tag = $env:GITHUB_REF -replace '^refs/tags/', ''
            $suffix = $tag -replace '^sherbet-', ''
            if ($suffix -ne $ver) {
              Write-Error "태그($tag)와 SHERBET_VERSION($ver)이 다릅니다. 서버는 새 버전이라 하고 DLL 은 구버전이라 하면 무한 알림 루프가 됩니다."; exit 1
            }
          }
          "SHERBET_VERSION = $ver" | Out-File -FilePath $env:GITHUB_ENV -Append -Encoding utf8
          "SHERBET_TAG = sherbet-$ver" | Out-File -FilePath $env:GITHUB_ENV -Append -Encoding utf8

      - name: 공용 릴리스 빌드 인자 강제
        shell: pwsh
        run: |
          # 공개 릴리스에 게이트 없는 DLL 이 올라가면 제품이 공짜가 되고,
          # 노드락이 켜진 공용 빌드는 전원을 브릭시킨다. 무조건 치환한다.
          $c = Get-Content -Raw source/sherbet_owner.h
          $c = $c -replace '#define SHERBET_OWNER ""', '#define SHERBET_OWNER ""'
          $c = $c -replace '#define SHERBET_ORDER_NO ""', '#define SHERBET_ORDER_NO ""'
          $c = $c -replace '#define SHERBET_DEFAULT_THEME "mint"', '#define SHERBET_DEFAULT_THEME "mint"'
          $c = $c -replace '#define SHERBET_NODELOCK 0', '#define SHERBET_NODELOCK 0'
          $c = $c -replace '#define SHERBET_ONLINE_AUTH 0', '#define SHERBET_ONLINE_AUTH 1'
          Set-Content -Path source/sherbet_owner.h -Value $c -Encoding UTF8 -NoNewline
          $now = Get-Content -Raw source/sherbet_owner.h
          if ($now -notmatch 'SHERBET_ONLINE_AUTH 1') { Write-Error "ONLINE_AUTH 치환 실패"; exit 1 }
          if ($now -match 'SHERBET_NODELOCK 1')       { Write-Error "NODELOCK 이 1 입니다"; exit 1 }
          if (Test-Path res/presets/personal.ini)     { Write-Error "개인 프리셋이 포함돼 있습니다"; exit 1 }

      - name: Build (32-bit)
        run: msbuild ReShade.sln /p:Configuration=Release /p:Platform=32-bit

      - name: Build (64-bit)
        run: msbuild ReShade.sln /p:Configuration=Release /p:Platform=64-bit

      - name: 산출물에 SherbetVersion 심볼이 박혔는지 확인
        shell: pwsh
        run: |
          # findstr 은 NUL 을 포함한 바이너리에서 신뢰할 수 없다. python 으로 바이트 검색.
          python - <<'PY'
          import os, sys
          ver = os.environ["SHERBET_VERSION"].encode()
          for p in (r"bin\x64\Release\ReShade64.dll", r"bin\Win32\Release\ReShade32.dll"):
              data = open(p, "rb").read()
              if b"SherbetVersion" not in data:
                  sys.exit(f"{p}: SherbetVersion 심볼이 없습니다")
              if ver not in data:
                  sys.exit(f"{p}: 버전 문자열 {ver.decode()} 이 없습니다")
              print(f"{p}: OK ({len(data)} bytes)")
          PY

      - name: SHA-256 기록 (참고용 — 권위 있는 값은 맥이 직접 계산한다)
        shell: pwsh
        run: |
          foreach ($p in @("bin\x64\Release\ReShade64.dll", "bin\Win32\Release\ReShade32.dll")) {
            $h = (Get-FileHash -Algorithm SHA256 $p).Hash.ToLower()
            $s = (Get-Item $p).Length
            "$([System.IO.Path]::GetFileName($p))  sha256=$h  size=$s" | Out-File -FilePath $env:GITHUB_STEP_SUMMARY -Append -Encoding utf8
          }

      - name: 릴리스 발행
        if: startsWith(github.ref, 'refs/tags/')
        shell: pwsh
        run: |
          # 멱등: 업로드 중 러너가 죽어도 재실행할 수 있다(태그 삭제/재푸시라는 지저분한 복구를 없앤다).
          $tag = $env:SHERBET_TAG
          $files = @("bin\x64\Release\ReShade64.dll", "bin\Win32\Release\ReShade32.dll")
          gh release create $tag $files --title "Sherbet $env:SHERBET_VERSION" --notes "자동 발행" 2>$null
          if ($LASTEXITCODE -ne 0) {
            gh release upload $tag $files --clobber
          }
```

- [ ] **Step 2: YAML 문법 확인**

Run: `cd ~/reshade && python3 -c "import yaml; d=yaml.safe_load(open('.github/workflows/release.yml')); print('jobs:', list(d['jobs'].keys())); print('perms:', d['jobs']['release']['permissions'])"`
Expected: `jobs: ['release']`, `perms: {'contents': 'write'}`

- [ ] **Step 3: 태그-버전 검증 로직을 로컬에서 재현**

Run:
```bash
cd ~/reshade && python3 -c "
import re
h=open('source/sherbet_owner.h').read()
m=re.search(r'#define SHERBET_VERSION \"([0-9]+\.[0-9]+\.[0-9]+)\"', h)
print('헤더 버전:', m.group(1) if m else '못 찾음')
for tag in ['sherbet-1.0.0','sherbet-1.4.0','sherbet-v1.0.0']:
    suf=re.sub(r'^sherbet-','',tag)
    print(f'  {tag:16} → suffix={suf:8} 일치={suf==m.group(1)}')
"
```
Expected: 헤더 버전 `1.0.0`, `sherbet-1.0.0`만 일치=True. `sherbet-v1.0.0`은 suffix가 `v1.0.0`이라 불일치 → 빌드 중단(의도된 동작).

- [ ] **Step 4: 커밋하고 dry_run 으로 태워보기**

태그는 한 번 밀면 되돌리기가 지저분하므로 **반드시 `workflow_dispatch` + `dry_run` 으로 먼저** YAML·권한·GH_TOKEN 오류를 태운다.

```bash
git add .github/workflows/release.yml
git commit -m "태그 전용 릴리스 워크플로 추가

태그와 SHERBET_VERSION 이 다르면 빌드를 중단한다 — 서버는 새 버전이라 하고
DLL 은 구버전이라 하면 구매자에게 무한 알림 루프가 그대로 출고된다.
ONLINE_AUTH=1/NODELOCK=0 을 무조건 치환하고, 산출물 바이트에서 SherbetVersion
심볼을 확인한다. gh release create || upload --clobber 로 멱등."
git push
gh workflow run release.yml -f dry_run=true
sleep 30 && gh run list --workflow=release.yml --limit 1
```

- [ ] **Step 5: dry_run 결과 확인**

Run: `gh run watch <id>` 또는 `gh run view <id> --log-failed`
Expected: 모든 스텝 성공, 마지막 "릴리스 발행" 스텝은 `if: startsWith(github.ref, 'refs/tags/')`로 스킵됨. Step Summary에 두 DLL의 sha256·size가 기록됨.

**실패하면 여기서 고친다** — 태그를 밀기 전에 잡는 것이 이 스텝의 목적이다.

---

## Task 4: 관리자 도구 — 새 버전 배포

**Files:**
- Modify: `tools/sherbet_admin.py`

**Interfaces:**
- Consumes: GitHub Release(Task 3), 홈서버 `content/update.json`
- Produces: 메뉴 항목 `6. 새 버전 배포`

- [ ] **Step 1: 구현**

`tools/sherbet_admin.py` 상단 상수 구역(`FILES = ...` 아래)에 추가:

```python
UPDATE = f"{CONTENT}/update.json"
REPO = "Jeong-Ryeol/reshade"
URL_PREFIX = f"https://github.com/{REPO}/releases/download/"
ASSETS = {"x64": "ReShade64.dll", "x86": "ReShade32.dll"}
```

`show_list` 앞에 함수 추가:

```python
def _pe_arch(data: bytes):
    """PE 헤더에서 아키텍처를 읽는다. 클라 pe_check 와 같은 판정 —
    x64 슬롯에 32비트 DLL 을 올리면 구매자 게임이 아예 안 켜지고 롤백도 안 돈다."""
    import struct
    if len(data) < 0x40 or data[:2] != b"MZ":
        return None
    e = struct.unpack_from("<I", data, 0x3c)[0]
    if e < 0x40 or e + 24 > len(data) or data[e:e + 4] != b"PE\0\0":
        return None
    machine = struct.unpack_from("<H", data, e + 4)[0]
    chars = struct.unpack_from("<H", data, e + 22)[0]
    if not (chars & 0x2000):          # IMAGE_FILE_DLL
        return None
    return {0x8664: "x64", 0x014c: "x86"}.get(machine)


def publish_update():
    import hashlib
    import re
    import subprocess
    import tempfile

    tag = ask("릴리스 태그 (예: sherbet-1.4.0)")
    if not re.fullmatch(r"sherbet-\d+\.\d+\.\d+", tag):
        die("태그는 sherbet-1.4.0 형식이어야 합니다 (v 를 넣지 마세요 — "
            "sherbet-v1.4.0 은 리쉐이드 version.h 를 탈취합니다)")
    version = tag[len("sherbet-"):]

    with tempfile.TemporaryDirectory() as tmp:
        print(f"릴리스 {tag} 에셋 다운로드 중…")
        r = subprocess.run(["gh", "release", "download", tag, "-R", REPO, "-D", tmp],
                           capture_output=True, text=True)
        if r.returncode != 0:
            die(f"다운로드 실패: {r.stderr.strip()}")

        builds = {}
        for arch, name in ASSETS.items():
            path = os.path.join(tmp, name)
            if not os.path.isfile(path):
                die(f"릴리스에 {name} 이 없습니다")
            data = open(path, "rb").read()

            # (a) 해시는 여기서 직접 계산한다. CI 출력 복붙 금지 —
            #     '릴리스 이후 에셋이 바뀌었나' 를 잡는 유일한 지점이다.
            sha = hashlib.sha256(data).hexdigest()
            # (b) 아키텍처 확인
            got = _pe_arch(data)
            if got != arch:
                die(f"{name} 의 PE 아키텍처가 {got} 입니다 ({arch} 여야 함)")
            # (c) 바이너리 안의 버전이 태그와 같은지
            if version.encode() not in data:
                die(f"{name} 안에 버전 문자열 {version} 이 없습니다 — "
                    f"태그와 다른 빌드가 올라갔을 수 있습니다")
            # (d) 크기 범위(클라가 1MiB~32MiB 밖을 거부한다)
            if not (1 << 20) <= len(data) <= (32 << 20):
                die(f"{name} 크기 {len(data)} 가 1MiB~32MiB 밖입니다")

            builds[arch] = {"url": f"{URL_PREFIX}{tag}/{name}",
                            "size": len(data), "sha256": sha}
            print(f"  {name}: {arch}, {len(data)} bytes, sha256={sha[:16]}…")

    # 변경내역은 릴리스 본문을 단일 출처로 삼는다
    r = subprocess.run(["gh", "release", "view", tag, "-R", REPO, "--json", "body", "-q", ".body"],
                       capture_output=True, text=True)
    notes = (r.stdout or "").strip() if r.returncode == 0 else ""
    print(f"\n변경내역(릴리스 본문에서 가져옴):\n{notes or '(비어 있음)'}\n")
    if confirm("이 내용으로 배포할까요?") is False:
        print("취소했습니다."); return

    doc = {
        "schema": 1,
        "version": version,
        "min_version": ask("최소 버전 (이 미만은 빨간 배너, 비우면 없음)", default="", required=False),
        "allow_downgrade": False,
        "notice": "",
        "notes": notes,
        "builds": builds,
    }
    save_manifest(UPDATE, doc)
    print(f"✅ {UPDATE} 배치 완료")
    print("   확인: curl -s 'https://wonryeol.asuscomm.com/sherbet-auth/update/manifest?arch=x64' | head -c 400")
```

`actions` 딕셔너리에 추가(`"5"` 뒤, `"0"` 앞):

```python
        "6": ("새 버전 배포", publish_update),
```

- [ ] **Step 2: PE 판정을 실제 DLL 로 검증**

Run:
```bash
cd ~/reshade && python3 -c "
import sys; sys.path.insert(0,'tools')
import importlib.util
spec=importlib.util.spec_from_file_location('adm','tools/sherbet_admin.py')
m=importlib.util.module_from_spec(spec)
import types
# main() 실행을 막기 위해 __name__ 을 바꿔 로드
m.__name__='adm'; spec.loader.exec_module(m)
for p,exp in [('/tmp/sherbet64/ReShade64.dll','x64'),('/tmp/sherbet32/ReShade32.dll','x86')]:
    try:
        got=m._pe_arch(open(p,'rb').read())
        print(f'{p}: {got} (기대 {exp}) {\"OK\" if got==exp else \"불일치\"}')
    except FileNotFoundError:
        print(f'{p}: 없음(건너뜀)')
"
```
Expected: 각각 `x64`, `x86` 으로 판정. 파일이 없으면 `gh run download` 로 받아서 다시.

- [ ] **Step 3: 커밋**

```bash
git add tools/sherbet_admin.py
git commit -m "관리자 도구: 새 버전 배포 메뉴 추가

릴리스 에셋을 다시 받아 해시를 직접 계산한다 — CI 출력을 복붙하면
'릴리스 이후 에셋이 바뀌었나' 를 잡을 방법이 없어진다.
PE 아키텍처와 바이너리 안의 버전 문자열도 대조한 뒤에만 매니페스트를 만든다."
```

---

## Task 5: 서버 배포 + 라이브 확인

**Files:** 없음(운영 작업)

- [ ] **Step 1: 서버에 코드 배포**

Run:
```bash
cd ~/reshade/server && rsync -av --delete \
  -e "ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null" \
  app/ wonryeol5336-server@192.168.50.95:~/sherbet-auth/app/
```
(내부IP 직결 — DDNS 는 헤어핀 NAT 로 큰 전송이 절단된다)

- [ ] **Step 2: 서버에서 테스트 실행 후 재시작**

Run:
```bash
ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null wonryeol5336-server@192.168.50.95 \
  'cd ~/sherbet-auth && .venv/bin/python -m pytest -q 2>&1 | tail -3; export XDG_RUNTIME_DIR=/run/user/$(id -u); systemctl --user restart sherbet-auth; sleep 2; systemctl --user is-active sherbet-auth'
```
Expected: 테스트 통과, `active`

- [ ] **Step 3: 매니페스트가 없는 상태에서 503 인지 확인**

Run: `curl -s -o /dev/null -w '%{http_code}\n' 'https://wonryeol.asuscomm.com/sherbet-auth/update/manifest?arch=x64'`
Expected: `503` — `update.json` 을 아직 안 올렸으므로. **500 이나 404 가 나오면 라우터 등록이나 nginx 경로 문제다.**

- [ ] **Step 4: 기존 엔드포인트 회귀 확인**

Run:
```bash
curl -s -o /dev/null -w 'health=%{http_code} ' 'https://wonryeol.asuscomm.com/sherbet-auth/health'
curl -s -o /dev/null -w 'content=%{http_code}\n' 'https://wonryeol.asuscomm.com/sherbet-auth/content/me'
```
Expected: `health=200 content=401` — 인증 경로가 그대로 살아 있어야 한다.

---

## Task 6: 런북 문서

**Files:**
- Create: `docs/sherbet-업데이트-런북.md`
- Modify: `docs/sherbet-관리자-가이드.md`

- [ ] **Step 1: 런북 작성**

`docs/sherbet-업데이트-런북.md` — 폰으로 보고 따라할 수 있게 명령어 나열:

```markdown
# Sherbet 업데이트 런북

## 새 버전 배포 (순서 지킬 것)

1. `source/sherbet_owner.h` 의 `SHERBET_VERSION` 을 올리고, 변경내역과 함께 커밋·푸시
2. CI green 확인 (`gh run list --workflow=build.yml --limit 1`)
3. **태그는 v 없이:**
   ```
   git tag sherbet-1.4.0 && git push origin sherbet-1.4.0
   ```
   ⚠️ `sherbet-v1.4.0` 은 리쉐이드 `version.h` 를 탈취한다. **`v` 를 넣지 말 것.**
4. release 워크플로 green 확인, 릴리스 에셋 URL 이 200 인지 확인
5. `python3 tools/sherbet_admin.py` → `6. 새 버전 배포` → 태그 입력
6. 밖에서 확인:
   ```
   curl -s 'https://wonryeol.asuscomm.com/sherbet-auth/update/manifest?arch=x64'
   ```
7. **카나리:** 내 PC 1대에서 실제 업데이트 → 재시작 → 새 버전 확인.
   이걸 통과하기 전에는 디스코드 공지 금지.

## 긴급 정지 (킬스위치)

문제가 생기면 홈서버 `content/update.json` 을 이전 버전 + `allow_downgrade: true` 로 되돌린다.
서명키도 GitHub 도 필요 없다. 신규 제안이 즉시 멈추고, **이미 업데이트한 사람도 다음 실행에 스스로 강등**된다.

```
ssh wonryeol5336-server@192.168.50.95
nano ~/sherbet-auth/content/update.json
# version 을 이전 값으로, allow_downgrade 를 true 로
```
서버 재시작 불필요(mtime 캐시).

## 손님 게임이 안 켜질 때

게임 폴더에 `Sherbet-복구안내.txt` 가 있으면 그 안내대로.
없으면: `dxgi.dll.sherbet-bak` 을 `dxgi.dll` 로 이름 변경.

## 절대 하지 말 것

- CI 가 홈서버에 SSH 로 쓰게 만들기 (GitHub 침해 = 홈서버 침해가 된다)
- CI 가 출력한 sha256 을 매니페스트에 복붙 (릴리스 이후 에셋 교체를 못 잡는다)
- `sherbet-v1.4.0` 형식 태그
```

- [ ] **Step 2: 관리자 가이드에 한 줄 링크 추가**

`docs/sherbet-관리자-가이드.md` 의 "판매용 DLL 빌드" 절 근처에:

```markdown
> **새 버전 배포는** `docs/sherbet-업데이트-런북.md` 참고. 태그는 `sherbet-1.4.0` (v 없음).
```

- [ ] **Step 3: 커밋**

```bash
git add docs/sherbet-업데이트-런북.md docs/sherbet-관리자-가이드.md
git commit -m "업데이트 런북 추가

발행 순서, 킬스위치, 손님 복구, 절대 하지 말 것.
태그에 v 를 넣으면 안 되는 이유를 굵게 적었다."
```

---

## Self-Review

**1. 스펙 커버리지** — 이 계획이 다루는 절: §3.1(전달 경로), §3.2(서버 파일), §3.3(평면 응답·타입 규약), §7(릴리스 파이프라인 1~9, 11~13). **다루지 않는 절(3단계로 이월):** §3.4~3.5의 클라측 소비, §4(자기교체), §5(부팅마커 런타임), §6(클라 구조), §7의 10(카나리 — 운영 작업).

**2. 플레이스홀더** — 없음. 모든 코드 스텝에 실제 코드, 모든 실행 스텝에 명령과 기대 출력.

**3. 타입 일관성** — `load_json_object`(T1) → `update.py`(T2)에서 사용. `UPDATE_PATH`/`UPDATE_CACHE`는 테스트가 `monkeypatch`로 바꾸므로 모듈 수준 변수여야 한다(T2 Step 1·3 일치). `_pe_arch`(T4)는 클라 `pe_check`와 같은 판정을 하되 파이썬 구현이다 — 두 곳이 손으로 동기화되므로 T4 Step 2에서 실제 DLL로 대조한다.

**4. 1단계와의 계약** — T2 Step 5가 서버 응답을 실제 `parse_manifest`에 먹이는 유일한 교차 검증 지점이다. 이 스텝을 건너뛰면 두 층이 서로 다른 계약을 믿는 채로 3단계에 들어간다.

---

## 이 계획이 끝나면

- 태그 하나로 GitHub Release 발행 → 관리자 도구 한 번으로 매니페스트 배포
- `curl` 로 매니페스트가 확인되고, 그 응답이 1단계 파서를 실제로 통과함
- **여전히 구매자에겐 아무 일도 안 일어난다** — 물어보는 클라가 없다

**다음:** 3단계 — 클라가 매니페스트를 묻고, 배너를 띄우고, 다운받아 자기를 교체하고, 실패하면 되돌린다.
