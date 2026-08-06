#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Sherbet 업데이트 매니페스트 발행 — 릴리스 때마다 쓰는 스크립트.

쓰는 법:
    1. GitHub 릴리스가 **성공**한 뒤에 실행한다(에셋이 올라와 있어야 한다).
    2. 아래 VERSION 과 NOTES 를 이번 릴리스에 맞게 고친다.
    3. python3 publish_manifest.py
    4. 끝나면 반드시 외부에서 curl 로 확인한다(아래 참고).

⚠️ sha256 은 **릴리스에서 실제로 내려받은 바이트**로 여기서 직접 계산한다.
   CI 출력을 복붙하면 릴리스 이후 에셋이 바뀐 것을 영원히 못 잡는다.
⚠️ min_version 은 쓰지 않으므로 **키 자체를 넣지 않는다**. "" 를 넣으면 클라가
   매니페스트 전체를 거부해서 아무도 업데이트를 못 받는다(실제로 만들었던 버그).
⚠️ size 는 디스크에 숫자로 적는다 — 서버 app/update.py 가 str() 로 문자열화해 내보낸다.
   클라의 json_string 은 따옴표 문자열만 읽으므로, **응답**이 문자열이면 정상이다.

발행 후 확인(둘 다 '정상' 이어야 한다):
    curl -s "https://wonryeol.asuscomm.com/sherbet-auth/update/manifest?arch=x64&cur=<이전버전>"
    curl -s "https://wonryeol.asuscomm.com/sherbet-auth/update/manifest?arch=x86&cur=<이전버전>"
  확인 항목 4개: version 갱신 / size 가 문자열 / allow_downgrade 가 진짜 불리언 /
  min_version 키 없음. (arch 값은 x64·x86 이고, 공개 경로엔 /sherbet-auth 접두사가 붙는다.)
"""
import hashlib
import json
import subprocess
import sys
import urllib.request

# ── 릴리스마다 여기 두 개만 고친다 ──────────────────────────────────────────
VERSION = "1.8.2"
# ⚠️ 알림 문구에 「 」 를 쓰지 말 것. 폰트 아틀라스에 없어서 인게임에 **물음표로 찍힌다**
# (1.6.9 에서 실제로 겪음: "새 ?사격 훈련? 탭"). 코드베이스 관례대로 '작은따옴표' 를 쓴다.
# · — 는 폰트에 있으니 써도 된다.
NOTES = (
    "수평 방식에서 쏠수록 표적이 위로 올라가던 문제를 고쳤어요\n"
    "표적이 전체적으로 작아지고, 쏠수록 작아지는 게 눈에 보이게 다듬었어요\n"
    "자유 방식의 위아래 폭을 더 줄였어요"
)
# ───────────────────────────────────────────────────────────────────────────

TAG = f"sherbet-{VERSION}"
BASE = f"https://github.com/Jeong-Ryeol/reshade/releases/download/{TAG}"

# ⚠️ 홈서버는 **내부 IP 직결**. DDNS(wonryeol.asuscomm.com)로 파일을 보내면
#    헤어핀 NAT 때문에 전송이 중간에 잘린다(6MB 가 255KB 에서 끊긴 적 있음).
SSH = [
    "ssh", "-o", "StrictHostKeyChecking=no", "-o", "UserKnownHostsFile=/dev/null",
    "-o", "ConnectTimeout=10", "wonryeol5336-server@192.168.50.95",
]


def fetch(name: str):
    url = f"{BASE}/{name}"
    print(f"  내려받는 중: {url}")
    with urllib.request.urlopen(url, timeout=180) as r:
        data = r.read()
    return url, data, hashlib.sha256(data).hexdigest(), len(data)


def main() -> int:
    builds = {}
    for arch, name in (("x64", "ReShade64.dll"), ("x86", "ReShade32.dll")):
        url, data, sha, size = fetch(name)
        # PE 인지 확인 — 404 HTML 을 DLL 로 배포하는 사고를 여기서 막는다.
        if data[:2] != b"MZ":
            print(f"  X {name}: PE 헤더(MZ)가 아닙니다. 릴리스 에셋이 아직 없거나 잘못됐습니다.")
            return 1
        # 바이너리 안에 이번 버전 문자열이 있는지 — 태그와 빌드가 어긋나는 사고 방지.
        if VERSION.encode() not in data:
            print(f"  X {name}: 바이너리 안에 버전 문자열 {VERSION} 이 없습니다.")
            return 1
        print(f"  OK {name}: size={size} sha256={sha}")
        builds[arch] = {"url": url, "size": size, "sha256": sha}

    doc = {
        "schema": 1,
        "version": VERSION,
        "allow_downgrade": False,
        "notice": "",
        "notes": NOTES,
        "builds": builds,
    }
    body = json.dumps(doc, ensure_ascii=False, indent=2) + "\n"
    print("\n생성된 매니페스트:")
    print(body)

    # 백업 → 원자적 교체(mv). 반쯤 쓰인 파일이 서비스에 노출되지 않게.
    r = subprocess.run(
        SSH + ["cp ~/sherbet-auth/content/update.json "
               "~/sherbet-auth/content/update.json.bak-$(date +%Y%m%d-%H%M%S) "
               "&& cat > ~/sherbet-auth/content/update.json.new "
               "&& mv ~/sherbet-auth/content/update.json.new ~/sherbet-auth/content/update.json "
               "&& echo WROTE"],
        input=body.encode("utf-8"), capture_output=True)
    out = (r.stdout + r.stderr).decode("utf-8", "replace").strip()
    print(out)
    if "WROTE" not in out:
        print("  X 서버 쓰기 실패")
        return 1
    print("  OK 홈서버 update.json 갱신 완료 (mtime 캐시라 서비스 재시작 불필요)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
