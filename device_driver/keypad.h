// keypad.h - 4x4 매트릭스 키패드 제어 헤더 파일
// 
// Raspberry Pi 4용 4x4 매트릭스 키패드를 GPIO를 통해 제어하기 위한
// 매크로, 상수, 함수 선언들을 정의합니다.
// 
// 하드웨어 구성:
// - BCM2711 SoC의 GPIO 핀 사용
// - 4x4 매트릭스 키패드 (16개 키)
// - LCD 디스플레이 연동

#ifndef KEYPAD_H
#define KEYPAD_H

// 표준 라이브러리 헤더
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>          // 파일 제어 (open, O_RDWR 등)
#include <sys/mman.h>       // 메모리 매핑 (mmap)
#include <unistd.h>         // UNIX 표준 함수들
#include <string.h>         // 문자열 처리 함수들
#include <pthread.h>        // POSIX 스레드
#include <signal.h>         // 시그널 처리
#include <stdint.h>         // 표준 정수 타입

// ================= GPIO 하드웨어 관련 정의 =================

// BCM2711 (Raspberry Pi 4) 물리 주소
#define BCM2711_PERI_BASE   0xFE000000                      // 주변장치 베이스 주소
#define GPIO_BASE           (BCM2711_PERI_BASE + 0x200000)  // GPIO 레지스터 베이스 주소
#define BLOCK_SIZE          (4*1024)                        // mmap 블록 크기 (4KB)

// 4x4 키패드 GPIO 핀 번호 (BCM 번호 기준)
// 열(Column) 핀들 - 출력용 (스캔 신호)
#define COL1    21          // 첫 번째 열
#define COL2    20          // 두 번째 열  
#define COL3    16          // 세 번째 열
#define COL4    12          // 네 번째 열

// 행(Row) 핀들 - 입력용 (키 상태 읽기)
#define ROW1    13          // 첫 번째 행
#define ROW2    19          // 두 번째 행
#define ROW3    26          // 세 번째 행
#define ROW4    18          // 네 번째 행

// GPIO 핀 배열 (외부에서 정의됨)
extern int colPins[4];      // 열 핀 배열
extern int rowPins[4];      // 행 핀 배열

// GPIO 레지스터 접근을 위한 포인터
extern volatile unsigned *gpio;

// ================= GPIO 제어 매크로 =================

// GPIO 핀 기능 설정 매크로
#define INP_GPIO(g)     *(gpio+((g)/10)) &= ~(7<<(((g)%10)*3))  // 입력으로 설정
#define OUT_GPIO(g)     *(gpio+((g)/10)) |=  (1<<(((g)%10)*3))  // 출력으로 설정

// GPIO 출력 제어 매크로
#define GPIO_SET        *(gpio+7)                               // 핀을 HIGH로 설정
#define GPIO_CLR        *(gpio+10)                              // 핀을 LOW로 설정

// GPIO 입력 읽기 매크로
#define GET_GPIO(g)     (*(gpio+13)&(1<<g))                     // 핀 상태 읽기 (0 또는 1)

// GPIO 풀업/풀다운 저항 제어 매크로
#define GPIO_PULL       *(gpio+37)                              // 풀업/풀다운 모드 설정
#define GPIO_PULLCLK0   *(gpio+38)                              // 풀업/풀다운 클럭 제어

// ================= 키패드 상태 정의 =================

// 키 상태 정의 (풀업 저항 기준)
#define PUSHED      0       // 키가 눌린 상태 (LOW)
#define RELEASED    1       // 키가 떼어진 상태 (HIGH)

// 특수 키 정의
#define SEND        'v'     // 입력 완료 및 전송 키
#define END_SIGN    'e'     // 프로그램 종료 키

// ================= 키패드 매핑 테이블 =================

// 키패드 물리적 배치 (4x4 매트릭스):
// 
//      COL1  COL2  COL3  COL4
// ROW1  S16   S12   S8    S4     →  {SEND, '0', ' ', ' '}
// ROW2  S15   S11   S7    S3     →  {' ', '9', '6', '3'}  
// ROW3  S14   S10   S6    S2     →  {' ', '8', '5', '2'}
// ROW4  S13   S9    S5    S1     →  {END_SIGN, '7', '4', '1'}
extern char keypadChar[4][4];

// ================= LCD 관련 정의 =================

#define LCD_DEV     "/dev/mylcd"    // LCD 캐릭터 디바이스 경로

// ================= 입력 버퍼 및 동기화 =================

// 키패드 입력 관리 변수들
extern char input_buf[17];          // 입력 버퍼 (최대 16자 + NULL 종료자)
extern int idx;                     // 현재 입력 위치 인덱스
extern int is_send;                 // 전송 준비 플래그 (0: 입력중, 1: 전송준비)
extern pthread_mutex_t buf_mutex;   // 버퍼 접근 동기화용 뮤텍스

// ================= 프로그램 제어 =================

extern volatile int keepRunning;    // 프로그램 실행 제어 플래그

// ================= 함수 선언 =================

// 초기화 함수
void keypad_init(void);             // 키패드 및 GPIO 초기화

// GPIO 관련 함수
void setup_io();                    // GPIO 메모리 매핑 설정
void set_pull_up(int g);            // 지정 핀에 풀업 저항 설정

// 키패드 입력 함수
char getKeypadState(int col, int row);  // 특정 키의 상태 확인
char keypadScan();                  // 전체 키패드 스캔

// 버퍼 관리 함수
void clear_keypad_str(void);        // 입력 버퍼 초기화

// 스레드 함수
void* keypad_thread(void* arg);     // 키패드 입력 처리 스레드

// LCD 제어 함수
void lcd_write_line1(const char* str);  // LCD 첫 번째 줄에 문자열 출력
void lcd_clear_line1();             // LCD 첫 번째 줄 지우기
void lcd_write_line2(const char* str);  // LCD 두 번째 줄에 문자열 출력  
void lcd_clear_line2();             // LCD 두 번째 줄 지우기

#endif // KEYPAD_H