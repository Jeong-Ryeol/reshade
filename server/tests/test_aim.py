"""사격 훈련 리더보드.

순수 함수(판정·정렬·저장)와 라우터를 함께 검사한다. 특히 두 가지를 못박는다:
  - 10초는 절대 순위에 못 올라간다(클라와 서버가 같은 계약을 갖는지)
  - 낮은 점수가 최고 기록을 덮어쓰지 않는다(리더보드가 '마지막 판' 표가 되지 않게)
"""

import json

from fastapi.testclient import TestClient

import app.aim as aim_module
from app.aim import (
    DURATIONS,
    LEVELS,
    board_key,
    load_scores,
    plausible,
    public_rows,
    rank_of,
    ranked_entries,
    save_scores,
    upsert_score,
    valid_duration,
    valid_level,
)
from app.config import get_settings
from app.main import app
from app.tokens import issue_token


def _override(settings):
    app.dependency_overrides[get_settings] = lambda: settings


def _reset():
    app.dependency_overrides.clear()


def _auth(settings, user_id="u1", name="정렬"):
    return {"Authorization": "Bearer " + issue_token(
        settings, user_id=user_id, hwid="hw", roles=["role-buyer"], name=name)}


# ── 계약 ────────────────────────────────────────────────────────────────────
def test_boards():
    assert valid_level("easy") and valid_level("hell")
    assert not valid_level("Easy")      # 대소문자는 계약 그대로
    assert not valid_level("insane")
    assert len(LEVELS) == 4

    # ★ 10초는 리더보드가 없다. 짧은 판은 편차가 커서 운이 순위를 정한다.
    assert not valid_duration("s10")
    assert valid_duration("s30") and valid_duration("s60")
    assert set(DURATIONS) == {"s30", "s60"}

    # ★ 자유 모드 키는 모드가 생기기 전과 **똑같아야** 한다 — 안 그러면 1.7.4 이하에서
    #   쌓인 기록이 통째로 사라진다.
    assert board_key("hell", "s60") == "hell:s60"
    assert board_key("hell", "s60", "free") == "hell:s60"
    # 수평은 자유도가 하나 줄어 더 쉬우므로 표가 갈린다.
    assert board_key("hell", "s60", "level") == "level:hell:s60"
    assert board_key("hell", "s60", "level") != board_key("hell", "s60", "free")


# ── 타당성 ──────────────────────────────────────────────────────────────────
def test_plausible():
    assert plausible("normal", "s60", 40, 50)
    assert plausible("normal", "s60", 0, 0)

    # 명중이 클릭보다 많을 수 없다 — 정확도 100% 초과는 정의상 불가능하다.
    assert not plausible("normal", "s60", 51, 50)

    # 인간 한계. 60초 * 3.0 = 180 까지만.
    assert plausible("easy", "s60", 180, 180)
    assert not plausible("easy", "s60", 181, 181)
    assert plausible("easy", "s30", 90, 90)
    assert not plausible("easy", "s30", 91, 91)

    # 음수·10초·bool 방어. bool 은 int 의 하위형이라 명시적으로 막아야 한다.
    assert not plausible("normal", "s60", -1, 5)
    assert not plausible("normal", "s60", 5, -1)
    assert not plausible("normal", "s10", 5, 5)
    assert not plausible("normal", "s60", True, 5)


# ── 저장 ────────────────────────────────────────────────────────────────────
def test_upsert_keeps_best():
    data = {}
    assert upsert_score(data, "hard", "s60", "u1", "가", 30, 40, now=100.0)

    # ★ 더 낮은 점수로 덮어쓰지 않는다. 안 그러면 1등 뒤에 대충 한 판이 기록을 지운다.
    assert not upsert_score(data, "hard", "s60", "u1", "가", 10, 40, now=200.0)
    assert data["hard:s60"]["u1"]["hits"] == 30

    # 동점도 갱신하지 않는다(먼저 낸 시각을 지켜야 동점 정렬이 흔들리지 않는다).
    assert not upsert_score(data, "hard", "s60", "u1", "가", 30, 31, now=300.0)
    assert data["hard:s60"]["u1"]["ts"] == 100.0

    # 더 높으면 갱신.
    assert upsert_score(data, "hard", "s60", "u1", "가", 31, 40, now=400.0)
    assert data["hard:s60"]["u1"]["hits"] == 31

    # 이름은 낮은 점수를 냈어도 최신으로 따라간다(디스코드에서 바꿀 수 있다).
    upsert_score(data, "hard", "s60", "u1", "새이름", 1, 1, now=500.0)
    assert data["hard:s60"]["u1"]["name"] == "새이름"
    assert data["hard:s60"]["u1"]["hits"] == 31


