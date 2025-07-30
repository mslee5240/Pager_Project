#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>

#define PORT 8080
#define MAX_CLIENTS 100 // 수정 가능
#define BUFFER_SIZE 1024
#define NAME_SIZE 32

// 클라이언트 정보 구조체
typedef struct {
    int socket;
    int id;
    char name[NAME_SIZE];
    struct sockaddr_in address;
    int active;
} client_info;

// 전역 변수
int server_socket = -1;
client_info clients[MAX_CLIENTS];
pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;

// 시그널 핸들러 - 서버 종료시 소켓 정리
void handle_shutdown(int sig) {
    printf("\n서버를 종료합니다...\n");
    if (server_socket != -1) {
        close(server_socket);
    }
    exit(0);
}

// 모든 활성 클라이언트에게 메시지 브로드캐스트
void broadcast_message(char *message, int sender_id) {
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].active && clients[i].id != sender_id) {
            if (send(clients[i].socket, message, strlen(message), 0) < 0) {
                perror("브로드캐스트 전송 실패");
            }
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}

// 특정 클라이언트에게 메시지 전송
void send_to_client(char *message, int target_id, int sender_id) {
    pthread_mutex_lock(&clients_mutex);
    int found = 0;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].active && clients[i].id == target_id) {
            if (send(clients[i].socket, message, strlen(message), 0) < 0) {
                perror("개인 메시지 전송 실패");
            }
            found = 1;
            break;
        }
    }
    
    // 발신자에게 전송 결과 알림
    if (!found && sender_id > 0) {
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i].active && clients[i].id == sender_id) {
                char error_msg[100];
                sprintf(error_msg, "[시스템] 클라이언트 %d를 찾을 수 없습니다.\n", target_id);
                send(clients[i].socket, error_msg, strlen(error_msg), 0);
                break;
            }
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}

// 활성 클라이언트 목록 생성
void get_client_list(char *buffer) {
    pthread_mutex_lock(&clients_mutex);
    sprintf(buffer, "\n=== 연결된 클라이언트 목록 ===\n");
    int count = 0;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].active) {
            char temp[100];
            sprintf(temp, "ID: %d, 이름: %s, IP: %s\n", 
                    clients[i].id, 
                    clients[i].name,
                    inet_ntoa(clients[i].address.sin_addr));
            strcat(buffer, temp);
            count++;
        }
    }
    char summary[50];
    sprintf(summary, "총 %d명 접속 중\n", count);
    strcat(buffer, summary);
    pthread_mutex_unlock(&clients_mutex);
}

// 클라이언트 추가
int add_client(int socket, struct sockaddr_in address) {
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (!clients[i].active) {
            clients[i].socket = socket;
            clients[i].address = address;
            clients[i].active = 1;
            clients[i].id = i + 1;
            sprintf(clients[i].name, "User%d", clients[i].id);
            pthread_mutex_unlock(&clients_mutex);
            return i;
        }
    }
    pthread_mutex_unlock(&clients_mutex);
    return -1;
}

// 클라이언트 제거
void remove_client(int index) {
    pthread_mutex_lock(&clients_mutex);
    clients[index].active = 0;
    close(clients[index].socket);
    pthread_mutex_unlock(&clients_mutex);
}

