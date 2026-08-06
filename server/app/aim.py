"""Sherbet 사격 훈련 리더보드.

── 왜 서버가 필요한가 ────────────────────────────────────────────────────────
개인 최고 기록은 클라가 ini 에 들고 있으면 그만이다. 서버가 필요한 것은 **남과
비교**하기 위해서다. 그리고 비교가 성립하려면 (1) 누가 냈는지 알아야 하고,
(2) 말이 안 되는 값이 걸러져야 한다.

(1)은 공짜다 — 인증 토큰에 디스코드 표시이름이 이미 들어 있다(공용 DLL 이름
릴레이 때 넣은 name 클레임). 익명이 아니라는 것 자체가 가장 센 방어다.

(2)는 여기서 한다. 클라가 보낸 점수를 그대로 믿지 않는다. 다만 **완전 방어는
하지 않는다** — 그러려면 서버가 표적 순서를 정하고 클라가 마우스 궤적 전체를
보내 서버가 재계산해야 하는데, 지금 규모에서 그건 안 쓸 방어에 시간을 쓰는 것이다.
지나가다 장난치는 것을 막고, 실제 조작이 나타나면 그때 궤적 검증을 얹는다.

── 10초는 왜 못 올리나 ──────────────────────────────────────────────────────
짧을수록 표본이 적어 편차가 크다(60초 대비 약 2.5배). 10초는 싸서 수십 번 돌릴
수 있고, 그중 제일 잘 나온 판만 남으면 그건 실력이 아니라 운이다. 그러면 상위
5명이 전부 10초 기록으로 채워지고 60초를 성실히 돈 사람은 영영 못 올라간다.
클라도 같은 계약을 갖고 있다(sherbet_aim.hpp 의 ranked()).
"""

import json
import os
import time

from fastapi import APIRouter, Depends, Header, HTTPException
from pydantic import BaseModel

from app.config import Settings, get_settings
from app.tokens import verify_token

router = APIRouter()

SCORES_PATH = os.path.join(
    os.path.dirname(os.path.dirname(__file__)), "content", "aim_scores.json")

# 난이도·판 길이. **클라의 sherbet_aim.hpp 와 글자 하나까지 같아야 한다** —
# 여기가 어긋나면 클라가 올린 기록이 엉뚱한 표에 들어가거나 통째로 거부된다.
LEVELS = ("easy", "normal", "hard", "hell")
DURATIONS = {"s30": 30, "s60": 60}   # 순위에 올라가는 길이만 받는다(10초 없음)

# 사람이 낼 수 있는 상한. 초당 3개는 아주 잘하는 사람의 쉬움 난이도보다도 위다 —
# 넉넉하게 잡아 정상 기록을 자르지 않으면서, 자동화된 값은 걸러낸다.
MAX_HITS_PER_SECOND = 3.0

# 상위 몇 명을 보여주는가.
TOP_N = 5

# 표시 이름 상한. 디스코드 표시이름은 32자지만 방어적으로 자른다.
MAX_NAME = 40


def board_key(level: str, duration: str) -> str:
    """난이도·길이 조합 하나가 리더보드 하나다. 8개(4×2)가 된다."""
    return f"{level}:{duration}"


def valid_level(level: str) -> bool:
    return level in LEVELS


def valid_duration(duration: str) -> bool:
    return duration in DURATIONS


def plausible(level: str, duration: str, hits: int, shots: int) -> bool:
    """사람이 낼 수 있는 값인가.

    막는 것은 셋뿐이다:
      - 음수/비정수
      - 명중이 클릭보다 많음 (정확도 100% 초과 — 정의상 불가능)
      - 초당 명중 수가 인간 한계를 넘음

    난이도별로 상한을 다르게 두지 않는다. 어려운 난이도의 상한을 조이면 실력이
    는 사람의 정상 기록을 언젠가 자르게 되는데, 그건 조작을 막는 것보다 나쁘다.
    """
    if not isinstance(hits, int) or not isinstance(shots, int):
        return False
    if isinstance(hits, bool) or isinstance(shots, bool):  # bool 은 int 의 하위형이다
        return False
    if hits < 0 or shots < 0:
        return False
    if hits > shots:
        return False
    seconds = DURATIONS.get(duration)
    if seconds is None:
        return False
    return hits <= int(seconds * MAX_HITS_PER_SECOND)