def test_ranking_and_ties():
    data = {}
    upsert_score(data, "easy", "s30", "a", "A", 20, 25, now=100.0)
    upsert_score(data, "easy", "s30", "b", "B", 30, 30, now=110.0)
    upsert_score(data, "easy", "s30", "c", "C", 20, 20, now=90.0)  # a 와 동점, 더 먼저

    rows = ranked_entries(data, "easy", "s30")
    assert [r["name"] for r in rows] == ["B", "C", "A"]  # 동점은 먼저 낸 쪽이 위
    assert rank_of(rows, "b") == 1
    assert rank_of(rows, "c") == 2
    assert rank_of(rows, "zzz") == 0

    # 밖으로 나가는 형태에는 user_id 가 없어야 한다.
    pub = public_rows(rows)
    assert all("user_id" not in p for p in pub)
    # ★ 숫자는 전부 **문자열**이다. 클라의 평면 JSON 헬퍼는 따옴표 있는 값만 읽으므로,
    #   숫자로 내보내면 리더보드가 조용히 전부 '0개' 로 보인다(아무 오류도 안 난다).
    assert pub[0] == {"rank": "1", "name": "B", "hits": "30", "accuracy": "1.0000"}
    assert all(isinstance(p["hits"], str) and isinstance(p["rank"], str) for p in pub)

    # 상위 N 만.
    assert len(public_rows(rows, limit=2)) == 2


def test_save_load_roundtrip(tmp_path):
    p = str(tmp_path / "scores.json")
    data = {}
    upsert_score(data, "hell", "s60", "u9", "구", 7, 9, now=1.0)
    save_scores(data, p)
    assert load_scores(p) == data

    # 깨진 파일은 빈 표로 — 기록은 잃어도 서비스는 돌아야 한다.
    with open(p, "w", encoding="utf-8") as f:
        f.write("{ this is not json")
    assert load_scores(p) == {}

    # 없는 파일도 빈 표.
    assert load_scores(str(tmp_path / "nope.json")) == {}


# ── 라우터 ──────────────────────────────────────────────────────────────────
def _use_tmp_scores(monkeypatch, tmp_path):
    p = str(tmp_path / "aim_scores.json")
    monkeypatch.setattr(aim_module, "SCORES_PATH", p)
    return p


def test_score_requires_auth(settings, monkeypatch, tmp_path):
    _use_tmp_scores(monkeypatch, tmp_path)
    _override(settings)
    try:
        c = TestClient(app)
        body = {"level": "normal", "duration": "s60", "hits": 10, "shots": 12}
        assert c.post("/aim/score", json=body).status_code == 401
        assert c.post("/aim/score", json=body, headers={"Authorization": "Bearer junk"}).status_code == 401
        assert c.get("/aim/leaderboard?level=normal&duration=s60").status_code == 401
    finally:
        _reset()


def test_score_submit_and_board(settings, monkeypatch, tmp_path):
    path = _use_tmp_scores(monkeypatch, tmp_path)
    _override(settings)
    try:
        c = TestClient(app)
        r = c.post("/aim/score", headers=_auth(settings, "u1", "정렬"),
                   json={"level": "hard", "duration": "s60", "hits": 40, "shots": 50})
        assert r.status_code == 200
        j = r.json()
        # updated 는 진짜 불리언(json_bool 로 읽는다), 숫자는 문자열.
        # ★ 최상위는 my_rank 다 — 그냥 "rank" 로 두면 클라 평면 파서가 top[0] 의
        #   "rank" 를 먼저 집어서 내 순위가 항상 1위로 읽힌다(조용히 틀린다).
        assert j["updated"] is True and j["my_rank"] == "1" and j["total"] == "1"
        assert "rank" not in j  # 최상위에 겹치는 키가 없어야 한다
        assert j["top"][0]["name"] == "정렬"

        # 두 번째 사람이 더 잘하면 1위가 바뀐다.
        r2 = c.post("/aim/score", headers=_auth(settings, "u2", "서아연"),
                    json={"level": "hard", "duration": "s60", "hits": 55, "shots": 60})
        assert r2.json()["my_rank"] == "1"

        # 첫 사람의 순위는 2위로 밀린다.
        lb = c.get("/aim/leaderboard?level=hard&duration=s60",
                   headers=_auth(settings, "u1", "정렬")).json()
        assert lb["my_rank"] == "2" and lb["my_hits"] == "40" and lb["total"] == "2"
        assert "rank" not in lb and "hits" not in lb  # top 줄과 키가 겹치면 안 된다
        assert [t["name"] for t in lb["top"]] == ["서아연", "정렬"]

        # 다른 난이도는 완전히 별개 표다.
        other = c.get("/aim/leaderboard?level=easy&duration=s60",
                      headers=_auth(settings, "u1")).json()
        assert other["top"] == [] and other["my_rank"] == "0"

        # 실제로 디스크에 남았다.
        with open(path, encoding="utf-8") as f:
            assert "hard:s60" in json.load(f)
    finally:
        _reset()


