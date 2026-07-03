import threading
import time

_DEFAULT_TTL_SECONDS = 300.0
_DEFAULT_CAP = 10000


class PendingStore:
    def __init__(
        self,
        ttl_seconds: float = _DEFAULT_TTL_SECONDS,
        cap: int = _DEFAULT_CAP,
    ) -> None:
        self._lock = threading.Lock()
        self._hwid: dict[str, str] = {}
        self._created: dict[str, float] = {}
        self._result: dict[str, dict] = {}
        self._ttl = ttl_seconds
        self._cap = cap

    @staticmethod
    def _now(now: float | None) -> float:
        return time.monotonic() if now is None else now

    def _drop(self, state: str) -> None:
        # 잠금 보유 상태에서만 호출
        self._hwid.pop(state, None)
        self._created.pop(state, None)
        self._result.pop(state, None)

    def _sweep_locked(self, now: float) -> None:
        # 잠금 보유 상태에서만 호출
        stale = [s for s, t in self._created.items() if now - t > self._ttl]
        for s in stale:
            self._drop(s)

    def _enforce_cap_locked(self) -> None:
        # 잠금 보유 상태에서만 호출: 오래된 것부터 제거해 cap 이하로 유지
        while len(self._created) > self._cap:
            oldest = min(self._created, key=self._created.__getitem__)
            self._drop(oldest)

    def put_pending(self, state: str, hwid: str, now: float | None = None) -> None:
        now = self._now(now)
        with self._lock:
            self._sweep_locked(now)
            self._hwid[state] = hwid
            self._created[state] = now
            self._enforce_cap_locked()

    def sweep(self, now: float | None = None) -> None:
        now = self._now(now)
        with self._lock:
            self._sweep_locked(now)

    def get_hwid(self, state: str, now: float | None = None) -> str | None:
        now = self._now(now)
        with self._lock:
            created = self._created.get(state)
            if created is not None and now - created > self._ttl:
                # TTL 초과한 state 는 읽는 순간에도 만료 처리(유휴 시 백그라운드 스윕이 없어도 강제)
                self._drop(state)
                return None
            return self._hwid.get(state)

    def set_result(self, state: str, token: str, name: str = "") -> None:
        with self._lock:
            if state in self._hwid:
                self._result[state] = {"status": "ready", "token": token, "name": name}

    def set_denied(self, state: str, reason: str) -> None:
        with self._lock:
            if state in self._hwid:
                self._result[state] = {"status": "denied", "reason": reason}

    def pop_result(self, state: str, now: float | None = None) -> dict | None:
        now = self._now(now)
        with self._lock:
            created = self._created.get(state)
            if created is not None and now - created > self._ttl:
                self._drop(state)
                return None
            if state not in self._hwid:
                return None
            if state in self._result:
                res = self._result.pop(state)
                self._drop(state)
                return res
            return {"status": "pending"}
