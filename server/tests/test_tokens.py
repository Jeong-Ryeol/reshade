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


def test_verify_token_skips_hwid_when_none(settings):
    tok = issue_token(settings, "user-1", "HW-A", ["sherbet-buyer"])
    # hwid=None → hwid 불일치여도(검사 안 함) 통과, sub 확인
    payload = verify_token(settings, tok, hwid=None)
    assert payload is not None and payload["sub"] == "user-1"


def test_verify_token_none_hwid_still_checks_expiry(settings):
    # 이미 만료된 토큰(now 주입) → hwid=None 이어도 만료로 거부
    tok = issue_token(settings, "user-1", "HW-A", ["sherbet-buyer"], now=1000)
    assert verify_token(settings, tok, hwid=None, now=10**12) is None
