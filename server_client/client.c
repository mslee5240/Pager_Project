#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <signal.h>

#include "keypad.h"   // keypad 입력받기 위한 함수들, lcd 관련 내용도 포함됨

#define BUFFER_SIZE 1024

int client_socket = -1;
int running = 1;

// 시그널 핸들러 - 클라이언트 종료시 소켓 정리
void handle_shutdown(int sig) {
    printf("\n클라이언트를 종료합니다...\n");
    running = 0;
    if (client_socket != -1) {
        close(client_socket);
    }
    exit(0);
}

// 서버로부터 메시지 수신 쓰레드
void *receive_messages(void *arg) {
    int socket = *(int *)arg;
    char buffer[BUFFER_SIZE];
    int bytes_received;
    
    while (running && (bytes_received = recv(socket, buffer, BUFFER_SIZE - 1, 0)) > 0) {
        buffer[bytes_received] = '\0';
        
        // 시스템 메시지나 다른 사용자의 메시지 표시
        printf("\r%s", buffer);  // \r로 현재 줄 덮어쓰기
        printf("> ");            // 프롬프트 재출력
        fflush(stdout);

        /* ============== 수신받은 메시지를 LCD에 출력 ===============*/
        if(bytes_received > 16){
            // LCD에 출력하기에 너무 긴 문자열이 온것 (welcom msg일 가능성)
            // LCD에는 출력하지 않는다
        }else{
            lcd_write_line1(buffer);
        }
        
    }
    
    if (bytes_received == 0) {
        printf("\n서버와의 연결이 종료되었습니다.\n");
    } else if (bytes_received < 0 && running) {
        perror("\n수신 오류");
    }
    
    running = 0;
    printf("\n클라이언트를 종료합니다...\n");
    return NULL;
}

// 화면 지우기 함수 (선택적)
void clear_screen() {
    printf("\033[2J\033[1;1H");
    fflush(stdout);
}

int main(int argc, char *argv[]) {
    struct sockaddr_in server_addr;
    pthread_t receive_thread;
    char server_ip[20] = "127.0.0.1";  // 기본값: localhost
    int port = 8080;
    
    // 명령줄 인자 처리
    if (argc > 1) {
        strncpy(server_ip, argv[1], sizeof(server_ip) - 1);
        server_ip[sizeof(server_ip) - 1] = '\0';
    }
    if (argc > 2) {
        port = atoi(argv[2]);
    }

    // ========= keypad 초기화 ==========
    keypad_init();

    // 키패드 스레드 시작
    pthread_t keypad_tid;
    pthread_create(&keypad_tid, NULL, keypad_thread, NULL);
    
    printf("==================================\n");
    printf("    TCP 채팅 클라이언트 v2.0\n");
    printf("==================================\n");
    printf("서버 주소: %s:%d\n", server_ip, port);
    
    // 시그널 핸들러 등록
    signal(SIGINT, handle_shutdown);
    
    // 소켓 생성
    client_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (client_socket < 0) {
        perror("소켓 생성 실패");
        exit(1);
    }
    
    // 서버 주소 설정
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    
    // IP 주소 변환
    if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0) {
        perror("잘못된 주소");
        close(client_socket);
        exit(1);
    }
    
    // 서버에 연결
    printf("서버에 연결 중...\n");
    if (connect(client_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("연결 실패");
        close(client_socket);
        exit(1);
    }
    
    printf("서버에 연결되었습니다!\n");
    printf("----------------------------------\n");
    
    // 수신 쓰레드 시작
    if (pthread_create(&receive_thread, NULL, receive_messages, &client_socket) != 0) {
        perror("수신 쓰레드 생성 실패");
        close(client_socket);
        exit(1);
    }
    
    // 잠시 대기 (서버 환영 메시지 수신)
    sleep(1);
    
    // 사용자 입력 처리
    while (running) {
        if(is_send){
            // 사용자가 keypad 입력을 끝냄 -> 서버로 메시지 전송
            // 서버로 전송
            if (send(client_socket, input_buf, strlen(input_buf), 0) < 0) {
                perror("전송 실패");
                break;
            }

            // 전송 후 keypad 문자열 정리
            clear_keypad_str();

        }
        // 입력중에는 할게 없음

        if(!keepRunning){
            // 종료 버튼을 누른 상황 -> 프로그램 종료
            printf("연결을 종료합니다...\n");
            running = 0;
            break;
        }
    }

    printf("while 탈출\n");
    
    // 정리
    running = 0;
    shutdown(client_socket, SHUT_RDWR); // running = 0을 했음에도 수신스레드가 종료되지 않는 현상때문에 추가
    close(client_socket);
    printf("소켓 닫기 완료\n");

    pthread_join(keypad_tid, NULL);
    printf("키패드스레드 종료\n");
    
    pthread_join(receive_thread, NULL);
    printf("수신스레드 종료\n");
    
    
    return 0;
}