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

#define BUFFER_SIZE 1024
#define SA struct sockaddr 

// ./testhttp_raw www.mimuw.edu.pl:80 ciasteczka.txt http://www.mimuw.edu.pl/

char *conn_addr;
int port;
char *cookies;
char *http_addr;
char *file_addr;      // adres pliku adresu http


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
    
// sprawdzenie czy to nazwa domeny czy adres IP
    if (conn_addr[0] == 'w') {
        struct hostent *hstnm;
        hstnm = gethostbyname(conn_addr);
        if (hstnm == 0) {
            syserr("wrong host name");
        }
        conn_addr = inet_ntoa(*(struct in_addr *)hstnm->h_name); // to chyba nie dziala
        
    }
    
    cookies = argv[2];
    http_addr = argv[3];
    
// oddzielanie pliku od adresu
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


void send_get_request(int sockfd) {
    char sendline[BUFFER_SIZE + 1], readline[BUFFER_SIZE + 1];
    
    snprintf(sendline, BUFFER_SIZE, 
        "GET %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "\r\n", file_addr, conn_addr);
    
    if (write(sockfd, sendline, sizeof(sendline)) < 0) {
        syserr("bad write to socket");
    }
    
    bzero(readline, sizeof(readline)); 
    if (read(sockfd, readline, sizeof(readline)) < 0) {
        syserr("bad read from socket");
    }
    
    printf("[dbg] wysyłam zapytanie:\n%s\n", sendline);
    
    printf("[dbg] otrzymałem dane od serwera:\n%s\n", readline);
}

int main(int argc, char *argv[]) {
// parsowanie wejścia
    if (argc != 4) {
        syserr("wrong number of arguments");
    }
    parse_command(argv);
    
    printf("[dbg] %s %d %s %s %s\n", conn_addr, port, cookies, http_addr, file_addr);
    
    
// łączenie przez tcp z serwerem http
    int sockfd;
    struct sockaddr_in servaddr;
    
// tworzenie gniazda
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
       syserr("socket creation failed"); 
    } 
    bzero(&servaddr, sizeof(servaddr));
    
// przypisz adres ip i port serwera z którym się łączę
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = inet_addr(conn_addr);  // podane w komendzie
    servaddr.sin_port = htons(port);                  // podane w komendzie
    
// połącz klienta i serwer 
    if (connect(sockfd, (SA*)&servaddr, sizeof(servaddr)) != 0) {
        syserr("connection with the server failed");
    }
    
// wyślij zapytanie GET 
    send_get_request(sockfd);
    
// zamknięcie gniazda (koniec komunikacji)
    close(sockfd);
}
