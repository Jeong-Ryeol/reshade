# Sherbet 관리자 가이드 — 키 발급 & 전체 사용법

정렬(오너)이 Sherbet을 판매·운영할 때 보는 실무 설명서.
"코드가 어떻게 짜였나"가 아니라 **"손님이 사면 나는 뭘 누르나"**를 정리한다.

---

## 0. 30초 요약 — 이게 어떻게 돌아가는가

- **DLL은 모두 똑같은 하나(공용 DLL).** 사람마다 따로 빌드 안 한다.
- 손님이 게임에서 Sherbet을 켜면 **로그인 창**이 뜨고, **디스코드로 로그인**한다.
- 서버가 그 사람의 **디스코드 역할**을 확인해서:
  - `구매자` 역할 있음 → 잠금 해제(이펙트 사용 가능).
  - 로그인한 **디스코드 표시이름**을 DLL이 받아서 화면에 "○○님을 위한 커스텀"으로 각인.
  - 그 사람이 가진 **상품 역할**에 맞는 테마·프리셋·fx만 서버가 내려준다.
- 그래서 **"키"라는 문자열은 없다.** 손님의 **디스코드 계정 + 역할이 곧 키**다.
  키 발급 = **디스코드에서 역할을 부여하는 것**.

---

## 1. "키 발급" = 판매했을 때 하는 일 (제일 중요)

손님이 결제하면 순서는 이렇다:

1. 손님을 **디스코드 서버에 들어오게** 한다(초대 링크).
2. 디스코드에서 명령어 한 줄:
   ```
   /grant member:@손님 product:상품키
   ```
   - `구매자` 역할 **+** 그 상품의 역할이 자동으로 붙는다.
   - 기본 판매(테마/기본 프리셋만)라면 `product:base` 처럼 기본 상품 하나만 주면 된다.
3. 끝. 손님이 게임에서 로그인하면 바로 잠금 해제되고, 산 콘텐츠가 마켓의
   **"내 전용 불러오기"**에 뜬다.

> 환불·기간 만료 시: `/revoke member:@손님 product:상품키` 로 회수.

**포인트:** DLL을 다시 주거나 파일을 보내줄 필요가 전혀 없다. 역할만 만지면 된다.
그래서 예전처럼 "오프라인 코드가 유출되는" 문제가 없다.

---

## 2. 최초 1회만 하는 세팅 (이미 대부분 되어 있음)

| 할 일 | 명령/방법 | 상태 |
|---|---|---|
| 봇 역할을 `구매자`·모든 상품 역할 **위로** 드래그 | 서버 설정 → 역할에서 `JeongRyeol Ticket` 위로 | ⚠️ **이거 안 하면 `/grant` 실패** |
| 로그 채널 지정 | `/setlog channel:#로그` | 선택 |
| 구매자 역할 확인 | `/setbuyer role:@구매자` (이미 프리시드됨) | 됨 |
| 문의 티켓 패널 | `/panel` (원하는 채널에서 1회) | 선택 |

⚠️ **봇 역할 계층**만은 반드시 확인. 디스코드는 "자기보다 낮은 역할만 부여 가능"이라,
봇 역할이 `구매자`보다 아래에 있으면 `/grant`가 거부된다.

---

## 3. 새 상품 만들기 (실사·PVP fx 판매 세팅)

지금은 **기본 프리셋만 배포되어 있고 fx는 하나도 없다.** 실사/PVP를 팔려면 상품마다:

### 3-1. 디스코드에서 역할 만들고 봇에 등록
1. 서버 설정 → 역할에서 역할 생성 (예: `sherbet-실사`, `sherbet-pvp`). 위치는 봇 역할 아래.
2. 역할 우클릭 → **ID 복사**(개발자 모드 켜야 보임).
3. 디스코드에서:
   ```
   /product-add key:silsa   name:실사 전용 fx  role:@sherbet-실사
   /product-add key:pvp     name:PVP 전용 fx   role:@sherbet-pvp
   ```
   - 한 상품에 여러 역할을 묶고 싶으면 **같은 key로 반복**하면 역할이 누적된다.
4. `/product-list` 로 확인.

### 3-2. 서버에 fx 파일 올리기 (재빌드 불필요)
홈서버 `~/sherbet-auth/content/` 에서:

