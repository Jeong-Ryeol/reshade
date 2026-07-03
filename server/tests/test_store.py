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


def test_ttl_evicts_stale_on_sweep():
    s = PendingStore(ttl_seconds=300)
    s.put_pending("old", "HW1", now=0)
    s.sweep(now=301)  # 301s > 300s TTL
    assert s.get_hwid("old") is None
    assert s.pop_result("old") is None  # expired == unknown


def test_fresh_entry_survives_sweep():
    s = PendingStore(ttl_seconds=300)
    s.put_pending("fresh", "HW2", now=100)
    s.sweep(now=200)  # only 100s old
    assert s.get_hwid("fresh") == "HW2"
    assert s.pop_result("fresh") == {"status": "pending"}


def test_put_pending_sweeps_stale_entries():
    s = PendingStore(ttl_seconds=300)
    s.put_pending("old", "HW1", now=0)
    s.put_pending("new", "HW2", now=301)  # this put triggers a sweep
    assert s.pop_result("old") is None
    assert s.pop_result("new") == {"status": "pending"}


def test_cap_eviction_drops_oldest():
    s = PendingStore(ttl_seconds=100000, cap=3)
    s.put_pending("a", "HWa", now=1)
    s.put_pending("b", "HWb", now=2)
    s.put_pending("c", "HWc", now=3)
    s.put_pending("d", "HWd", now=4)  # exceeds cap of 3 -> drop oldest ("a")
    assert s.get_hwid("a") is None
    assert s.get_hwid("b") == "HWb"
    assert s.get_hwid("d") == "HWd"
