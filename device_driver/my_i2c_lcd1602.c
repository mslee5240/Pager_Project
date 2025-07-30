// I2C LCD1602 Character Display Driver

// 이 드라이버는 I2C 백팩을 사용하는 16x2 캐릭터 LCD를 제어
// I2C 백팩은 PCF8574 칩을 기반으로 하며, 4비트 병렬 통신을 I2C로 변환

// I2C 백팩 핀 배치 (PCF8574):
// P7 P6 P5 P4 P3 P2 P1 P0
// D7 D6 D5 D4 BL EN RW RS

// - D7~D4: LCD 데이터 핀 (4비트 모드)
// - BL: 백라이트 제어 (1=ON, 0=OFF)
// - EN: Enable 신호 (LCD 데이터 래치용)
// - RW: Read/Write 선택 (0=Write, 1=Read)
// - RS: Register Select (0=Command, 1=Data)

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/delay.h>

/* 드라이버 및 디바이스 정보 */
#define DEV_NAME            "my_i2c_lcd1602"     // 캐릭터 디바이스 이름
#define I2C_BUS_NUM         1                    // 사용할 I2C 버스 번호 (I2C-1)
#define I2C_LCD1602_ADDR    0x27                 // LCD I2C 백팩의 슬레이브 주소

/* I2C 백팩 제어 비트 정의 (PCF8574 기준) */
#define I2C_ENABLE      0x04                     // Enable 비트 (P2) - LCD 데이터 래치 신호
#define I2C_DATA        0x08                     // 백라이트 ON + RS=1 (데이터 모드)
#define I2C_COMMAND     0x09                     // 백라이트 ON + RS=0 (명령어 모드) - 실제로는 0x08이어야 함

/* 전역 변수 - I2C 및 캐릭터 디바이스 관리 */
static struct i2c_adapter* i2c_adap;             // I2C 어댑터 포인터
static struct i2c_client* i2c_client;            // I2C 클라이언트 디바이스 포인터
static int major_num;                            // 캐릭터 디바이스 주 번호

/* 함수 프로토타입 선언 */
void i2c_send_data(uint8_t data);
void i2c_send_command(uint8_t data);
void I2C_LCD_write_string(char *string);
void I2C_LCD_goto_XY(uint8_t row, uint8_t col);
void I2C_LCD_write_string_XY(uint8_t row, uint8_t col, char *string);
void lcd_init_seq(void);

// I2C_LCD_write_string - LCD에 문자열 출력
// - @string: 출력할 문자열 포인터
// - 현재 커서 위치부터 문자열을 순차적으로 출력합니다.
// - ASCII 32 미만의 제어 문자는 무시합니다 (\n, \t 등).
void I2C_LCD_write_string(char *string)
{
	uint8_t// 
	for(i=0; string[i]; i++){
        // ASCII 32 미만은 출력 가능한 문자가 아니므로 건너뜀
        if(string[i] < 32){
            continue;
            // \n, \t 같은 제어문자들은 LCD에서 출력불가
        }
        i2c_send_data(string[i]);           // 각 문자를 데이터로 전송
    }
}


// I2C_LCD_goto_XY - LCD 커서를 지정된 위치로 이동
// - @row: 행 번호 (0 또는 1)
// - @col: 열 번호 (0~15)
// 
// 16x2 LCD의 DDRAM 주소 구조:
// - 첫 번째 행: 0x00~0x0F (0x80+0x00 ~ 0x80+0x0F)
// - 두 번째 행: 0x40~0x4F (0x80+0x40 ~ 0x80+0x4F)
void I2C_LCD_goto_XY(uint8_t row, uint8_t col)
{
	col %= 16;                              // 열 번호를 0~15로 제한
	row %= 2;                               // 행 번호를 0~1로 제한
	
	uint8_t address = (0x40 * row) + col;   // DDRAM 주소 계산
	uint8_t command = 0x80 + address;       // Set DDRAM Address 명령어 (0x80 | address)
	
    i2c_send_command(command);              // 커서 이동 명령어 전송
}


// I2C_LCD_write_string_XY - 지정된 위치에 문자열 출력
// - @row: 출력할 행 번호
// - @col: 출력할 열 번호  
// - @string: 출력할 문자열
// - 커서를 지정된 위치로 이동한 후 문자열을 출력합니다.
void I2C_LCD_write_string_XY(uint8_t row, uint8_t col, char *string)
{
	I2C_LCD_goto_XY(row, col);             // 커서를 목표 위치로 이동
	I2C_LCD_write_string(string);          // 문자열 출력
}


