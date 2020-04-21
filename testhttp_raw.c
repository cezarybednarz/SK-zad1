#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdlib.h>
#include <arpa/inet.h>

#include "err.h"

#define SA struct sockaddr 

// ./testhttp_raw www.mimuw.edu.pl:80 ciasteczka.txt http://www.mimuw.edu.pl/

char *conn_addr;
int port;
char *cookies;
char *http_addr;

void parse_command(char *argv[]) {
    bool colon = false;
    for (size_t i = 0; i < strlen(argv[1]); i++) {
        if(argv[1][i] == ':') {
            argv[1][i] = '\0';
            conn_addr = argv[1];
            port = atoi(&argv[1][i + 1]);
            colon = true;
        }
    }
    if (!colon) { 
        syserr("please enter valid connection address: <address>:<port>");
    }
    cookies = argv[2];
    http_addr = argv[3];
}

int main(int argc, char *argv[]) {
    // parsowanie wejścia
    if (argc != 4) {
        syserr("wrong number of arguments");
    }
    parse_command(argv);
    
    
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
    
    
    // zamknięcie gniazda (koniec komunikacji)
    close(sockfd);
}
