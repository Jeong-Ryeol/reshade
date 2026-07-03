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
    // themes 없음 → 빈 벡터
    assert(parse_themes_manifest("{}").empty());
    assert(parse_themes_manifest("garbage").empty());
}

int main()
{
    test_hex();
    test_particle();
    test_manifest();
    std::puts("sherbet_theme_json_test: ALL PASS");
    return 0;
}
