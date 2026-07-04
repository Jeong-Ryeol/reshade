#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Sherbet 관리자 도구 — 테마 / 프리셋 / fx 를 홈서버에 쉽게 추가·관리한다.

사용법:
    python3 tools/sherbet_admin.py

전제:
    - 맥에서 홈서버로 SSH 키 접속이 이미 되는 상태(BatchMode).
    - 서버의 sherbet-auth 콘텐츠 폴더: ~/sherbet-auth/content/
      (themes.json / presets.json / effects.json / files/)

메뉴에서 번호만 고르면 되고, 테마는 '포인트 색' 하나만 넣으면
12색 팔레트를 자동 생성한다. 서버에 바로 배포되고 재시작 불필요(mtime 캐시).
비밀·토큰은 이 파일에 없다(서버 .env 에만).
"""

import colorsys
import json
import os
import re
import subprocess
import sys
import tempfile

# ------------------------------------------------------------------ 설정
SSH_HOST = "wonryeol5336-server@wonryeol.asuscomm.com"
CONTENT = "~/sherbet-auth/content"
THEMES = f"{CONTENT}/themes.json"
PRESETS = f"{CONTENT}/presets.json"
EFFECTS = f"{CONTENT}/effects.json"
FILES = f"{CONTENT}/files"

# DLL 에 이미 내장된 테마 id — 새 동적 테마 id 로 쓰면 안 된다(색 대신 잠금해제만 됨)
BUILTIN_THEME_IDS = {"mint", "peach", "pink", "rainbow", "lavender", "noir", "strawberry"}
PARTICLES = ["spark", "heart", "leaf", "petal"]


# ------------------------------------------------------------------ SSH/파일 입출력
def _ssh(args):
    return subprocess.run(
        ["ssh", "-o", "BatchMode=yes", "-o", "ConnectTimeout=10", SSH_HOST] + args,
        capture_output=True, text=True,
    )


def remote_read(path):
    r = _ssh([f"cat {path} 2>/dev/null || echo __MISSING__"])
    if r.returncode != 0:
        die(f"서버 접속 실패: {r.stderr.strip() or 'SSH 오류'}")
    return r.stdout


def load_manifest(path):
    raw = remote_read(path)
    if raw.strip() == "__MISSING__" or not raw.strip():
        return []
    try:
        data = json.loads(raw)
        return data if isinstance(data, list) else []
    except json.JSONDecodeError:
        die(f"{path} 가 올바른 JSON 이 아닙니다. 서버 파일을 확인하세요.")


def save_manifest(path, data):
    text = json.dumps(data, ensure_ascii=False, indent=2) + "\n"
    with tempfile.NamedTemporaryFile("w", suffix=".json", delete=False, encoding="utf-8") as f:
        f.write(text)
        tmp = f.name
    try:
        r = subprocess.run(
            ["scp", "-o", "BatchMode=yes", tmp, f"{SSH_HOST}:{path}"],
            capture_output=True, text=True,
        )
        if r.returncode != 0:
            die(f"업로드 실패: {r.stderr.strip()}")
    finally:
        os.unlink(tmp)


def upload_file(local_path, remote_name):
    r = subprocess.run(
        ["scp", "-o", "BatchMode=yes", local_path, f"{SSH_HOST}:{FILES}/{remote_name}"],
        capture_output=True, text=True,
    )
    if r.returncode != 0:
        die(f"파일 업로드 실패: {r.stderr.strip()}")


def service_ok():
    r = _ssh(["export XDG_RUNTIME_DIR=/run/user/$(id -u); "
              "systemctl --user is-active sherbet-auth.service; "
              "curl -s http://127.0.0.1:8010/health"])
    return r.stdout.strip()


# ------------------------------------------------------------------ 색 유틸 / 팔레트 생성
def hex_to_rgb(h):
    h = h.lstrip("#")
    return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4))


def rgb_hex(r, g, b, a=255):
    c = lambda x: max(0, min(255, int(round(x))))
    return f"#{c(r):02x}{c(g):02x}{c(b):02x}{c(a):02x}"


def valid_hex(s):
    return bool(re.fullmatch(r"#?[0-9a-fA-F]{6}", s.strip()))


def gen_palette(accent_hex):
    """포인트 색(accent) 하나로 다크 테마 12색을 만든다. 밝은 테마는 수동 편집 권장."""
    r, g, b = hex_to_rgb(accent_hex)
    h, s, v = colorsys.rgb_to_hsv(r / 255, g / 255, b / 255)

    def mk(sat, val, a=255):
        rr, gg, bb = colorsys.hsv_to_rgb(h, sat, val)
        return rgb_hex(rr * 255, gg * 255, bb * 255, a)

    bg_s = min(s, 0.5) * 0.7  # 배경은 포인트 색조를 살짝만
    return {
        "bg0": mk(bg_s, 0.045), "bg1": mk(bg_s, 0.075), "bg2": mk(bg_s, 0.11),
        "panel": mk(bg_s, 0.09, 0xB8), "panel_alt": mk(bg_s, 0.13, 0xD9),
        "chip": mk(bg_s, 0.16), "border": rgb_hex(r, g, b, 0x40),
        "text": mk(min(s, 0.15), 0.96), "text_dim": mk(min(s, 0.30), 0.62),
        "accent": rgb_hex(r, g, b, 255),
        "accent2": mk(s * 0.6, min(1.0, v * 1.15 + 0.10)),
        "glow": rgb_hex(r, g, b, 0x73),
    }


# ------------------------------------------------------------------ 입력 헬퍼
def die(msg):
    print(f"\n❌ {msg}")
    sys.exit(1)


def ask(prompt, default=None, required=True):
    suffix = f" [{default}]" if default else ""
    while True:
        val = input(f"{prompt}{suffix}: ").strip()
        if not val and default is not None:
            return default
        if not val and not required:
            return ""
        if val:
            return val
        print("  값을 입력해 주세요.")


def ask_hex(prompt):
    while True:
        v = ask(prompt)
        if valid_hex(v):
            return "#" + v.lstrip("#").lower()
        print("  #rrggbb 형식 6자리 16진수로 입력하세요 (예: 5ea6ff).")


def ask_role(prompt):
    v = ask(prompt + " (숫자 ID, 없으면 엔터=전 구매자 무료)", required=False)
    if not v:
        if input("  정말 역할 없이(무료)로 둘까요? y/N: ").strip().lower() != "y":
            return ask_role(prompt)
        return None
    if not v.isdigit():
        print("  역할 ID 는 숫자여야 합니다.")
        return ask_role(prompt)
    return v


def choose(prompt, options):
    for i, o in enumerate(options, 1):
        print(f"  {i}) {o}")
    while True:
        v = input(f"{prompt} (1-{len(options)}): ").strip()
        if v.isdigit() and 1 <= int(v) <= len(options):
            return options[int(v) - 1]
        print("  번호로 골라주세요.")


def confirm(prompt):
    return input(f"{prompt} (y/N): ").strip().lower() == "y"


# ------------------------------------------------------------------ 기능: 테마 추가
def add_theme():
    print("\n── 테마 추가 ──")
    themes = load_manifest(THEMES)
    existing = {t.get("id") for t in themes}

    while True:
        tid = ask("테마 id (영문 소문자, 예: deepdark)").lower()
        if not re.fullmatch(r"[a-z0-9_-]+", tid):
            print("  영문 소문자/숫자/-/_ 만 쓸 수 있어요."); continue
        if tid in BUILTIN_THEME_IDS:
            print(f"  '{tid}' 은 내장 테마 id 예요. 새 테마는 다른 id 를 쓰세요."); continue
        if tid in existing:
            print(f"  '{tid}' 은 이미 있어요. 수정하려면 먼저 삭제하세요."); continue
        break

    name = ask("표시 이름 (예: Deep Dark)")
    accent = ask_hex("포인트 색 accent (#rrggbb)")
    print("  파티클(배경 입자) 종류:")
    particle = choose("  선택", PARTICLES)
    role = ask_role("잠금 역할")

    colors = gen_palette(accent)
    print("\n  \U0001f3a8 자동 생성된 팔레트:")
    for k, v in colors.items():
        print(f"    {k:10s} {v}")
    print(f"    particle   {particle}")
    print(f"    role       {role if role else '(무료)'}")

    if not confirm("\n이대로 배포할까요?"):
        print("취소했어요."); return

    themes.append({
        "id": tid, "display_name": name, "colors": colors,
        "particle": particle, "hue_cycle": False, "role": role,
    })
    save_manifest(THEMES, themes)
    print(f"\n✅ 테마 '{name}' 배포 완료.")
    if role:
        print(f"   다음: 디스코드에서 역할 만들고  /product-add key:{tid} name:{name} role:@역할")
    else:
        print("   역할 없이(무료) 배포됨 — 로그인한 전 구매자에게 보입니다.")


# ------------------------------------------------------------------ 기능: 프리셋/fx 추가
def add_file_content(kind):
    # kind: "preset" -> presets.json / Sherbet-Presets,  "fx" -> effects.json / Sherbet-Fx
    is_fx = kind == "fx"
    path = PRESETS if not is_fx else EFFECTS
    label = "fx(셰이더)" if is_fx else "프리셋(.ini)"
    print(f"\n── {label} 추가 ──")

    local = ask("올릴 로컬 파일 경로 (드래그해서 붙여넣기 가능)")
    local = local.strip().strip("'\"")
    local = os.path.expanduser(local)
    if not os.path.isfile(local):
        die(f"파일을 찾을 수 없어요: {local}")

    items = load_manifest(path)
    existing = {it.get("id") for it in items}

    base = os.path.basename(local)
    default_id = re.sub(r"[^A-Za-z0-9._-]", "-", base)  # 서버 디스크용 안전 id
    sid = ask("서버 저장 id (엔터=자동)", default=default_id)
    while sid in existing:
        sid = ask(f"'{sid}' 는 이미 있어요. 다른 id", default=default_id + "-v2")

    filename = ask("손님 클라에 저장될 파일명", default=base)
    display = ask("표시 이름", default=os.path.splitext(base)[0])
    role = ask_role("잠금 역할")

    print(f"\n  파일:   {local}\n  → 서버: {FILES}/{sid}\n  저장명: {filename}\n  역할:   {role if role else '(무료)'}")
    if not confirm("\n이대로 배포할까요?"):
        print("취소했어요."); return

    upload_file(local, sid)
    items.append({"id": sid, "filename": filename, "display_name": display, "role": role})
    save_manifest(path, items)
    print(f"\n✅ {label} '{display}' 배포 완료.")
    if role:
        key = re.sub(r"[^a-z0-9]", "", display.lower()) or sid
        print(f"   다음: 디스코드에서 역할 만들고  /product-add key:{key} name:{display} role:@역할")


# ------------------------------------------------------------------ 기능: 목록 / 삭제
def show_list():
    print("\n── 현재 등록 현황 ──")
    for title, path, is_theme in [("테마", THEMES, True), ("프리셋", PRESETS, False), ("fx", EFFECTS, False)]:
        items = load_manifest(path)
        print(f"\n[{title}] {len(items)}개")
        if not items:
            print("  (없음)")
        for it in items:
            role = it.get("role")
            tag = f"role:{role}" if role else "무료"
            if is_theme:
                kind = "내장" if it.get("id") in BUILTIN_THEME_IDS or "colors" not in it else "동적"
                print(f"  • {it.get('id'):14s} {it.get('display_name',''):18s} [{kind}] {tag}")
            else:
                print(f"  • {it.get('id'):22s} {it.get('display_name',''):18s} {tag}")


def remove_item():
    print("\n── 삭제 ──")
    kind = choose("무엇을 삭제할까요", ["테마", "프리셋", "fx"])
    path = {"테마": THEMES, "프리셋": PRESETS, "fx": EFFECTS}[kind]
    items = load_manifest(path)
    if not items:
        print("삭제할 게 없어요."); return
    for i, it in enumerate(items, 1):
        print(f"  {i}) {it.get('id')} — {it.get('display_name','')}")
    v = input(f"삭제할 번호 (1-{len(items)}, 취소=엔터): ").strip()
    if not v.isdigit() or not (1 <= int(v) <= len(items)):
        print("취소."); return
    it = items.pop(int(v) - 1)
    if not confirm(f"'{it.get('display_name')}' 삭제할까요?"):
        print("취소."); return
    save_manifest(path, items)
    print(f"✅ 삭제 완료. (서버 files/ 의 실제 파일은 남아있어요 — 필요하면 수동 삭제)")


# ------------------------------------------------------------------ main
def main():
    print("\U0001f367 Sherbet 관리자 도구")
    status = service_ok()
    if "active" not in status:
        print(f"⚠️  인증 서버 상태 확인 필요: {status or '응답 없음'}")
    else:
        print("서버: 정상 (active, health ok)")

    actions = {
        "1": ("테마 추가", add_theme),
        "2": ("프리셋(.ini) 추가", lambda: add_file_content("preset")),
        "3": ("fx(셰이더) 추가", lambda: add_file_content("fx")),
        "4": ("목록 보기", show_list),
        "5": ("삭제", remove_item),
        "0": ("종료", None),
    }
    while True:
        print("\n" + "=" * 40)
        for k, (label, _) in actions.items():
            print(f"  {k}. {label}")
        c = input("선택: ").strip()
        if c == "0":
            print("종료합니다. 🍧"); break
        if c in actions and actions[c][1]:
            try:
                actions[c][1]()
            except KeyboardInterrupt:
                print("\n(취소됨)")
        else:
            print("메뉴 번호를 골라주세요.")


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\n종료합니다.")
