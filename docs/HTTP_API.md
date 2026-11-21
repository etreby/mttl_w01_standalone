# 로컬 HTTP 제어 API

한국어 | [English](HTTP_API_EN.md) · [프로젝트](../README.md)

현재 공개 펌웨어 2.1.1의 웹 대시보드가 사용하는 API입니다. 별도 MQTT 브로커 없이 로컬 프로그램에서도 호출할 수 있습니다. 이 문서 추가로 펌웨어 동작이 변경되지는 않습니다.

## 사용 조건

- 기기가 공유기에 연결되어 정상 온라인 상태여야 합니다. SoftAP에서는 웹 제어 명령이 거절됩니다.
- 주소는 `http://<멀티탭의 현재 IPv4 주소>`입니다. 아래 IP는 예시입니다.
- 인증 토큰은 없으며 `Origin` 검사는 인증이 아닙니다. 선택형 PIN을 설정할 수 있습니다([선택형 PIN](#선택형-pin-x-mttl-pin) 참고). 설정하지 않으면 기존과 동일합니다. 신뢰할 수 있는 내부망에서만 사용하고 인터넷에 포트를 공개하지 마십시오.
- 다른 출처의 웹페이지를 위한 CORS API가 아닙니다. 브라우저 예제는 멀티탭 웹페이지의 개발자 콘솔에서 실행합니다. 외부 프로그램은 `Origin` 헤더를 직접 설정해야 합니다.

## POST /api/control

`Content-Type: application/octet-stream`으로 **정확히 2바이트의 바이너리 본문**을 보냅니다. JSON, 문자열 `01`, `[0,1]` 텍스트, 폼 데이터가 아닙니다.

| 바이트 위치 | 의미 | 허용 값 |
|---|---|---|
| 0 | 채널 | `0`=채널 1, `1`=채널 2, `2`=채널 3, `3`=채널 4, `4`=전체 |
| 1 | 동작 | `0`=OFF, `1`=ON |

`Origin: http://<현재 IPv4 주소>`가 반드시 필요합니다. 경로·끝 슬래시·`:80`을 붙이지 않고, IP는 요청 대상 기기의 현재 주소와 일치시킵니다.

### Python 예제 — 표준 라이브러리

```python
import urllib.request

base = 'http://192.168.0.153'  # 본인 멀티탭 IP로 변경
request = urllib.request.Request(
    base + '/api/control',
    data=bytes([0, 1]),  # 채널 1 ON; 전체 OFF는 bytes([4, 0])
    headers={'Content-Type': 'application/octet-stream', 'Origin': base},
    method='POST',
)
with urllib.request.urlopen(request, timeout=10) as response:
    print(response.status, response.read().decode())
```

### JavaScript 예제 — 멀티탭 페이지의 개발자 콘솔

```javascript
const response = await fetch('/api/control', {
  method: 'POST',
  headers: {'Content-Type': 'application/octet-stream'},
  body: new Uint8Array([0, 1]) // 채널 1 ON
});
console.log(response.status, await response.text());
```

브라우저가 POST 요청의 `Origin`을 설정합니다. 이 예제를 다른 웹사이트나 로컬 HTML 파일에서 실행하는 방식은 지원하지 않습니다.

## 응답과 상태 확인

| HTTP 상태 | 의미 |
|---|---|
| `200` | 제어 큐에 명령 접수 성공. 실제 릴레이 전환 완료를 의미하지 않음 |
| `400` | `Origin` 불일치/누락 또는 본문 길이가 2바이트가 아님 |
| `403` | PIN이 설정되어 있고 `X-MTTL-PIN`이 없거나 틀림(`PIN required`), 또는 연속 5회 틀려 60초 동안 PIN 보호 요청이 잠김(`PIN locked; retry in 60 s`) |
| `429` | 잘못된 채널/동작 값, 큐 포화, 오프라인, OTA·설정 작업 중 또는 하드웨어/릴레이 오류 등으로 접수 거절 |
| `405` | GET·POST 외의 지원하지 않는 메서드 |

성공 응답:

```json
{"accepted":true,"contact_feedback":false}
```

`contact_feedback:false`는 물리적인 접점 피드백을 읽어 확인한 결과가 아니라는 뜻입니다. 웹/HA 제어와 마찬가지로 물리 버튼 잠금의 영향을 받지 않지만 보호 차단과 타이머 등의 동작은 계속 적용됩니다.

- `GET /api/device`: 채널별 `on` 등 논리 상태와 센서값을 조회합니다. `on:null`은 상태 미확인입니다. 물리 접점 상태의 보증은 아닙니다.
- `GET /api/status`: 네트워크·계측 상태를 조회합니다.
- `GET /api/control`: `sequence`, `wait_ms`, `run_ms`, `busy` 제어 진단을 조회할 뿐 릴레이를 조작하지 않습니다. 개별 요청에 대한 완료 확인 ID는 제공하지 않습니다.

주소창에서 `?ch=1&state=1`로 제어하는 `/api/relay` API는 현재 공개 펌웨어에 없습니다. 요청 타임아웃은 기기에서 명령이 접수되지 않았음을 보장하지 않으므로, 무조건 반복 전송하지 말고 상태를 확인하십시오. 대량·무제한 재시도는 피하십시오.

## 펌웨어 업로드: POST /api/firmware/upload

`.fwr` 파일을 비활성 슬롯에 기록하고, 모든 검사를 통과한 경우에만 활성화 후 재부팅합니다. 대시보드 펌웨어 탭이 사용하는 엔드포인트입니다.

| 헤더 | 필수 | 값 |
|---|---|---|
| `Origin` | 예 | `http://<현재 IPv4 주소>` |
| `Content-Type` | 예 | `application/x-mttl-fwr` |
| `Content-Length` | 예 | 정확한 `.fwr` 크기(44~434196바이트) |
| `X-MTTL-PIN` | PIN 설정 시 | 현재 PIN([선택형 PIN](#선택형-pin-x-mttl-pin) 참고) |
| `X-MTTL-SHA256` | 아니요 | 16진수 64자(대소문자 무관): **업로드하는 `.fwr` 파일 원본 그대로**의 SHA-256. 즉 `sha256sum file.fwr` 출력값이자 OTA 서버가 게시하는 값 |

본문은 `.fwr` 파일 원본입니다. 요청 헤더 전체는 1024바이트 이내여야 하며 `X-MTTL-SHA256`은 한 번만 보냅니다(중복된 헤더 줄은 없는 것으로 처리됩니다).

`X-MTTL-SHA256` 동작:

- **없음:** 기존과 동일합니다. 내부 형식·체크섬·수신·읽기 검증·슬롯 검사만 적용됩니다.
- **형식 오류**(정확히 16진수 64자가 아님): 플래시 지우기 전에 `400 Malformed X-MTTL-SHA256; no write`를 반환합니다.
- **있음:** 기기가 수신한 모든 바이트를 디코딩 전에 해시합니다. 값이 다르면 활성화 전에 세션을 중단하고 실행 중인 이미지를 유지하며 `400 SHA-256 mismatch; nothing activated`를 반환합니다. 비활성 슬롯에 일부 기록된 데이터는 부팅되지 않습니다.

이 헤더를 지원하는 펌웨어는 `GET /api/firmware/status`에 `"sha256_header":true`를 표시합니다.

```python
import hashlib, urllib.request
base = 'http://192.168.0.153'
data = open('comMTTL-W01_2.1.1.fwr', 'rb').read()
request = urllib.request.Request(base + '/api/firmware/upload', data=data, method='POST', headers={
    'Origin': base, 'Content-Type': 'application/x-mttl-fwr',
    'X-MTTL-SHA256': hashlib.sha256(data).hexdigest()})  # 또는 OTA 서버가 게시한 값
print(urllib.request.urlopen(request, timeout=180).read().decode())
```

성공 응답은 `{"written":true,"activated":true,"rebooting":true}`(복구 시 `restore_version` 포함) 또는 `{"already_installed":true,"activated":false,"rebooting":false}`입니다.

## 선택형 PIN (X-MTTL-PIN)

기본값은 PIN 없음이며 이때는 동작이 바뀌지 않습니다. PIN(4~12자리 ASCII 숫자)을 설정하면 다음 요청에 `X-MTTL-PIN: <pin>` 헤더가 필요합니다.

- `POST /api/control`
- `POST /api/firmware/upload`
- `POST /api/config`

PIN은 상수 시간으로 비교합니다. 없거나 틀리면 `403 PIN required`를 반환합니다. 연속 5회 틀리면 60초 동안 PIN 보호 요청 전체에 `403 PIN locked; retry in 60 s`를 반환합니다(헤더 누락은 틀린 PIN으로 세지 않으며, 올바른 PIN은 횟수를 초기화합니다). GET 엔드포인트는 계속 열려 있으며 PIN을 반환하지 않습니다. `GET /api/firmware/status`는 `"pin_required":true|false`를, `GET /api/config`는 `"pin_set"`과 성공적으로 적용된 설정 저장 횟수 `"applied"`를 표시합니다(PIN 변경 적용 확인용). Wi-Fi 설정·검색과 MQTT 명령은 PIN으로 보호되지 않습니다.

**설정·변경·해제:** 일반 설정 본문을 `POST /api/config`로 보내면서 `X-MTTL-New-PIN: <4~12자리 숫자>` 또는 `X-MTTL-New-PIN: clear`를 붙입니다. PIN이 설정되어 있으면 현재 `X-MTTL-PIN`도 필요합니다. 잘못된 값은 `400 Invalid X-MTTL-New-PIN`입니다. `X-MTTL-New-PIN` 없이 보낸 `POST /api/config`는 저장된 PIN을 유지하므로 기존 스크립트가 실수로 PIN을 지우지 않습니다.

**PIN 분실:** 본체 메인 버튼을 10초 동안 누르면(Wi-Fi 설정 진입) 전력량 초기화와 같은 플래시 저장에서 PIN이 해제됩니다. 설정 진입과 마찬가지로 유효한 계량 카운터가 준비될 때까지 기다립니다.

**다운그레이드:** PIN을 지원하지 않는 펌웨어는 PIN이 포함된 설정 기록을 읽지 못해 기본 설정(설정 AP)으로 시작합니다. 이전 펌웨어를 설치하기 전에 PIN을 해제하세요.

PIN은 암호화되지 않은 HTTP로 전송됩니다. 내부망의 실수나 가벼운 접근을 막는 용도이며, 내부망 트래픽을 볼 수 있는 사람으로부터 보호하지는 않습니다.

```python
headers = {'Content-Type': 'application/octet-stream', 'Origin': base, 'X-MTTL-PIN': '2468'}
```
