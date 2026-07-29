import os
import shutil
import subprocess

import pytest

from app.config import Settings

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def _find_cxx() -> str | None:
    for cc in ("clang++", "g++", "c++"):
        p = shutil.which(cc)
        if p:
            return p
    return None


@pytest.fixture(scope="session")
def client_parser(tmp_path_factory):
    """라우터 응답을 **진짜 클라이언트 파서**(sherbet_update_core.hpp)에 먹이는 함수.

    ⚠️ 이 픽스처가 없으면 서버 계약은 파이썬 쪽 거울(“문자열인가”)로만 검사된다.
    실제로 그 거울을 전부 통과하면서 클라가 매니페스트를 통째로 거부하는 응답
    (`"min_version": ""`)이 나간 적이 있다 — 픽스처가 min_version 을 채워 둔 탓에
    그 조합이 한 번도 실행되지 않았다. 그래서 왕복을 상설 테스트로 박는다.

    반환: parse(body: str, arch: str) -> dict
          {"ok": bool, "version": str, "min_version": str, ...}
    """
    cxx = _find_cxx()
    if cxx is None:
        # CI 에서는 조용히 건너뛰면 안 된다 — 컴파일러가 없는 잡은 이 테스트를
        # '통과' 로 보고하면서 아무것도 검증하지 않는다.
        if os.environ.get("SHERBET_REQUIRE_CXX"):
            pytest.fail("C++ 컴파일러가 없어 클라 파서 왕복 테스트를 돌리지 못했습니다")
        pytest.skip("C++ 컴파일러 없음 — 클라 파서 왕복 테스트를 건너뜁니다")

    src = os.path.join(REPO_ROOT, "tools", "sherbet_manifest_check.cpp")
    exe = str(tmp_path_factory.mktemp("cxx") / "sherbet_manifest_check")
    subprocess.run(
        [cxx, "-std=c++17", "-Wall", "-I", os.path.join(REPO_ROOT, "source"), src, "-o", exe],
        check=True, capture_output=True)

    def parse(body: str, arch: str = "x64") -> dict:
        r = subprocess.run([exe, arch], input=body.encode("utf-8"),
                           stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        # 종료코드 0 = '판정을 마쳤다'. 거부는 stdout 의 ok=0 로만 신호한다 —
        # 하네스가 못 돌아간 것과 클라가 거부한 것을 구별하지 못하면 테스트가 조용히 통과한다.
        assert r.returncode == 0, f"하네스 실패: rc={r.returncode} {r.stderr.decode()}"
        out: dict = {}
        for line in r.stdout.decode("utf-8").splitlines():
            k, _, v = line.partition("=")
            out[k] = v
        out["ok"] = out.get("ok") == "1"
        return out

    return parse


@pytest.fixture
def settings() -> Settings:
    return Settings(
        discord_client_id="test-cid",
        discord_client_secret="test-secret",
        discord_bot_token="test-bot-token",
        discord_guild_id="guild-1",
        discord_redirect_uri="https://test/auth/callback",
        session_secret="unit-test-session-secret",
        role_buyer_id="role-buyer",
        token_ttl_seconds=86400,
    )
