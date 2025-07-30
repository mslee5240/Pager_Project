# 삐삐(무선호출기) 프로젝트

## 프로젝트 개요

Raspberry Pi 4를 이용한 무선호출기(삐삐) 시뮬레이터입니다. 4x4 키패드로 숫자를 입력하고, 16x2 LCD에 메시지를 표시하며, TCP 소켓 통신을 통해 다른 사용자와 실시간 메시지를 주고받을 수 있습니다.

## 하드웨어 구성

- **Raspberry Pi 4** (BCM2711)
- **16x2 I2C LCD 디스플레이** (PCF8574 백팩)
- **4x4 매트릭스 키패드**
- **GPIO 연결**

### GPIO 핀 배치

```
키패드 연결:
- 열(COL): GPIO 21, 20, 16, 12
- 행(ROW): GPIO 13, 19, 26, 18

LCD 연결:
- I2C 주소: 0x27
- SDA: GPIO 2, SCL: GPIO 3
```

## 소프트웨어 구조

### 1. 커널 모듈 (LCD 드라이버)
- **파일**: `my_i2c_lcd1602.c`
- **기능**: I2C를 통한 LCD 제어
- **디바이스**: `/dev/my_i2c_lcd1602`

### 2. 키패드 제어 라이브러리
- **파일**: `keypad.h`, `keypad.c`
- **기능**: 4x4 키패드 입력 처리, LCD 출력 제어
- **키 매핑**:
  ```
  [SEND] [0] [ ] [ ]
  [ ]    [9] [6] [3]
  [ ]    [8] [5] [2]
  [END]  [7] [4] [1]
  ```

### 3. 네트워크 통신
- **서버**: `server.c` - 다중 클라이언트 채팅 서버
- **클라이언트**: `client.c` - 키패드 입력 기반 채팅 클라이언트

## 주요 기능

### 클라이언트 (삐삐 단말기)
- 키패드로 숫자 입력
- LCD 1번째 줄: 수신 메시지 표시
- LCD 2번째 줄: 입력 중인 메시지 표시
- SEND 키('v'): 메시지 전송
- END 키('e'): 프로그램 종료

### 서버 (중계 서버)
- 최대 100명 동시 접속
- 실시간 메시지 브로드캐스트
- 클라이언트 관리 (접속/종료 알림)

## 설치 및 실행

### 1. LCD 드라이버 컴파일

```makefile
# Makefile 생성
obj-m := my_i2c_lcd1602.o
KDIR := $(HOME)/linux-study/linux
PWD  := $(shell pwd)

all:
	make -C $(KDIR) M=$(PWD) modules -j12 ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu-

clean:
	make -C $(KDIR) M=$(PWD) clean
```

```bash
# 컴파일
$ make

# 라즈베리파이로 전송
$ scp my_i2c_lcd1602.ko pi@<IP>:/home/pi/
```

### 2. 드라이버 설치 (라즈베리파이에서)

```bash
# 모듈 로드
$ sudo insmod my_i2c_lcd1602.ko

# 디바이스 노드 생성
$ sudo mknod /dev/my_i2c_lcd1602 c $(grep my_i2c_lcd1602 /proc/devices | awk '{print $1}') 0

# 권한 설정
$ sudo chmod 666 /dev/my_i2c_lcd1602
```

### 3. 애플리케이션 컴파일

```bash
# 클라이언트 및 서버 컴파일
$ gcc client.c keypad.c -o client -Wall -pthread
$ gcc server.c -o server -Wall -pthread
```

### 4. 실행

```bash
# 서버 실행 (PC 또는 라즈베리파이)
$ ./server

# 클라이언트 실행 (라즈베리파이에서)
$ sudo ./client <server_ip_address>
```

## 사용법

1. **메시지 입력**: 키패드로 숫자 입력
2. **메시지 전송**: SEND 키 누르기
3. **메시지 수신**: LCD 1번째 줄에 자동 표시
4. **프로그램 종료**: END 키 누르기

## 기술적 특징

- **멀티스레딩**: 키패드 입력과 네트워크 수신 동시 처리
- **커널 드라이버**: 직접 구현한 I2C LCD 드라이버
- **GPIO 직접 제어**: /dev/mem을 통한 저수준 하드웨어 접근
- **실시간 통신**: TCP 소켓 기반 즉시 메시지 전달

## 트러블슈팅

### LCD가 동작하지 않는 경우
```bash
# I2C 주소 확인
$ sudo i2cdetect -y 1

# I2C 활성화 확인
$ sudo raspi-config  # Interfacing Options → I2C → Enable
```

### 키패드 입력이 안 되는 경우
```bash
# GPIO 권한 확인 (root 권한 필요)
$ sudo ./client <server_ip>
```

### 컴파일 오류
```bash
# 필요한 패키지 설치
$ sudo apt-get update
$ sudo apt-get install build-essential
```

## 라이선스

GPL v2.0

## 작성자

Minsoo