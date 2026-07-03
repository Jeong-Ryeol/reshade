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
