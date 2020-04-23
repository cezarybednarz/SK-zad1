#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <netdb.h>

#include "err.h"

#define BUFFER_SIZE 65000
#define SA struct sockaddr 

// ./testhttp_raw www.mimuw.edu.pl:80 ciasteczka.txt http://www.mimuw.edu.pl/

static const char *OK_CODE = "HTTP/1.1 200";
static const char *ENCODING = "Transfer-Encoding: ";
static const char *COOKIE = "Set-Cookie: ";
static const char *CHUNKED = "chunked";

char *conn_addr;
int port;
char *cookies;
char *http_addr;
char *file_addr;      // adres pliku adresu http

bool chunked = false;


void parse_command(char *argv[]) {
    
    bool colon = false;
    for (size_t i = 0; i < strlen(argv[1]); i++) {
        if (argv[1][i] == ':') {
            argv[1][i] = '\0';
            port = atoi(&argv[1][i + 1]);
            colon = true;
        }
    }
    if (!colon) { 
        syserr("please enter valid connection address: <address>:<port>");
    }
    
    conn_addr = argv[1];
    
    if (conn_addr[0] < '0' || conn_addr[0] > '9') {
        struct hostent *hstnm;
        hstnm = gethostbyname(conn_addr);
        if (hstnm == 0) {
            syserr("wrong host name");
        }
        printf("[dbg] zamieniam: %s\n", conn_addr);
        conn_addr = inet_ntoa(*(struct in_addr *)hstnm->h_name); // to chyba nie dziala
        printf("[dbg] na: %s\n", conn_addr);
    }
    
    cookies = argv[2];
    http_addr = argv[3];
    
    int slash = 0;
    for(size_t i = 0; i < strlen(argv[3]); i++) {
        if(argv[3][i] == '/') {
            slash++;
        }
        if(slash == 3) {
            file_addr = &argv[3][i];
            break;
        }
    }
}


void send_get_request(int sockfd) { // trzeba dodać cookies
    char sendline[BUFFER_SIZE + 1];
    
    snprintf(sendline, BUFFER_SIZE, 
        "GET %s HTTP/1.1\r\n"
        "Host: %s\r\n", file_addr, conn_addr);
    
    FILE *file = fopen(cookies, "r");
    
    
    char c;
    bool new_line = true;
    int i = strlen(sendline);
    while (fscanf(file, "%c", &c) != EOF) {
        if (new_line) {
            strncat(sendline, COOKIE, strlen(COOKIE));
            i += strlen(COOKIE);
        }
        if (c == '\n') {
            new_line = true;
            sendline[i++] = '\r';
            sendline[i++] = '\n';
        }
        else {
            new_line = false;
            sendline[i++] = c;
        }
    }
    
    strncat(sendline, "\r\n", strlen("\r\n"));
    
    if (write(sockfd, sendline, sizeof(sendline)) < 0) {
        syserr("bad write to socket");
    }
    
    printf("[dbg] wysyłam zapytanie:\n%s\n", sendline);

}

void receive_first_chunk(int sockfd) {
    char readline[BUFFER_SIZE + 1];
    
    bzero(readline, sizeof(readline)); 
    if (read(sockfd, readline, sizeof(readline)) < 0) {
        syserr("bad read from socket");
    }
    
    if (strncmp(readline, OK_CODE, strlen(OK_CODE)) != 0) { 
        int i = strlen(OK_CODE) - 4;
        while (readline[i++] != '\r') {
            printf("%c", readline[i]);
        }
        return;
    }
    
    //printf("[dbg] otrzymałem dane od serwera:\n%s\n", readline);
    
    int i = 0;
    while (!(readline[i] == '\r' && readline[i + 1] == '\n' &&
              readline[i + 2] == '\r' && readline[i + 3] == '\n')) {
        
        if (strncmp(&readline[i], COOKIE, strlen(COOKIE)) == 0) { // Set-Cookie:
            int j = i + strlen(COOKIE);
            
            while (readline[j] != ';' && readline[j] != '\r') {
                printf("%c", readline[j++]);
            }
        }

        if (strncmp(&readline[i], ENCODING, strlen(ENCODING)) == 0) { // Transfer-Encoding:
            if (strncmp(&readline[i + strlen(ENCODING)], CHUNKED, strlen(CHUNKED)) == 0) {
                chunked = true;
            }
        }
        
        i++;
    }
}


void receive_following_chunk(int sockfd) {
    
}


int main(int argc, char *argv[]) {
    if (argc != 4) {
        syserr("wrong number of arguments");
    }
    parse_command(argv);
    printf("[dbg] %s %d %s %s %s\n", conn_addr, port, cookies, http_addr, file_addr);
    
    
    int sockfd;
    struct sockaddr_in servaddr;
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
       syserr("socket creation failed"); 
    } 
    bzero(&servaddr, sizeof(servaddr));
    
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = inet_addr(conn_addr);  // podane w komendzie
    servaddr.sin_port = htons(port);                  // podane w komendzie
    
    if (connect(sockfd, (SA*)&servaddr, sizeof(servaddr)) != 0) {
        syserr("connection with the server failed");
    }
    
    send_get_request(sockfd);
    
    receive_first_chunk(sockfd);
    
    close(sockfd);
}
