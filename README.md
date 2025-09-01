# JeongRyeol's Custom ReShade (Fork)

이 저장소는 공식 ReShade(후처리 인젝터)를 포크하여 **UI/사용자 경험(UX) 개선**과 **로컬 환경에 특화된 기능**을 실험/적용한 브랜치들을 모은 것입니다.  
원본: crosire/reshade (BSD-3-Clause)

## 내가 한 핵심 변경점
## 📌 Changelog
- [CHANGELOG.md](./CHANGELOG.md) 에서 확인할 수 있습니다.
  
- **한국어 로컬라이징 & 온보딩 메시지 커스텀**
  - 스플래시/ABOUT 화면 한국어화, 디스코드 안내 등 커뮤니티 중심 메시지로 교체
  - 번역 래퍼 제거로 커스텀 문구가 빈 화면으로 보이던 문제 해결
- **UI 테마 개선**
  - 노란/오렌지 포인트의 게이밍 테마 추가
  - 둥근 모서리, 여백/간격 정리로 가독성 향상
- **그래디언트 애니메이션(옵션)**
  - Overlay & Styling에 토글 제공
  - HSV 컬러휠 기반 배경/액티브 보색 애니메이션으로 대비 보강
- **HWID 검증(실험 브랜치)**
  - CPU/디스크 시리얼 기반 HWID 체크, `%APPDATA%/ReShade/license.txt` 지원

## 브랜치별 특징
- `gradient`: 그래디언트 UI 애니메이션 + 현대적 스타일 정리
- `custom-color`: 한국어 메시지/테마 커스텀 중심
- `hwid-add`: HWID 라이선스 검증(실험)
- `recursive_addon_api`, `dxc`: 업스트림 기능 트래킹

## 빌드
- Visual Studio 2017+ 필요. 원본 가이드와 동일(서브모듈 포함 클론 후 솔루션 빌드).

## 라이선스
- BSD-3-Clause(원저작권 고지 유지). 일부 파일은 파일 헤더 표기대로 MIT 병행.
- 본 저장소는 **교육/개인 커스터마이징 목적**으로 운영되며, 상용 배포/재배포 시 원저작권 고지를 반드시 포함합니다.
  
ReShade
=======

This is a generic post-processing injector for games and video software. It exposes an automated way to access both frame color and depth information and a custom shader language called ReShade FX to write effects like ambient occlusion, depth of field, color correction and more which work everywhere.

ReShade can optionally load **add-ons**, DLLs that make use of the ReShade API to extend functionality of both ReShade and/or the application ReShade is being applied to. To get started on how to write your own add-on, check out the [API reference](REFERENCE.md).

The ReShade FX shader compiler contained in this repository is standalone, so can be integrated into other projects as well. Simply add all `source/effect_*.*` files to your project and use it similar to the [fxc example](tools/fxc.cpp).

## Building

You'll need Visual Studio 2017 or higher to build ReShade and Python for the `gl3w` dependency.

1. Clone this repository including all Git submodules\
```git clone --recurse-submodules https://github.com/crosire/reshade```
2. Open the Visual Studio solution
3. Select either the `32-bit` or `64-bit` target platform and build the solution.\
   This will build ReShade and all dependencies. To build the setup tool, first build the `Release` configuration for both `32-bit` and `64-bit` targets and only afterwards build the `Release Setup` configuration (does not matter which target is selected then).

A quick overview of what some of the source code files contain:

|File                                                                  |Description                                                            |
|----------------------------------------------------------------------|-----------------------------------------------------------------------|
|[dll_log.cpp](source/dll_log.cpp)                                     |Simple file logger implementation                                      |
|[dll_main.cpp](source/dll_main.cpp)                                   |Main entry point (and optional test application)                       |
|[dll_resources.cpp](source/dll_resources.cpp)                         |Access to DLL resource data (e.g. built-in shaders)                    |
|[effect_lexer.cpp](source/effect_lexer.cpp)                           |Lexical analyzer for C-like languages                                  |
|[effect_parser_stmt.cpp](source/effect_parser_stmt.cpp)               |Parser for the ReShade FX shader language                              |
|[effect_preprocessor.cpp](source/effect_preprocessor.cpp)             |C-like preprocessor implementation                                     |
|[hook.cpp](source/hook.cpp)                                           |Wrapper around MinHook which tracks associated function pointers       |
|[hook_manager.cpp](source/hook_manager.cpp)                           |Automatic hook installation based on DLL exports                       |
|[input.cpp](source/input.cpp)                                         |Keyboard and mouse input management and window message queue hooks     |
|[runtime.cpp](source/runtime.cpp)                                     |Core ReShade runtime including effect and preset management            |
|[runtime_gui.cpp](source/runtime_gui.cpp)                             |Overlay rendering and everything user interface related                |

## Contributing

Any contributions to the project are welcomed, it's recommended to use GitHub [pull requests](https://help.github.com/articles/using-pull-requests/).

## Feedback and Support

See the [ReShade Forum](https://reshade.me/forum) and [Discord](https://discord.gg/PrwndfH) server for feedback and support.

## License

ReShade is licensed under the terms of the [BSD 3-clause license](LICENSE.md).\
Some source code files are dual-licensed and are also available under the terms of the MIT license, when stated as such at the top of those files.
