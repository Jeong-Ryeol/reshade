import httpx
import respx
from fastapi.testclient import TestClient

import app.main as main_module
from app.config import get_settings
from app.main import app
from app.store import PendingStore
from app.tokens import issue_token

API = "https://discord.com/api/v10"


def _override(settings):
    app.dependency_overrides[get_settings] = lambda: settings


def _reset():
    app.dependency_overrides.clear()
    main_module.store = PendingStore()  # 테스트 간 상태 격리


def test_health(settings):
    _override(settings)
    try:
        client = TestClient(app)
        assert client.get("/health").json() == {"status": "ok"}
    finally:
        _reset()


def test_auth_discord_redirects_and_registers_state(settings):
    _override(settings)
    try:
        _reset(); _override(settings)
        client = TestClient(app)
        r = client.get("/auth/discord", params={"state": "s1", "hwid": "HW1"},
                       follow_redirects=False)
        assert r.status_code == 307 or r.status_code == 302
        assert "discord.com/oauth2/authorize" in r.headers["location"]
        assert main_module.store.get_hwid("s1") == "HW1"
    finally:
        _reset()


@respx.mock
def test_callback_issues_token_for_buyer(settings):
    _reset(); _override(settings)
    try:
        main_module.store.put_pending("s2", "HW2")
        respx.post(f"{API}/oauth2/token").mock(
            return_value=httpx.Response(200, json={"access_token": "at"}))
        respx.get(f"{API}/users/@me").mock(
            return_value=httpx.Response(200, json={"id": "user-9"}))
        respx.get(f"{API}/guilds/guild-1/members/user-9").mock(
            return_value=httpx.Response(200, json={"roles": ["role-buyer"]}))
        client = TestClient(app)
        r = client.get("/auth/callback", params={"code": "c", "state": "s2"})
        assert r.status_code == 200
        poll = client.get("/auth/poll", params={"state": "s2"}).json()
        assert poll["status"] == "ready"
        assert poll["token"]
    finally:
        _reset()


@respx.mock
def test_callback_denies_non_buyer(settings):
    _reset(); _override(settings)
    try:
        main_module.store.put_pending("s3", "HW3")
        respx.post(f"{API}/oauth2/token").mock(
            return_value=httpx.Response(200, json={"access_token": "at"}))
        respx.get(f"{API}/users/@me").mock(
            return_value=httpx.Response(200, json={"id": "user-x"}))
        respx.get(f"{API}/guilds/guild-1/members/user-x").mock(
            return_value=httpx.Response(200, json={"roles": ["role-other"]}))
        client = TestClient(app)
        client.get("/auth/callback", params={"code": "c", "state": "s3"})
        poll = client.get("/auth/poll", params={"state": "s3"}).json()
        assert poll["status"] == "denied"
        assert poll["reason"] == "no_buyer_role"
    finally:
        _reset()


def test_poll_unknown_state_404(settings):
    _reset(); _override(settings)
    try:
        client = TestClient(app)
        assert client.get("/auth/poll", params={"state": "ghost"}).status_code == 404
    finally:
        _reset()


@respx.mock
def test_verify_valid_token_rechecks_roles(settings):
    _reset(); _override(settings)
    try:
        tok = issue_token(settings, "user-9", "HW9", ["sherbet-buyer"])  # 실제 현재 시각 발급(살아있는 토큰)
        respx.get(f"{API}/guilds/guild-1/members/user-9").mock(
            return_value=httpx.Response(200, json={"roles": ["role-buyer", "role-x"]}))
        client = TestClient(app)
        r = client.post("/auth/verify", json={"token": tok, "hwid": "HW9"})
        body = r.json()
        assert body["valid"] is True
        assert body["sub"] == "user-9"
        assert "sherbet-buyer" in body["roles"]
    finally:
        _reset()


def test_verify_bad_hwid(settings):
    _reset(); _override(settings)
    try:
        tok = issue_token(settings, "user-9", "HW9", ["sherbet-buyer"], now=1000)
        client = TestClient(app)
        r = client.post("/auth/verify", json={"token": tok, "hwid": "WRONG"})
        assert r.json() == {"valid": False}
    finally:
        _reset()