// 클라이언트 처리 쓰레드 함수
void *handle_client(void *arg) {
    int index = *(int *)arg;
    free(arg);
    
    client_info *client = &clients[index];
    char buffer[BUFFER_SIZE];
    int bytes_received;
    
    printf("[클라이언트 %d] 연결됨 - IP: %s, Port: %d\n", 
           client->id,
           inet_ntoa(client->address.sin_addr), 
           ntohs(client->address.sin_port));
    
    // 환영 메시지 및 명령어 안내
    char welcome_msg[500];
    sprintf(welcome_msg, 
            "\n=== 채팅 서버에 오신 것을 환영합니다! ===\n"
            "당신의 ID: %d, 이름: %s\n"
            "\n[명령어]\n"
            "/name <이름> - 이름 변경\n"
            "/list - 접속자 목록\n"
            "/msg <ID> <메시지> - 개인 메시지\n"
            "/all <메시지> - 전체 메시지\n"
            "/quit - 종료\n"
            "그 외 입력은 모두에게 전송됩니다.\n"
            "=====================================\n",
            client->id, client->name);
    send(client->socket, welcome_msg, strlen(welcome_msg), 0);
    
    // 다른 사용자들에게 입장 알림
    char join_msg[100];
    sprintf(join_msg, "[시스템] %s(ID:%d)님이 입장하셨습니다.\n", client->name, client->id);
    broadcast_message(join_msg, client->id);
    
    // 클라이언트로부터 메시지 수신
    while ((bytes_received = recv(client->socket, buffer, BUFFER_SIZE - 1, 0)) > 0) {
        buffer[bytes_received] = '\0';
        
        // 개행 문자 제거
        char *newline = strchr(buffer, '\n');
        if (newline) *newline = '\0';
        
        // printf("[클라이언트 %d] %s: %s\n", client->id, client->name, buffer);
        printf("%s\n", buffer);
        
        // 명령어 처리
        if (buffer[0] == '/') {
            if (strncmp(buffer, "/quit", 5) == 0) {
                printf("[클라이언트 %d] 연결 종료 요청\n", client->id);
                break;
            }
            else if (strncmp(buffer, "/name ", 6) == 0) {
                // 이름 변경
                char old_name[NAME_SIZE];
                strcpy(old_name, client->name);
                strncpy(client->name, buffer + 6, NAME_SIZE - 1);
                client->name[NAME_SIZE - 1] = '\0';
                
                char name_msg[200];
                sprintf(name_msg, "[시스템] %s님이 이름을 %s(으)로 변경했습니다.\n", old_name, client->name);
                broadcast_message(name_msg, -1);
            }
            else if (strncmp(buffer, "/list", 5) == 0) {
                // 접속자 목록
                char list_buffer[1024];
                get_client_list(list_buffer);
                send(client->socket, list_buffer, strlen(list_buffer), 0);
            }
            else if (strncmp(buffer, "/msg ", 5) == 0) {
                // 개인 메시지
                int target_id;
                char msg_content[BUFFER_SIZE];
                if (sscanf(buffer + 5, "%d %[^\n]", &target_id, msg_content) == 2) {
                    char private_msg[BUFFER_SIZE + 100];
                    sprintf(private_msg, "[귓속말 from %s(ID:%d)] %s\n", client->name, client->id, msg_content);
                    send_to_client(private_msg, target_id, client->id);
                    
                    // 발신자에게 확인 메시지
                    sprintf(private_msg, "[귓속말 to ID:%d] %s\n", target_id, msg_content);
                    send(client->socket, private_msg, strlen(private_msg), 0);
                } else {
                    char error_msg[] = "[시스템] 사용법: /msg <ID> <메시지>\n";
                    send(client->socket, error_msg, strlen(error_msg), 0);
                }
            }
            else if (strncmp(buffer, "/all ", 5) == 0) {
                // 전체 메시지 (명시적)
                char broadcast_msg[BUFFER_SIZE + 100];
                sprintf(broadcast_msg, "[전체] %s(ID:%d): %s\n", client->name, client->id, buffer + 5);
                broadcast_message(broadcast_msg, client->id);
                send(client->socket, broadcast_msg, strlen(broadcast_msg), 0);
            }
            else {
                // 알 수 없는 명령어
                char help_msg[] = "[시스템] 알 수 없는 명령어입니다. /help로 도움말을 확인하세요.\n";
                send(client->socket, help_msg, strlen(help_msg), 0);
            }
        }
        else {
            // 일반 메시지는 모든 사용자에게 전송
            char chat_msg[BUFFER_SIZE + 100];
            // sprintf(chat_msg, "%s(ID:%d): %s\n", client->name, client->id, buffer);
            sprintf(chat_msg, "%s\n", buffer);  // 이름과 ID 제거, 메시지만 전송
            broadcast_message(chat_msg, client->id);
            
            // 발신자에게도 자신의 메시지 표시
            //send(client->socket, chat_msg, strlen(chat_msg), 0);
        }
    }
    
    // 클라이언트 연결 종료
    printf("[클라이언트 %d] %s 연결 종료\n", client->id, client->name);
    
    // 퇴장 알림
    char leave_msg[100];
    sprintf(leave_msg, "[시스템] %s(ID:%d)님이 퇴장하셨습니다.\n", client->name, client->id);
    broadcast_message(leave_msg, client->id);
    
    // 클라이언트 제거
    remove_client(index);
    
    return NULL;
}

int main() {
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    pthread_t thread_id;
    
    // 시그널 핸들러 등록
    signal(SIGINT, handle_shutdown);
    
    // 클라이언트 배열 초기화
    memset(clients, 0, sizeof(clients));
    
    // 소켓 생성
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        perror("소켓 생성 실패");
        exit(1);
    }
    
    // SO_REUSEADDR 옵션 설정
    int opt = 1;
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt 실패");
        exit(1);
    }
    
    // 서버 주소 설정
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);
    
    // 바인드
    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("바인드 실패");
        close(server_socket);
        exit(1);
    }
    
    // 리슨
    if (listen(server_socket, MAX_CLIENTS) < 0) {
        perror("리슨 실패");
        close(server_socket);
        exit(1);
    }
    
    printf("채팅 서버가 포트 %d에서 시작되었습니다.\n", PORT);
    printf("클라이언트 연결을 기다리는 중...\n");
    
    // 클라이언트 연결 수락 루프
    while (1) {
        int client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_len);
        
        if (client_socket < 0) {
            perror("클라이언트 연결 수락 실패");
            continue;
        }
        
        // 클라이언트 추가
        int index = add_client(client_socket, client_addr);
        if (index < 0) {
            printf("최대 클라이언트 수에 도달했습니다.\n");
            char full_msg[] = "서버가 가득 찼습니다. 나중에 다시 시도해주세요.\n";
            send(client_socket, full_msg, strlen(full_msg), 0);
            close(client_socket);
            continue;
        }
        
        // 새 쓰레드에서 클라이언트 처리
        int *arg = malloc(sizeof(int));
        *arg = index;
        
        if (pthread_create(&thread_id, NULL, handle_client, arg) != 0) {
            perror("쓰레드 생성 실패");
            remove_client(index);
            free(arg);
            continue;
        }
        
        // 쓰레드 분리
        pthread_detach(thread_id);
    }
    
    close(server_socket);
    return 0;
}