# Overlord MWR VR

[English](README.md) | 한국어

![Overlord MWR VR](src/launcher-ui/public/images/launcher-background.jpg)

Overlord MWR VR은 *콜 오브 듀티: 모던 워페어 리마스터*(2016) 캠페인용 비공식 VR 모드입니다. *모던 워페어 2 캠페인 리마스터*용 VR 모드인 [Overlord](https://github.com/Aeka0/Overlord)를 MWR 실행 파일에 맞게 옮기고 미션에 맞춰 손봤습니다. 입체 화면, 머리와 손의 6DOF 추적, 직접 다루는 총기, 장비와 스토리 장면의 VR 조작을 지원합니다. 가능한 한 게임 원래의 무기, 탄약, 애니메이션, 미션 로직을 그대로 써서 원작의 연출을 유지합니다.

### **지금 버전은 Beta 1이며 한 사람이 관리합니다. 많은 기능을 관리자의 PC에서만 시험했고, 안정성은 낮다고 보셔야 합니다. 완성품이 아니라 시험판으로 생각해 주세요.**

### **버전이 바뀌면 세이브와 진행 상황이 손상될 수 있습니다. 일반(비 VR) 게임용 모드는 함께 쓸 수 없다고 보셔야 합니다.**

## 주요 기능

- **입체 화면과 머리 추적**: 캠페인 전체를 VR로 보고, 게임 메뉴와 HUD도 VR 안에 표시됩니다.
- **양손 총기 조작**: 컨트롤러로 총을 쥐고 조준하며, 보조 손잡이, 몸에 차는 홀스터, 바닥의 총 줍기를 지원합니다.
- **직접 하는 재장전과 장전**: 총마다 구조에 맞게 탄창, 노리쇠, 슬라이드, 장전 손잡이를 다룹니다. AK 계열과 드라구노프는 탄창 앞을 걸고 뒤로 젖혀 끼우는 AK식 결합 방식입니다.
- **총열 아래 유탄발사기**
  - M4 M203: 총열을 앞으로 밀고, 약실에 유탄을 넣고, 닫습니다.
  - AK GP25: 총구로 유탄을 넣습니다.
  - 유탄은 허리에서 꺼냅니다.
- **미션 장비**: 재블린·스팅거 록온 화면, C4 기폭기, 야간 투시경과 VR 총을 따라가는 조준 레이저, 투척물과 칼.
- **MWR 미션 맞춤 조작**
  - *One Shot, One Kill*: 바렛을 양각대에 거치하고, 정해진 회전 범위 안에서 손으로 조준합니다.
  - *Death From Above*: AC-130 화면을 오른쪽 스틱으로 조준하고, 무기 전환과 열화상 전환은 컨트롤러 버튼으로 합니다.
  - 거치 기관총과 차량 총좌(예: *Shock and Awe*의 MK19): 손이 손잡이를 쥐고 있고, 팔을 움직이면 총이 돕니다.
  - *Safehouse*: 코브라 공중 지원. 손을 귀에 대고 그립을 누르고, 목표를 가리킨 뒤 트리거를 누릅니다.
  - *Aftermath*: 손으로 바닥을 짚고 당겨서 헬기 잔해 밖으로 기어 나갑니다.
  - 사다리는 손을 번갈아 잡으며 오릅니다.
  - 클레이모어는 손을 바닥에 대고 설치하고, 덮친 개는 신호에 맞춰 그립·트리거로 떨쳐냅니다.
- **실제로 웅크리거나 엎드리면** 게임 자세도 바뀝니다(스틱도 됩니다).
- **재블린·스팅어 머리 조준** 옵션: 발사관 대신 머리 방향으로 조준하고 록온합니다.
- **데스크톱 런처**: 처음 설정 안내, VR 설정, 편의·보조 옵션, 조작 도움말을 제공합니다.

## 요구 사항

- 64비트 Windows 10 또는 11
- 정식으로 구매해 설치한 **Steam판** *콜 오브 듀티: 모던 워페어 리마스터*. Battle.net판은 설치 프로그램이 찾아 주지만, 그 실행 파일에서 모드가 돌아가는지는 아직 확인하지 않았습니다.
- SteamVR, 그리고 SteamVR에서 동작하는 헤드셋과 추적 컨트롤러
- 설치 프로그램용 .NET Framework 4.5 이상 (Windows 10/11에 기본 포함)

클라이언트는 Direct3D 11을 쓰고 기본은 OpenXR입니다. 런처에서 OpenVR로 바꿀 수도 있습니다. 기본 컨트롤러 설정은 Overlord에서 가져왔습니다(Oculus Touch(메타 퀘스트), Valve Index, Vive). 주 대상은 SteamVR로 연결한 메타 퀘스트 컨트롤러이고, 다른 기기는 버튼 설정이나 손 위치 보정이 필요할 수 있습니다.

불법 복제판은 지원하지 않습니다. 배포 파일에 게임 파일은 들어 있지 않습니다.

## 설치

[Releases 페이지](https://github.com/lesan0622/Overlord-MWR-VR/releases)에서 최신 버전을 받으세요.

**설치 프로그램(권장)**: `OverlordMWR-VR-Setup.exe`를 실행합니다.
- Steam(과 Battle.net)에서 게임 폴더를 찾고, 설치 전에 한 번 묻습니다.
- 게임이 켜져 있으면 설치하지 않습니다.
- 모드 파일만 쓰고, 바탕화면에 바로가기를 만듭니다.
- 게임을 못 찾으면 `h1_sp64_ship.exe`가 있는 게임 폴더를 직접 고르면 됩니다.

**수동 설치**: `OverlordMWR-VR-<버전>.zip`을 게임 폴더에 풉니다.

~~~text
Call of Duty Modern Warfare Remastered/
├─ h1_sp64_ship.exe      (게임)
├─ overlord.exe
├─ openxr_loader.dll
├─ overlord.vrmanifest
├─ steamvr/
├─ vr_input/
└─ overlord-mwr/
~~~

그다음 SteamVR을 켜고 헤드셋과 컨트롤러를 연결합니다. `overlord.exe`(또는 바탕화면 바로가기)를 실행하고 싱글플레이를 고르세요.

## 기본 조작

| 동작 | 방법 |
| --- | --- |
| 이동 / 회전 | 왼쪽 스틱 / 오른쪽 스틱 (설정에서 부드러운 회전과 끊어 돌기 선택) |
| 총 쥐기 | 손을 손잡이에 대고 그립을 누르고 있기 |
| 사격 | 사격 손잡이를 쥔 손의 트리거 |
| 재장전 | 빈손으로 허리에서 트리거를 누르면 탄창이 나옵니다. 끼운 뒤 약실이 비었으면 노리쇠나 장전 손잡이를 당깁니다. |
| 유탄발사기 | M203: 총열을 앞으로 밀고, 허리에서 트리거로 유탄을 꺼내 약실에 넣고, 총열을 닫습니다. GP25: 허리에서 트리거로 꺼내 총구에 넣습니다. 허리에서 그립과 트리거를 함께 누르면 반대쪽 탄(유탄 대신 소총 탄창, 또는 그 반대)이 나옵니다. |
| 총 넣기 | 총을 몸의 홀스터 위치로 옮기고 그립을 놓기 |
| 거치 기관총 | 오른쪽 그립으로 오르고 내리며, 팔로 총을 돌리고, 트리거로 사격 |

런처의 도움말은 Overlord에서 가져온 것이라 아직 MWR 무기를 다 설명하지 못합니다.

## 알려진 문제

- Beta 1의 여러 기능은 아직 헤드셋 확인이 남아 있습니다: 바렛 양각대.
- 컷신에서 화면 좌우가 뒤집힌다는 보고가 있으나 아직 재현하지 못했습니다.
- Overlord에서 이어진 문제
  - `Create2DTexture` 오류가 나면 그래픽 설정에서 셰이더 미리 불러오기를 꺼 보세요.
  - MSI Afterburner가 실행을 막을 수 있습니다.

## 문제 보고

**lesan00622@gmail.com**으로 메일을 보내 주세요(런처의 *이메일로 보고* 버튼도 됩니다).

1. 문제가 보일 때 게임 창을 선택한 상태로 **Ctrl+Shift+F8**을 누릅니다. 콘솔에서 **`vr_diagnose`**를 입력해도 됩니다.
2. 5초쯤 뒤 탐색기에 진단 ZIP 파일이 선택된 채로 열립니다.
3. 그 파일을 첨부하고, 무슨 일이 어디서(미션, 체크포인트, 무기) 일어났는지 적어 주세요.

게임이 튕겼다면 `minidumps` 폴더의 크래시 ZIP을 첨부해 주세요. 게임 파일이나 계정 정보는 보내지 마세요.

## 소스에서 빌드

서브모듈까지 받아야 합니다. GitHub의 소스 ZIP에는 서브모듈이 빠져 있습니다.

다음을 설치합니다.
- Visual Studio (*C++를 사용한 데스크톱 개발*, Windows SDK)
- Python 3
- Node.js (클라이언트 빌드 중에 런처 화면을 빌드합니다)

그다음 개발자 명령 프롬프트에서 실행합니다.

~~~bat
git clone --recurse-submodules https://github.com/lesan0622/Overlord-MWR-VR.git
cd Overlord-MWR-VR
generate.bat
msbuild build\overlord.sln /m /p:Configuration=Release /p:Platform=x64
python tools\build_installer.py Release
~~~

빌드 결과는 `build/bin/x64/Release/`에 생깁니다. 설치 프로그램과 수동 설치용 ZIP은 `build/dist/`에 생깁니다. 자세한 내용은 [개발 안내](docs/development.md)와 [문서 목록](docs/README.md)을 보세요. Overlord에서 이어받은 문서로, 대부분 공통 VR 시스템에 관한 영어 문서입니다.

## 라이선스와 감사의 말

코드는 [GNU GPLv3](LICENSE)로 배포됩니다. 서드파티 코드, 글꼴, 기타 리소스는 각자의 라이선스를 따릅니다. [서드파티 고지](THIRD_PARTY_NOTICES.md)와 [소스 출처](docs/source-provenance.md)를 보세요.

이 포팅은 [Aeka0](https://github.com/Aeka0)의 [Overlord](https://github.com/Aeka0/Overlord)를 바탕으로 합니다. 클라이언트 기반은 H2-Mod, h1-mod(MWR 주소와 구조체), [IW6x](https://git.alterware.dev/alterware/iw6-mod), [S1x](https://git.alterware.dev/alterware/s1-mod)입니다.

아래 분들과 프로젝트에도 감사드립니다.
- [momo5502](https://github.com/momo5502), [xensik](https://github.com/xensik/gsc-tool), [JariKCoding](https://github.com/JariKCoding/CoDLuaDecompiler), [ZoneTool](https://github.com/ZoneTool/zonetool), [quaK](https://github.com/Joelrau)
- Valve, Khronos, REFramework의 관련 작업

이 포팅은 따로 관리되며, Overlord 제작자와 관계가 없고 그가 보증하지도 않습니다. 이 포팅의 문제는 Overlord 프로젝트가 아니라 이곳으로 보내 주세요.

이 모드는 독립 커뮤니티 모드이며 공식 게임 제품이 아닙니다. 게임 이름과 상표는 각 권리자의 것입니다. 불법 복제나 복제 방지 우회에 쓰면 안 됩니다.
