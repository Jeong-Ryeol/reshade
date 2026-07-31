/*
 * Copyright (C) 2026 정렬 (Jeong-Ryeol)
 * SPDX-License-Identifier: BSD-3-Clause
 */
// host 테스트: clang++ -std=c++17 -Ideps/imgui tools/sherbet_content_json_test.cpp -o /tmp/tc && /tmp/tc
#include "../source/sherbet_content_json.hpp"
#include <cassert>
#include <cstdio>

using namespace sherbet;

static const char *kBody = R"({"themes":[{"id":"t1"}],
"presets":[
  {"id":"p-1","filename":"Strawberry.ini","display_name":"딸기"},
  {"id":"p-2","filename":"noname.ini"}
],
"effects":[
  {"id":"e-1","filename":"Grade.fx","display_name":"Grade"}
]})";

static void test_parse_items()
{
    auto ps = parse_content_items(kBody, "presets");
    assert(ps.size() == 2);
    assert(ps[0].id == "p-1" && ps[0].filename == "Strawberry.ini" && ps[0].display_name == "딸기");
    assert(ps[1].display_name == "noname.ini"); // display_name 없으면 filename
    auto es = parse_content_items(kBody, "effects");
    assert(es.size() == 1 && es[0].id == "e-1" && es[0].filename == "Grade.fx");
    assert(parse_content_items("{}", "presets").empty());
    assert(parse_content_items(kBody, "nope").empty());
}

static void test_safe_basename()
{
    assert(safe_basename("Strawberry.ini") == "Strawberry.ini");
    assert(safe_basename("a/b/c.fx") == "c.fx");
    assert(safe_basename("a\\b\\c.fx") == "c.fx");
    assert(safe_basename("../evil.ini").empty());     // .. 거부
    assert(safe_basename("dir/../evil").empty());      // 경로 내 .. 거부
    assert(safe_basename("").empty());
}

static void test_download_targets()
{
    auto t = content_download_targets(kBody, "C:/g/Sherbet-Presets", "C:/g/Sherbet-Fx");
    // presets 2 + effects 1 = 3
    assert(t.size() == 3);
    assert(t[0].id == "p-1" && t[0].dest_path == "C:/g/Sherbet-Presets/Strawberry.ini");
    assert(t[1].id == "p-2" && t[1].dest_path == "C:/g/Sherbet-Presets/noname.ini");
    assert(t[2].id == "e-1" && t[2].dest_path == "C:/g/Sherbet-Fx/Grade.fx");
}

// 잠긴 항목 진열 — 서버 file_items_with_lock 이 내려보내는 형태를 그대로 먹인다.
// 계약: 잠긴 항목에는 id 가 없고 unlocked 가 **진짜 불리언** false 다.
static void test_locked_items_are_displayed_but_never_downloaded()
{
    static const char *const kLockBody =
        "{\"presets\":["
        "{\"filename\":\"Free.ini\",\"display_name\":\"\\uc21c\\uc815\",\"id\":\"p-free\",\"unlocked\":true},"
        "{\"filename\":\"Pretty.ini\",\"display_name\":\"Pretty\",\"unlocked\":false},"
        "{\"filename\":\"Mongsil.ini\",\"display_name\":\"\\ubabd\\uc2e4\",\"unlocked\":false}"
        "],\"effects\":[]}";

    const auto items = parse_content_items(kLockBody, "presets");
    // 잠긴 것도 **목록에는 남는다**(진열용). 안 그러면 안 산 사람 화면에 상품이 존재하지 않는다.
    assert(items.size() == 3);
    assert(items[0].unlocked && items[0].id == "p-free");
    assert(!items[1].unlocked && items[1].id.empty() && items[1].display_name == "Pretty");
    assert(!items[2].unlocked && items[2].id.empty());

    // 그러나 **다운로드 대상에는 절대 들어가지 않는다.** 여기가 뚫리면 잠긴 상품 수만큼
    // 403 요청이 나가고 그때마다 서버가 디스코드에 역할을 물어본다.
    const auto t = content_download_targets(kLockBody, "C:/g/P", "C:/g/F");
    assert(t.size() == 1);
    assert(t[0].id == "p-free");
}

// 하위호환: 구버전 서버는 unlocked 키를 안 보낸다 → 전부 열린 것으로 본다(기존 동작 유지).
static void test_missing_unlocked_defaults_open()
{
    static const char *const kOldBody =
        "{\"presets\":[{\"filename\":\"A.ini\",\"id\":\"a\"}],\"effects\":[]}";
    const auto items = parse_content_items(kOldBody, "presets");
    assert(items.size() == 1 && items[0].unlocked);
    assert(content_download_targets(kOldBody, "P", "F").size() == 1);
}

// 열린 항목인데 id 가 없으면 받을 방법이 없다 — 예전처럼 버린다.
static void test_unlocked_without_id_is_dropped()
{
    static const char *const kBad =
        "{\"presets\":[{\"filename\":\"A.ini\",\"unlocked\":true}],\"effects\":[]}";
    assert(parse_content_items(kBad, "presets").empty());
}

int main()
{
    test_parse_items();
    test_safe_basename();
    test_download_targets();
    test_locked_items_are_displayed_but_never_downloaded();
    test_missing_unlocked_defaults_open();
    test_unlocked_without_id_is_dropped();
    std::puts("sherbet_content_json_test: ALL PASS");
    return 0;
}
