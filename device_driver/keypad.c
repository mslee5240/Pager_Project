#include "keypad.h"

/* GPIO 메모리 매핑된 포인터 */
volatile unsigned *gpio = NULL;

/* 키패드 핀 배열 정의 */
int colPins[4] = {COL1, COL2, COL3, COL4};  // 출력 핀 (스캔용)
int rowPins[4] = {ROW1, ROW2, ROW3, ROW4};  // 입력 핀 (읽기용)

/* 키패드 문자 매핑 테이블 [열][행] */
// 하드웨어 배치:
// {S16, S12, S8, S4},   // COL0
// {S15, S11, S7, S3},   // COL1  
// {S14, S10, S6, S2},   // COL2
// {S13, S9,  S5, S1}    // COL3
char keypadChar[4][4] = {
    {SEND, '0', ' ', ' '},      // COL0: 전송, 0, 미사용, 미사용
    {' ', '9', '6', '3'},       // COL1: 미사용, 9, 6, 3
    {' ', '8', '5', '2'},       // COL2: 미사용, 8, 5, 2  
    {END_SIGN, '7', '4', '1'}   // COL3: 종료, 7, 4, 1
};

/* 이전 키 상태 저장 (디바운싱용) */
static uint8_t prevState[4][4] = {
    {RELEASED, RELEASED, RELEASED, RELEASED},
    {RELEASED, RELEASED, RELEASED, RELEASED},
    {RELEASED, RELEASED, RELEASED, RELEASED},
    {RELEASED, RELEASED, RELEASED, RELEASED}
};

/* 입력 버퍼 및 상태 관리 */
char input_buf[17] = {0};     // 최대 16자 + NULL 종료자
int idx = 0;                  // 현재 입력 위치
int is_send = 0;              // 전송 플래그: 0=입력중, 1=전송준비완료
pthread_mutex_t buf_mutex = PTHREAD_MUTEX_INITIALIZER;  // 버퍼 동기화용

/* 프로그램 실행 제어 */
volatile int keepRunning = 1;

/* Ctrl+C 시그널 핸들러 - 프로그램 종료 */
void signalHandler(int sig) {
    keepRunning = 0;
}

/**
 * keypad_init - 키패드 및 GPIO 초기화
 * 
 * GPIO 핀을 설정하고 키패드 스캔을 위한 환경을 구성:
 * - 열 핀들: 출력으로 설정 (스캔 신호용)
 * - 행 핀들: 입력으로 설정하고 풀업 저항 활성화
 */
void keypad_init(void){
    signal(SIGINT, signalHandler);  // Ctrl+C 핸들러 등록

    // GPIO 메모리 매핑 초기화
    setup_io();
    
    // 열 핀들을 출력으로 설정하고 HIGH로 초기화
    for (int i = 0; i < 4; i++) {
        INP_GPIO(colPins[i]);       // 먼저 입력으로 설정 (GPIO 클리어)
        OUT_GPIO(colPins[i]);       // 출력으로 설정
        GPIO_SET = 1 << colPins[i]; // HIGH로 설정 (비활성)
    }
    
    // 행 핀들을 입력으로 설정하고 풀업 저항 활성화
    for (int i = 0; i < 4; i++) {
        INP_GPIO(rowPins[i]);       // 입력으로 설정
        set_pull_up(rowPins[i]);    // 내부 풀업 저항 활성화
    }

    lcd_clear_line2();              // LCD 2번째 줄 초기화
}

/**
 * setup_io - GPIO 메모리 매핑 설정
 * /dev/mem을 통해 GPIO 레지스터에 직접 접근할 수 있도록 메모리 매핑
 */
void setup_io() {
    int mem_fd;
    void *gpio_map;
    
    // /dev/mem 열기 (물리 메모리 접근)
    if ((mem_fd = open("/dev/mem", O_RDWR|O_SYNC)) < 0) exit(1);
    
    // GPIO 레지스터 영역을 가상 메모리에 매핑
    gpio_map = mmap(NULL, BLOCK_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED, mem_fd, GPIO_BASE);
    close(mem_fd);
    
    if (gpio_map == MAP_FAILED) exit(1);
    gpio = (volatile unsigned *)gpio_map;
}

/**
 * set_pull_up - 지정된 GPIO 핀에 내부 풀업 저항 설정
 * @g: GPIO 핀 번호
 * 
 * BCM2835의 풀업/풀다운 설정 시퀀스:
 * 1. GPPUD에 풀업 모드 설정 (0x02)
 * 2. 150사이클 대기
 * 3. GPPUDCLK에 해당 핀 활성화
 * 4. 150사이클 대기  
 * 5. 레지스터 클리어
 */
void set_pull_up(int g) {
    GPIO_PULL = 0x02;           // 풀업 모드 설정
    usleep(500);                // 안정화 대기
    GPIO_PULLCLK0 = (1 << g);   // 해당 핀에 풀업 적용
    usleep(500);                // 적용 대기
    GPIO_PULL = 0;              // 풀업 설정 해제
    GPIO_PULLCLK0 = 0;          // 클럭 신호 해제
    usleep(200);                // 최종 안정화
}

/**
 * getKeypadState - 특정 키의 상태 확인 및 키 값 반환
 * @col: 열 번호 (0~3)
 * @row: 행 번호 (0~3)
 * @return: 눌렸다 떼어진 키의 문자, 없으면 0
 * 
 * 매트릭스 스캔 방식:
 * 1. 해당 열만 LOW, 나머지 열은 HIGH로 설정
 * 2. 행 핀의 상태 읽기 (풀업이므로 눌리면 LOW)
 * 3. 이전 상태와 비교하여 키 릴리스 감지
 */