def test_score_rejects_bad_board_and_implausible(settings, monkeypatch, tmp_path):
    _use_tmp_scores(monkeypatch, tmp_path)
    _override(settings)
    try:
        c = TestClient(app)
        h = _auth(settings)

        # ★ 10초 기록은 서버가 거부한다. 클라가 안 보내지만 계약의 단일 지점은 서버다.
        r = c.post("/aim/score", headers=h,
                   json={"level": "normal", "duration": "s10", "hits": 5, "shots": 5})
        assert r.status_code == 400 and r.json()["detail"] == "bad_board"

        r = c.post("/aim/score", headers=h,
                   json={"level": "nope", "duration": "s60", "hits": 5, "shots": 5})
        assert r.status_code == 400 and r.json()["detail"] == "bad_board"

        # 사람이 못 내는 값.
        r = c.post("/aim/score", headers=h,
                   json={"level": "hell", "duration": "s60", "hits": 999, "shots": 999})
        assert r.status_code == 400 and r.json()["detail"] == "implausible"

        # 명중 > 클릭.
        r = c.post("/aim/score", headers=h,
                   json={"level": "hell", "duration": "s60", "hits": 10, "shots": 3})
        assert r.status_code == 400 and r.json()["detail"] == "implausible"

        # 거부된 요청은 아무것도 남기지 않는다.
        lb = c.get("/aim/leaderboard?level=hell&duration=s60", headers=h).json()
        assert lb["total"] == "0"
    finally:
        _reset()


def test_modes_are_separate_boards(settings, monkeypatch, tmp_path):
    """수평 모드 기록이 자유 모드 표에 섞이면 안 된다 — 더 쉬운 판이 순위를 밀어낸다."""
    _use_tmp_scores(monkeypatch, tmp_path)
    _override(settings)
    try:
        c = TestClient(app)
        h = _auth(settings, "u1", "정렬")

        c.post("/aim/score", headers=h,
               json={"level": "hard", "duration": "s60", "hits": 30, "shots": 40, "mode": "free"})
        c.post("/aim/score", headers=h,
               json={"level": "hard", "duration": "s60", "hits": 55, "shots": 60, "mode": "level"})

        free = c.get("/aim/leaderboard?level=hard&duration=s60&mode=free", headers=h).json()
        lvl = c.get("/aim/leaderboard?level=hard&duration=s60&mode=level", headers=h).json()
        assert free["top"][0]["hits"] == "30"
        assert lvl["top"][0]["hits"] == "55"
        assert free["total"] == "1" and lvl["total"] == "1"

        # mode 를 안 보내면 자유 모드다 — 구버전 클라 호환.
        old = c.get("/aim/leaderboard?level=hard&duration=s60", headers=h).json()
        assert old["top"][0]["hits"] == "30"

        # 없는 모드는 거부.
        r = c.post("/aim/score", headers=h,
                   json={"level": "hard", "duration": "s60", "hits": 5, "shots": 5, "mode": "nope"})
        assert r.status_code == 400 and r.json()["detail"] == "bad_board"
        assert c.get("/aim/leaderboard?level=hard&duration=s60&mode=nope", headers=h).status_code == 400
    finally:
        _reset()


def test_leaderboard_shows_top_five_only(settings, monkeypatch, tmp_path):
    _use_tmp_scores(monkeypatch, tmp_path)
    _override(settings)
    try:
        c = TestClient(app)
        for i in range(8):
            c.post("/aim/score", headers=_auth(settings, f"u{i}", f"P{i}"),
                   json={"level": "normal", "duration": "s30", "hits": 10 + i, "shots": 40})
        lb = c.get("/aim/leaderboard?level=normal&duration=s30",
                   headers=_auth(settings, "u0", "P0")).json()
        assert len(lb["top"]) == 5
        assert [t["rank"] for t in lb["top"]] == ["1", "2", "3", "4", "5"]
        assert lb["total"] == "8"
        # 꼴찌도 자기 순위는 안다 — 5등 밖이라고 아무것도 안 보이면 다시 안 한다.
        assert lb["my_rank"] == "8"
    finally:
        _reset()
