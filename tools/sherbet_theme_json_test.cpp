/*
 * Copyright (C) 2026 정렬 (Jeong-Ryeol)
 * SPDX-License-Identifier: BSD-3-Clause
 */
// host 테스트: clang++ -std=c++17 -Ideps/imgui tools/sherbet_theme_json_test.cpp -o /tmp/tj && /tmp/tj
#include "../source/sherbet_theme_json.hpp"
#include <cassert>
#include <cstdio>

using namespace sherbet;

static void test_hex()
{
    ImU32 c = 0;
    assert(parse_hex_color("#5df0c0ff", c));
    assert(c == IM_COL32(0x5d, 0xf0, 0xc0, 0xff));
    assert(parse_hex_color("#00000000", c) && c == IM_COL32(0, 0, 0, 0));
    ImU32 keep = 123;
    assert(!parse_hex_color("5df0c0ff", keep) && keep == 123); // # 없음
    assert(!parse_hex_color("#5df0c0", keep) && keep == 123);  // 길이 부족
    assert(!parse_hex_color("#zzzzzzzz", keep) && keep == 123); // 비16진
}

static void test_particle()
{
    assert(parse_particle("heart") == particle::heart);
    assert(parse_particle("leaf") == particle::leaf);
    assert(parse_particle("petal") == particle::petal);
    assert(parse_particle("spark") == particle::spark);
    assert(parse_particle("nonsense") == particle::spark); // 폴백
}

static const char *kManifest = R"({"themes":[
  {"id":"aurora","display_name":"Aurora","particle":"spark","hue_cycle":true,
   "colors":{"bg0":"#0a1f1aff","bg1":"#0f2e24ff","bg2":"#123a2cff",
     "panel":"#122e26b8","panel_alt":"#183a2fd9","chip":"#134233ff","border":"#5df0c040",
     "text":"#eafff6ff","text_dim":"#8fbfaeff","accent":"#5df0c0ff","accent2":"#a7ffe3ff",
     "glow":"#5df0c073"}},
  {"id":"mint","role":"gone","display_name":"Mint"}
]})";

static void test_manifest()
{
    auto v = parse_themes_manifest(kManifest);
    assert(v.size() == 2);
    assert(v[0].ok);
    assert(v[0].id == "aurora");
    assert(v[0].display_name == "Aurora");
    assert(v[0].particle_shape == particle::spark);
    assert(v[0].hue_cycle == true);
    assert(v[0].accent == IM_COL32(0x5d, 0xf0, 0xc0, 0xff));
    assert(v[0].glow == IM_COL32(0x5d, 0xf0, 0xc0, 0x73));
    // 색 없는 내장 항목 → ok=false(색 불완전), 그러나 id 는 읽힘
    assert(!v[1].ok);
    assert(v[1].id == "mint");
    // unlocked 필드 없으면 하위호환으로 true(해제) 간주
    assert(v[0].unlocked == true);
    // themes 없음 → 빈 벡터
    assert(parse_themes_manifest("{}").empty());
    assert(parse_themes_manifest("garbage").empty());
}

static void test_unlocked()
{
    // 서버가 잠긴 테마도 진열용으로 내려줄 때 unlocked:false 로 표시
    const char *m = R"({"themes":[
      {"id":"locked1","display_name":"Locked","unlocked":false,
       "colors":{"bg0":"#000000ff","bg1":"#000000ff","bg2":"#000000ff",
         "panel":"#000000ff","panel_alt":"#000000ff","chip":"#000000ff","border":"#000000ff",
         "text":"#ffffffff","text_dim":"#888888ff","accent":"#5ea6ffff","accent2":"#a9d2ffff",
         "glow":"#5ea6ff73"}},
      {"id":"owned1","display_name":"Owned","unlocked":true,
       "colors":{"bg0":"#000000ff","bg1":"#000000ff","bg2":"#000000ff",
         "panel":"#000000ff","panel_alt":"#000000ff","chip":"#000000ff","border":"#000000ff",
         "text":"#ffffffff","text_dim":"#888888ff","accent":"#5ea6ffff","accent2":"#a9d2ffff",
         "glow":"#5ea6ff73"}}
    ]})";
    auto v = parse_themes_manifest(m);
    assert(v.size() == 2);
    assert(v[0].id == "locked1" && v[0].ok && v[0].unlocked == false); // 잠김
    assert(v[1].id == "owned1"  && v[1].ok && v[1].unlocked == true);  // 해제
}

int main()
{
    test_hex();
    test_particle();
    test_manifest();
    test_unlocked();
    std::puts("sherbet_theme_json_test: ALL PASS");
    return 0;
}
