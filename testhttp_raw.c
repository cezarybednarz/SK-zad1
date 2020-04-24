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

#define GET_SIZE    66000
#define BUFFER_SIZE 65000
#define SA struct sockaddr 

// ./testhttp_raw www.mimuw.edu.pl:80 ciasteczka.txt http://www.mimuw.edu.pl/

static const char *OK_CODE = "HTTP/1.1 200";
static const char *ENCODING = "Transfer-Encoding: ";
static const char *COOKIE = "Set-Cookie: ";
static const char *CHUNKED = "chunked";

char *conn_addr;
char *port;
char *cookies;
char *http_addr;
char *file_addr;      // adres pliku adresu http
char *host_addr;      // host (inny niż ip)

char* response;         // whole response from server
size_t response_length; // length of response
char* response_body;    // body of the response


bool chunked = false;


void parse_command(char *argv[]) {
    
    bool colon = false;
    for (size_t i = 0; i < strlen(argv[1]); i++) {
        if (argv[1][i] == ':') {
            argv[1][i] = '\0';
            port = &argv[1][i + 1];
            colon = true;
        }
    }
    if (!colon) { 
        syserr("please enter valid connection address: <address>:<port>");
    }
    
    conn_addr = argv[1];
    
    /*if (conn_addr[0] < '0' || conn_addr[0] > '9') {
        struct hostent *hstnm;
        hstnm = gethostbyname(conn_addr);
        if (hstnm == 0) {
            syserr("wrong host name");
        }
        printf("[dbg] zamieniam: %s\n", conn_addr);
        conn_addr = inet_ntoa(*(struct in_addr *)hstnm->h_name); // to chyba nie dziala
        printf("[dbg] na: %s\n", conn_addr);
    }*/
    
    cookies = argv[2];
    http_addr = argv[3];
    
    host_addr = malloc(sizeof(char) * BUFFER_SIZE);
    bzero(host_addr, BUFFER_SIZE);
    
    int slash = 0;
    int i = 0;
    while (i < strlen(argv[3])) {
        if(argv[3][i] == '/') {
            slash++;
        }
        if(slash == 2) { 
            i++;
            int first = i;
            while (i < strlen(argv[3]) && argv[3][i] != '/')  {
                host_addr[i - first] = argv[3][i];
                i++;
            }
            file_addr = &argv[3][i];
            break;
        }
        i++;
    }
}


void send_get_request(int sockfd) { // trzeba dodać cookies
    char sendline[GET_SIZE + 1] = {0};
    
    snprintf(sendline, GET_SIZE, 
        "GET %s HTTP/1.1\r\n"
        "Host: %s\r\n", file_addr, host_addr);
    
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
    
    //printf("[dbg] wysyłam zapytanie:\n%s\n", sendline);
    
    fclose(file);
}


void read_response(int sockfd) {
    FILE *stream;
    size_t len;
    stream = open_memstream(&response, &len);
    
    char readline[BUFFER_SIZE + 1];
    int length = 0;
    
    do {
        bzero(readline, sizeof(readline)); 
        length = read(sockfd, readline, sizeof(readline));
        if (length < 0) {
            syserr("bad read");
        }
        for (int i = 0; i < length; i++) {
            fprintf(stream, "%c", readline[i]);
        }
    } while (length);
    
    fflush(stream);
    
    response_length = len;
    
    fclose(stream);
}


void parse_header(int sockfd) {
    
    if (strncmp(response, OK_CODE, strlen(OK_CODE)) != 0) { 
        int i = strlen(OK_CODE) - 4;
        while (response[i++] != '\r') {
            printf("%c", response[i]);
        }
        return;
    }
    
    
    int i = 0;
    while (!(response[i] == '\r' && response[i + 1] == '\n' &&
              response[i + 2] == '\r' && response[i + 3] == '\n')) {
        
        if (strncmp(&response[i], COOKIE, strlen(COOKIE)) == 0) { // Set-Cookie:
            int j = i + strlen(COOKIE);
            
            while (response[j] != ';' && response[j] != ',' && response[j] != '\r') {
                printf("%c", response[j++]);
            }
            printf("\n");
        }

        if (strncmp(&response[i], ENCODING, strlen(ENCODING)) == 0) { // Transfer-Encoding:
            if (strncmp(&response[i + strlen(ENCODING)], CHUNKED, strlen(CHUNKED)) == 0) {
                chunked = true;
                //printf("[dbg] chunked!\n");
            }
        }
        
        i++;
    }
    
    response_body = &response[i+4];
}


int size_of_body(int sockfd) {
    int ret = 0;
    
    int i = 0;
    while (true) {
        int j = i;
        while (response_body[j] != '\r') {
            j++;
        }
        response_body[j] = '\0';
        
        
        
        int chunk_size = (int)strtol(&response_body[i], NULL, 16);
        
        //printf("\n[dbg] chunk_size = %d\n", chunk_size);
        
        if (!chunk_size) {
            break;
        }
        
        ret += chunk_size;
        
        i = j + chunk_size + 4;
    }
    
    return ret;
}

int main(int argc, char *argv[]) {
    if (argc != 4) {
        syserr("wrong number of arguments");
    }
    parse_command(argv);
    
    int sock;
    struct addrinfo addr_hints;
    struct addrinfo *addr_result;

    int err;

    // 'converting' host/port in string to struct addrinfo
    memset(&addr_hints, 0, sizeof(struct addrinfo));
    addr_hints.ai_family = AF_INET; // IPv4
    addr_hints.ai_socktype = SOCK_STREAM;
    addr_hints.ai_protocol = IPPROTO_TCP;
    err = getaddrinfo(conn_addr, port, &addr_hints, &addr_result);
    if (err == EAI_SYSTEM) { // system error
        syserr("getaddrinfo: %s", gai_strerror(err));
    }
    else if (err != 0) { // other error (host not found, etc.)
        fatal("getaddrinfo: %s", gai_strerror(err));
    }

    // initialize socket according to getaddrinfo results
    sock = socket(addr_result->ai_family, addr_result->ai_socktype, addr_result->ai_protocol);
    if (sock < 0) {
        syserr("socket");
    }

    // connect socket to the server
    if (connect(sock, addr_result->ai_addr, addr_result->ai_addrlen) < 0) {
        syserr("connect");
    }

    freeaddrinfo(addr_result);
    
    send_get_request(sock);
    
    read_response(sock);
    
    parse_header(sock);
    
    
    printf("Dlugosc zasobu: %d\n", size_of_body(sock));
    /*
    for (int i = 0; i < response_length; i++) {
        printf("%c", response[i]);
    }
    */
    
    
    free(response);
    free(host_addr);
    close(sock);
}
