import threading


class PendingStore:
    def __init__(self) -> None:
        self._lock = threading.Lock()
        self._hwid: dict[str, str] = {}
        self._result: dict[str, dict] = {}

    def put_pending(self, state: str, hwid: str) -> None:
        with self._lock:
            self._hwid[state] = hwid

    def get_hwid(self, state: str) -> str | None:
        with self._lock:
            return self._hwid.get(state)

    def set_result(self, state: str, token: str) -> None:
        with self._lock:
            self._result[state] = {"status": "ready", "token": token}

    def set_denied(self, state: str, reason: str) -> None:
        with self._lock:
            self._result[state] = {"status": "denied", "reason": reason}

    def pop_result(self, state: str) -> dict | None:
        with self._lock:
            if state not in self._hwid:
                return None
            if state in self._result:
                res = self._result.pop(state)
                self._hwid.pop(state, None)
                return res
            return {"status": "pending"}
