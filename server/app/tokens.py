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
    name: str = "",
) -> str:
    iat = _now(now)
    payload = {
        "sub": user_id,
        "hwid": hwid,
        "roles": roles,
        "name": name,  # 디스코드 표시이름 — 공용 DLL 이름 각인용
        "iat": iat,
        "exp": iat + settings.token_ttl_seconds,
    }
    return jwt.encode(payload, settings.session_secret, algorithm=_ALG)


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