1. fx 파일을 `files/<id>` 로 업로드. `<id>`는 확장자 포함 아무 고유 이름(예: `silsa-v1.fx`).
   ```
   scp 실사전용.fx  wonryeol5336-server@wonryeol.asuscomm.com:~/sherbet-auth/content/files/silsa-v1.fx
   scp PVP전용.fx   wonryeol5336-server@wonryeol.asuscomm.com:~/sherbet-auth/content/files/pvp-v1.fx
   ```
2. `effects.json` 에 항목 추가 (배열):
   ```json
   [
     { "id": "silsa-v1.fx", "filename": "실사전용.fx", "display_name": "실사 전용 fx", "role": "여기에_sherbet-실사_역할ID" },
     { "id": "pvp-v1.fx",   "filename": "PVP전용.fx",  "display_name": "PVP 전용 fx",  "role": "여기에_sherbet-pvp_역할ID" }
   ]
   ```
   - `id` = 서버 디스크 파일명(위 scp 이름과 동일).
   - `filename` = 손님 클라가 `Sherbet-Fx/` 에 저장할 이름.
   - `role` = **디스코드 숫자 역할 ID 문자열**(따옴표 필수). 이름 쓰면 항상 거부(fail-closed).
     `null` 이면 전원 무료.
3. 서버 **재시작 불필요**(파일 수정시각 캐시라 자동 반영).

이제 `/grant member:@손님 product:silsa` 하면, 그 손님만 실사 fx가 마켓에 뜨고
다운로드→`reload` 로 적용된다. 역할 없는 사람은 파일 자체를 못 받는다(서버가 매 요청 역할 재확인).

---

## 4. 콘텐츠 업로드 요약 (테마 / 프리셋 / fx)

`~/sherbet-auth/content/` 안의 세 매니페스트 + `files/` 폴더로 끝난다.

| 종류 | 매니페스트 | 파일 위치 | 항목 스키마 |
|---|---|---|---|
| 테마 | `themes.json` | (파일 없음, 색상값만) | `id / display_name / colors(12색 #rrggbbaa) / particle / hue_cycle / role` |
| 프리셋(.ini) | `presets.json` | `files/<id>` | `id / filename / display_name / role` |
| fx(셰이더) | `effects.json` | `files/<id>` | `id / filename / display_name / role` |

- `role`: 디스코드 숫자 역할 ID 문자열, `null`=무료.
- 업로드 후 재빌드·재시작 없음. 손님은 로그인 후 마켓 "내 전용 불러오기"로 받음.
- **현재 배포 상태:** `presets.json` = 기본 프리셋 `정렬-기본.ini`(role null, 전 구매자) 1개.
  `effects.json` = 비어 있음(fx 아직 없음). `themes.json` = Aurora 등.

> 참고: 기본 프리셋(`정렬-기본.ini`)의 활성 Technique는 `실사전용.fx` 를 참조한다.
> 실사 fx를 안 산 사람이 이 프리셋을 적용하면 그 항목만 "이펙트 없음"으로 표시될 뿐
> 에러/크래시는 아니다. 실사 fx는 위 3번대로 상품화하면 산 사람에게만 적용된다.

---

## 5. 판매용 DLL 빌드 (윈도우/CI, Mac 불가)

온라인 인증이 켜진 DLL을 만들려면 GitHub Actions `build` 워크플로를 수동 실행:

```
gh workflow run build --ref sherbet-base -f online_auth=true
```

- `online_auth=true` 여야 로그인 게이트가 켜진 판매 빌드가 나온다.
- 산출물(ReShade64.dll 등)을 손님에게 배포. 모두 같은 파일이면 된다(공용 DLL).
- 이름 각인은 빌드가 아니라 **로그인한 디스코드 표시이름**으로 자동 처리되므로,
  사람마다 빌드 인자를 바꿀 필요 없다.

---

## 6. 판매 직전 최종 점검

- [ ] 봇 역할이 `구매자`·상품 역할보다 위인가 (`/grant` 성공 조건)
- [ ] `/product-list` 에 팔 상품이 다 있는가
- [ ] 팔 fx가 `files/` 에 있고 `effects.json` role ID가 정확한가
- [ ] `online_auth=true` 로 빌드한 DLL인가
- [ ] 테스트 계정으로 로그인 → 잠금 해제 → 이름 각인 → 마켓 다운로드까지 되는가

---

*관련: 남은 운영 작업은 `docs/sherbet-운영-체크리스트.md`, 설계 전문은
`docs/superpowers/specs/2026-07-03-sherbet-online-auth-design.md`.*