// i2c_send_data - LCD에 데이터 바이트 전송 (4비트 모드)
// - @data: 전송할 8비트 데이터
// - 8비트 데이터를 상위 4비트와 하위 4비트로 나누어 두 번에 걸쳐 전송합니다.
// - 각 4비트 전송 시 Enable 신호를 High→Low로 토글하여 LCD가 데이터를 래치하도록 합니다.
// 
// - 전송 순서:
// - 1. 상위 4비트 + 제어비트 + Enable=1
// - 2. 상위 4비트 + 제어비트 + Enable=0  
// - 3. 하위 4비트 + 제어비트 + Enable=1
// - 4. 하위 4비트 + 제어비트 + Enable=0
void i2c_send_data(uint8_t data){
    uint8_t tmp[4];

    // 상위 4비트 추출 (aaaa0000 형태)
    uint8_t hnibble = (data & 0xF0);
    tmp[0] = hnibble|0x0D;                  // 상위4비트 + 백라이트ON + Enable=1 + RS=1 (데이터)
    tmp[1] = hnibble|0x09;                  // 상위4비트 + 백라이트ON + Enable=0 + RS=1

    // 하위 4비트를 상위로 이동 (bbbb0000 형태)
    uint8_t lnibble = ((data << 4) & 0xF0);
    tmp[2] = lnibble|0x0D;                  // 하위4비트 + 백라이트ON + Enable=1 + RS=1 (데이터)
    tmp[3] = lnibble|0x09;                  // 하위4비트 + 백라이트ON + Enable=0 + RS=1
    
    i2c_master_send(i2c_client, tmp, 4);    // I2C로 4바이트 연속 전송
}


// i2c_send_command - LCD에 명령어 바이트 전송 (4비트 모드)
// - @data: 전송할 8비트 명령어
// - 데이터 전송과 동일하지만 RS=0으로 설정하여 명령어 모드로 전송
// - LCD 초기화, 화면 클리어, 커서 이동 등의 제어 명령어를 전송할 때 사용
void i2c_send_command(uint8_t data){
    uint8_t tmp[4];

    // 상위 4비트 추출
    uint8_t hnibble = (data & 0xF0);
    tmp[0] = hnibble|0x0C;                  // 상위4비트 + 백라이트ON + Enable=1 + RS=0 (명령어)
    tmp[1] = hnibble|0x08;                  // 상위4비트 + 백라이트ON + Enable=0 + RS=0

    // 하위 4비트를 상위로 이동
    uint8_t lnibble = ((data << 4) & 0xF0);
    tmp[2] = lnibble|0x0C;                  // 하위4비트 + 백라이트ON + Enable=1 + RS=0 (명령어)
    tmp[3] = lnibble|0x08;                  // 하위4비트 + 백라이트ON + Enable=0 + RS=0
    
    i2c_master_send(i2c_client, tmp, 4);    // I2C로 4바이트 연속 전송
}


// lcd_init_seq - LCD 초기화 시퀀스 실행
// 
// HD44780 계열 LCD의 표준 초기화 절차를 수행:
// 1. 전원 안정화 대기 후 8비트 모드로 3번 초기화 시도
// 2. 4비트 모드로 전환  
// 3. 기능 설정 (4비트, 2라인, 5x8 폰트)
// 4. 디스플레이 제어 설정
// 5. 화면 클리어 및 엔트리 모드 설정
// 
// 초기화 명령어 설명:
// - 0x33: Function Set (8-bit mode) - 확실한 8비트 모드 설정
// - 0x32: Function Set (4-bit mode) - 4비트 모드로 전환
// - 0x28: Function Set (4-bit, 2-line, 5x8) - 최종 디스플레이 설정
// - 0x08: Display OFF - 초기화 중 화면 끄기
// - 0x0C: Display ON, Cursor OFF, Blink OFF - 디스플레이 켜기  
// - 0x01: Clear Display - 화면 지우기 및 커서 홈 위치로
// - 0x06: Entry Mode Set - 커서 자동 오른쪽 이동, 화면 시프트 없음
void lcd_init_seq(void){
    // HD44780 표준 초기화 시퀀스
    i2c_send_command(0x33);                 // Function Set: 8-bit mode (첫 번째 시도)
    i2c_send_command(0x32);                 // Function Set: 4-bit mode로 전환
    i2c_send_command(0x28);                 // Function Set: 4-bit, 2-line, 5x8 dots
    i2c_send_command(0x08);                 // Display Control: display off
    i2c_send_command(0x0C);                 // Display Control: display on, cursor off, blink off
    i2c_send_command(0x01);                 // Clear Display: 화면 클리어 및 커서 홈
    i2c_send_command(0x06);                 // Entry Mode Set: increment cursor, no shift
    msleep(2000);                           // 초기화 완료 대기 (2초)
}


