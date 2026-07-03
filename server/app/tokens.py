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