def load_scores(path: str | None = None) -> dict:
    """없으면 빈 표. 깨졌으면 빈 표(기록은 잃어도 서비스는 계속 돌아야 한다).

    ⚠️ 기본값을 `path=SCORES_PATH` 로 묶으면 안 된다. 기본 인자는 **정의 시점에**
    값이 박히므로 테스트가 SCORES_PATH 를 갈아 끼워도 함수는 옛 경로를 계속 쓴다 —
    실제로 그래서 테스트가 운영 파일에 썼다. 호출 시점에 모듈 전역을 읽는다.
    """
    if path is None:
        path = SCORES_PATH
    try:
        with open(path, "r", encoding="utf-8") as f:
            data = json.load(f)
    except (OSError, ValueError):
        return {}
    return data if isinstance(data, dict) else {}


def save_scores(data: dict, path: str | None = None) -> None:
    """원자적 교체. 반쯤 쓰인 파일이 조회에 노출되지 않게 한다.

    경로를 호출 시점에 읽는 이유는 load_scores() 주석 참조.
    """
    if path is None:
        path = SCORES_PATH
    os.makedirs(os.path.dirname(path), exist_ok=True)
    tmp = path + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(data, f, ensure_ascii=False, indent=2)
    os.replace(tmp, path)


def upsert_score(data: dict, level: str, duration: str, user_id: str,
                 name: str, hits: int, shots: int, now: float | None = None) -> bool:
    """개인 최고 기록만 남긴다. 갱신했으면 True.

    ⚠️ 더 낮은 점수로 덮어쓰지 않는다. 안 그러면 1등을 한 뒤 아무 판이나 대충 돌리면
    자기 기록이 사라진다 — 리더보드가 '마지막 판' 표가 되어 버린다.
    이름은 매번 갱신한다(디스코드에서 바꿀 수 있으므로).
    """
    key = board_key(level, duration)
    board = data.setdefault(key, {})
    prev = board.get(user_id)
    ts = time.time() if now is None else now
    if prev is not None and int(prev.get("hits", 0)) >= hits:
        prev["name"] = name[:MAX_NAME]     # 이름만 최신으로
        return False
    board[user_id] = {
        "name": name[:MAX_NAME],
        "hits": hits,
        "shots": shots,
        "ts": ts,
    }
    return True


def ranked_entries(data: dict, level: str, duration: str) -> list[dict]:
    """점수 내림차순. 동점이면 먼저 낸 사람이 위다 — 나중에 온 사람이 동점으로
    앞지르면 앞 사람은 아무것도 안 했는데 순위가 밀린다."""
    board = data.get(board_key(level, duration), {})
    rows = []
    for uid, e in board.items():
        rows.append({
            "user_id": uid,
            "name": str(e.get("name", ""))[:MAX_NAME],
            "hits": int(e.get("hits", 0)),
            "shots": int(e.get("shots", 0)),
            "ts": float(e.get("ts", 0.0)),
        })
    rows.sort(key=lambda r: (-r["hits"], r["ts"]))
    return rows


def rank_of(rows: list[dict], user_id: str) -> int:
    """1부터. 없으면 0."""
    for i, r in enumerate(rows):
        if r["user_id"] == user_id:
            return i + 1
    return 0


