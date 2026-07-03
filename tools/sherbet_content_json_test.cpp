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

int main()
{
    test_parse_items();
    test_safe_basename();
    test_download_targets();
    std::puts("sherbet_content_json_test: ALL PASS");
    return 0;
}