// dev_write - 캐릭터 디바이스 write 시스템 콜 핸들러
// - @file: 파일 구조체 포인터 (사용하지 않음)
// - @buf: 사용자 공간의 데이터 버퍼
// - @len: 쓸 데이터의 길이
// - @offset: 파일 오프셋 (사용하지 않음)
// - 사용자 애플리케이션에서 write() 시스템 콜로 LCD에 텍스트를 출력할 때 호출
// 
// 프로토콜:
// - 첫 번째 문자가 '0': 첫 번째 행에 출력 
// - 첫 번째 문자가 '1': 두 번째 행에 출력
// - 그 외: 화면을 지우고 "bbi bbi!!!!" 출력
// 
// 예시: echo "0Hello World" > /dev/mylcd  (첫 번째 행에 "Hello World" 출력)
static ssize_t dev_write (struct file *file, const char __user *buf, size_t len, loff_t *offset)
{
    char kbuf[19] = {0, };                  // 커널 버퍼 (최대 18문자 + NULL)

    // 사용자 공간에서 커널 공간으로 데이터 복사
    if (copy_from_user(kbuf, buf, len+1)) return -EFAULT;

    // 문자열 뒷부분을 공백으로 패딩 (LCD 디스플레이 정리용)
    for(int i = strlen(kbuf); i<=16; i++){
        kbuf[i] = ' ';
    }
    kbuf[16] = 0;                           // NULL 종료자 추가

    // 첫 번째 문자에 따른 출력 위치 결정
    if(kbuf[0] == '0')          I2C_LCD_write_string_XY(0, 0, kbuf + 1);  // 첫 번째 행
    else if(kbuf[0] == '1')     I2C_LCD_write_string_XY(1, 0, kbuf + 1);  // 두 번째 행
    else{
        i2c_send_command(0x01);             // 디스플레이 clear 명령어
        I2C_LCD_write_string_XY(0, 0, "bbi bbi!!!!");  // 기본 메시지 출력
    }
    
    return len;                             // 처리된 바이트 수 반환
}

/* 캐릭터 디바이스 파일 오퍼레이션 구조체 */
static struct file_operations fops = {
    .owner = THIS_MODULE,                   // 모듈 소유권
    .write = dev_write,                     // write 시스템 콜 핸들러
};


// lcd_init - 모듈 초기화 함수 
// 
// 모듈이 로드될 때 자동으로 호출되며 다음 작업을 수행:
// 1. I2C 어댑터 획득 (지정된 버스 번호)
// 2. I2C 클라이언트 디바이스 생성 및 등록
// 3. 캐릭터 디바이스 등록 (동적 주 번호 할당)
// 4. LCD 하드웨어 초기화
// 5. 초기 메시지 출력
static int __init lcd_init(void)
{
    /* I2C 보드 정보 구조체 - 디바이스 이름과 주소 설정 */
    struct i2c_board_info board_info = {
        I2C_BOARD_INFO("lcd", I2C_LCD1602_ADDR)
    };

    // 1. 지정된 번호의 I2C 어댑터 획득
    i2c_adap = i2c_get_adapter(I2C_BUS_NUM);
    if (!i2c_adap) {
        pr_err("I2C adapter not found\n");
        return -ENODEV;                     // 디바이스 없음 에러 반환
    }

    // 2. I2C 클라이언트 디바이스 생성 및 어댑터에 등록
    i2c_client = i2c_new_client_device(i2c_adap, &board_info);
    if (!i2c_client) {
        pr_err("Device registration failed\n");
        i2c_put_adapter(i2c_adap);          // 실패 시 어댑터 해제
        return -ENODEV;
    }

    // 3. 캐릭터 디바이스 등록 (주 번호 동적 할당)
    major_num = register_chrdev(0, DEV_NAME, &fops);
    if (major_num < 0) {
        pr_err("Device registration failed\n");
        return major_num;                   // 에러 코드 반환
    }
    pr_info("Major number: %d\n", major_num);

    // 4. 하드웨어 초기화 준비 및 LCD 초기화 실행
    msleep(15);                             // 전원 안정화 대기

    lcd_init_seq();                         // LCD 초기화 시퀀스 실행

    msleep(1000);                           // 초기화 후 안정화 대기
    
    pr_info("lcd initialized\n");

    // 5. 초기화 완료 메시지 출력
    I2C_LCD_write_string_XY(0,0,"goooood");

    return 0;                               // 성공 반환
}


// lcd_exit - 모듈 제거 함수
// 
// 모듈이 언로드될 때 자동으로 호출되며 할당된 자원들을 해제합니다.
// 주의: 현재 register_chrdev()로 등록한 캐릭터 디바이스를 해제하지 않고 있음
static void __exit lcd_exit(void)
{
    i2c_unregister_device(i2c_client);      // I2C 클라이언트 디바이스 등록 해제
    i2c_put_adapter(i2c_adap);              // I2C 어댑터 참조 해제
    pr_info("lcd removed\n");
    unregister_chrdev(major_num, DEV_NAME);
}

/* 모듈 초기화/제거 함수 등록 */
module_init(lcd_init);
module_exit(lcd_exit);

/* 모듈 정보 */  
MODULE_LICENSE("GPL");                       // GPL 라이선스
MODULE_AUTHOR("Minsoo");                     // 작성자
MODULE_DESCRIPTION("My I2C LCD1602 Driver"); // 모듈 설명