def public_rows(rows: list[dict], limit: int = TOP_N) -> list[dict]:
    """밖으로 나가는 형태. user_id 는 빼고 순위를 붙인다 —
    디스코드 사용자 id 를 리더보드로 흘릴 이유가 없다.

    ⚠️ **모든 숫자를 문자열로 낸다.** 클라의 평면 JSON 헬퍼(sherbet_json.hpp)는
    따옴표로 감싼 문자열만 읽는다 — 숫자를 그대로 내면 클라가 조용히 0 으로 읽고
    리더보드가 전부 '0개' 로 보인다. 아무 데도 오류가 안 난다.
    업데이트 매니페스트가 같은 이유로 같은 규약을 쓴다(설계 §3.3).
    """
    out = []
    for i, r in enumerate(rows[:limit]):
        acc = (r["hits"] / r["shots"]) if r["shots"] > 0 else 0.0
        out.append({
            "rank": str(i + 1),
            "name": r["name"],
            "hits": str(r["hits"]),
            "accuracy": f"{acc:.4f}",
        })
    return out


# ── HTTP ────────────────────────────────────────────────────────────────────
class ScoreBody(BaseModel):
    level: str
    duration: str
    hits: int
    shots: int


def _identity(authorization: str | None, settings: Settings) -> dict:
    """Bearer 토큰에서 신원만 뽑는다(hwid 는 안 본다 — 리더보드는 기기와 무관하다)."""
    if not authorization or not authorization.startswith("Bearer "):
        raise HTTPException(status_code=401, detail="unauthorized")
    payload = verify_token(settings, authorization[7:], hwid=None)
    if payload is None:
        raise HTTPException(status_code=401, detail="unauthorized")
    return payload


@router.post("/aim/score")
def aim_score(
    body: ScoreBody,
    authorization: str | None = Header(default=None),
    settings: Settings = Depends(get_settings),
) -> dict:
    payload = _identity(authorization, settings)

    if not valid_level(body.level) or not valid_duration(body.duration):
        # 10초를 올리려 한 경우도 여기로 온다. 클라가 애초에 안 보내지만,
        # 서버가 계약의 단일 지점이어야 한다.
        raise HTTPException(status_code=400, detail="bad_board")
    if not plausible(body.level, body.duration, body.hits, body.shots):
        raise HTTPException(status_code=400, detail="implausible")

    user_id = str(payload.get("sub", ""))
    if not user_id:
        raise HTTPException(status_code=401, detail="unauthorized")

    data = load_scores()
    changed = upsert_score(data, body.level, body.duration, user_id,
                           str(payload.get("name", "")), body.hits, body.shots)
    if changed:
        save_scores(data)

    rows = ranked_entries(data, body.level, body.duration)
    # 숫자는 문자열, 불리언은 진짜 불리언 — public_rows() 주석 참조.
    # ⚠️ 최상위 키를 my_* 로 쓰는 이유는 아래 aim_leaderboard() 주석 참조.
    return {
        "updated": changed,
        "my_rank": str(rank_of(rows, user_id)),
        "total": str(len(rows)),
        "top": public_rows(rows),
    }


@router.get("/aim/leaderboard")
def aim_leaderboard(
    level: str,
    duration: str,
    authorization: str | None = Header(default=None),
    settings: Settings = Depends(get_settings),
) -> dict:
    payload = _identity(authorization, settings)
    if not valid_level(level) or not valid_duration(duration):
        raise HTTPException(status_code=400, detail="bad_board")

    user_id = str(payload.get("sub", ""))
    data = load_scores()
    rows = ranked_entries(data, level, duration)
    me = rank_of(rows, user_id)
    my_hits = 0
    for r in rows:
        if r["user_id"] == user_id:
            my_hits = r["hits"]
            break
    # ⚠️ 최상위 필드 이름이 `top` 줄의 필드와 **겹치면 안 된다.**
    # 클라의 평면 헬퍼(sherbet_json.hpp)는 중첩을 모르고 body 전체에서 첫 "key" 를
    # 찾는다. 최상위를 그냥 "rank" 로 두면 top[0] 의 "rank" 가 먼저 걸려서, 내 순위가
    # 항상 1위로 읽힌다 — 아무 오류도 없이 조용히 틀린다. 그래서 my_ 를 붙인다.
    # (설계 §3.3: 새 페이로드는 평면 파서에 맞게 설계한다.)
    return {
        "level": level,
        "duration": duration,
        "top": public_rows(rows),
        "my_rank": str(me),
        "my_hits": str(my_hits),
        "total": str(len(rows)),
    }