char getKeypadState(int col, int row) {
    char key = 0;
    
    // 1. 매트릭스 스캔: 해당 열만 LOW, 나머지는 HIGH
    for (int i = 0; i < 4; i++) {
        if (i == col) 
            GPIO_CLR = 1 << colPins[i];  // 선택된 열을 LOW로
        else 
            GPIO_SET = 1 << colPins[i];  // 나머지 열은 HIGH로
    }
    usleep(50);                          // 신호 안정화 대기

    // 2. 행 핀 상태 읽기 (풀업 기준: HIGH=떼어짐, LOW=눌림)
    uint8_t curState = (GET_GPIO(rowPins[row]) ? RELEASED : PUSHED);

    // 3. 키 릴리스 감지 (눌림→떼어짐 전환 시점에서 키 인식)
    if (curState == RELEASED && prevState[col][row] == PUSHED) {
        key = keypadChar[col][row];      // 해당 위치의 키 문자 반환
    }
    prevState[col][row] = curState;      // 현재 상태 저장

    // 4. 스캔 완료 후 열 핀을 다시 HIGH로 복원
    GPIO_SET = 1 << colPins[col];

    return key;
}

/**
 * keypadScan - 전체 키패드 스캔
 * @return: 눌린 키의 문자, 없으면 0
 * 
 * 4x4 매트릭스를 순차적으로 스캔하여 눌린 키를 찾음
 */
char keypadScan() {
    char data = 0;
    
    // 모든 열과 행을 순회하며 키 상태 확인
    for (int col = 0; col < 4; col++) {
        for (int row = 0; row < 4; row++) {
            data = getKeypadState(col, row);
            if (data != 0) return data;      // 키가 감지되면 즉시 반환
        }
    }
    return 0;                                // 눌린 키가 없음
}

/**
 * lcd_write_line1 - LCD 첫 번째 줄에 문자열 출력
 * @str: 출력할 문자열 (최대 16자)
 */
void lcd_write_line1(const char* str) {
    int fd = open(LCD_DEV, O_WRONLY);
    if (fd < 0) return;
    
    char buffer[18] = {0};
    buffer[0] = '0';                         // 첫 번째 줄 지시자
    strncpy(buffer+1, str, 16);              // 최대 16자 복사
    write(fd, buffer, strlen(buffer));
    close(fd);
}

/**
 * lcd_clear_line1 - LCD 첫 번째 줄 지우기
 */
void lcd_clear_line1() {
    int fd = open(LCD_DEV, O_WRONLY);
    if (fd < 0) return;
    
    char buffer[18] = "0                ";   // 첫 번째 줄을 공백으로 채움
    write(fd, buffer, 17);
    close(fd);
}

/**
 * lcd_write_line2 - LCD 두 번째 줄에 문자열 출력
 * @str: 출력할 문자열 (최대 16자)
 */
void lcd_write_line2(const char* str) {
    int fd = open(LCD_DEV, O_WRONLY);
    if (fd < 0) return;
    
    char buffer[18] = {0};
    buffer[0] = '1';                         // 두 번째 줄 지시자
    strncpy(buffer+1, str, 16);              // 최대 16자 복사
    write(fd, buffer, strlen(buffer));
    close(fd);
}

/**
 * lcd_clear_line2 - LCD 두 번째 줄 지우기
 */
void lcd_clear_line2() {
    int fd = open(LCD_DEV, O_WRONLY);
    if (fd < 0) return;
    
    char buffer[18] = "1                ";   // 두 번째 줄을 공백으로 채움
    write(fd, buffer, 17);
    close(fd);
}

/**
 * clear_keypad_str - 키패드 입력 버퍼 초기화
 * 
 * 입력 버퍼를 비우고 관련 상태들을 초기값으로 재설정
 */
void clear_keypad_str(void){
    memset(input_buf, 0, sizeof(input_buf)); // 버퍼 전체를 0으로 초기화
    idx = 0;                                 // 입력 위치 초기화
    is_send = 0;                             // 전송 플래그 초기화
    lcd_clear_line2();                       // LCD 두 번째 줄 지우기
}

/**
 * keypad_thread - 키패드 입력 처리 스레드
 * @arg: 스레드 인자 (사용하지 않음)
 * @return: NULL
 * 
 * 무한 루프로 키패드를 스캔하고 입력을 처리:
 * - 숫자 키: 입력 버퍼에 추가하고 LCD에 표시
 * - SEND 키: 전송 플래그 설정
 * - END_SIGN 키: 프로그램 종료
 */
void* keypad_thread(void* arg) {
    while (keepRunning) {
        char key = keypadScan();             // 키패드 스캔
        
        if (key) {                           // 키가 눌렸을 때
            pthread_mutex_lock(&buf_mutex);  // 버퍼 접근 동기화
            
            // 전송 키 또는 버퍼 가득참
            if (key == SEND || idx >= 16) { 
                is_send = 1;                 // 전송 준비 완료
            } 
            // 숫자 키 입력 처리
            else if (key >= '0' && key <= '9') {
                if (idx < 16) {              // 버퍼 오버플로우 방지
                    input_buf[idx++] = key;  // 버퍼에 문자 추가
                    input_buf[idx] = '\0';   // NULL 종료자 추가
                    lcd_write_line2(input_buf); // LCD에 현재 입력 표시
                }
            }
            // 종료 키 처리
            else if(key == END_SIGN){
                keepRunning = 0;             // 메인 루프 종료 플래그
            }
            
            pthread_mutex_unlock(&buf_mutex); // 뮤텍스 해제
            usleep(200000);                  // 디바운싱: 200ms 대기
        }
        usleep(10000);                       // 스캔 주기: 10ms
    }
    return NULL;
}