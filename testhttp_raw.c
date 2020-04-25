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

#define GET_SIZE    6500
#define BUFFER_SIZE 6500
#define SA struct sockaddr 

// ./testhttp_raw www.mimuw.edu.pl:80 ciasteczka.txt http://www.mimuw.edu.pl/

static const char *OK_CODE = "HTTP/1.1 200";
static const char *ENCODING = "Transfer-Encoding: ";
static const char *COOKIE = "Set-Cookie: ";
static const char *CHUNKED = "chunked";
static const char *CONTENT = "Content-Length: ";
static const char *SEND_COOKIE = "Cookie: ";

char *conn_addr;
char *port;
char *cookies;
char *http_addr;
char *file_addr;      // adres pliku adresu http
char *host_addr;      // host (inny niż ip)

bool chunked = false;
int content_length = 0; 


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
        syserr("not valid address");
    }
    
    conn_addr = argv[1];
    
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


void send_get_request(int sockfd) { 
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
            strncat(sendline, SEND_COOKIE, strlen(SEND_COOKIE));
            i += strlen(SEND_COOKIE);
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
    
    fclose(file);
}

/* ===== używanie streama po sockecie ===== */
bool read_header(FILE *stream) {
    char *line_buf = NULL;
    size_t line_buf_size = 0;
    ssize_t line_size = 0;
    
    line_size = getline(&line_buf, &line_buf_size, stream);
    
    
    // check if response is "200 OK"
    if (strncmp(line_buf, OK_CODE, strlen(OK_CODE)) != 0) { 
        int i = 0;
        while (line_buf[i] != '\r') {
            printf("%c", line_buf[i]);
            i++;
        }
        printf("\n");
        free(line_buf);
        return false;
    }
    
    
    do {
        line_size = getline(&line_buf, &line_buf_size, stream);
        
        if (strncmp(line_buf, "\r\n", strlen("\r\n")) == 0) { // end of header
            break;
        }
        else if (strncmp(line_buf, ENCODING, strlen(ENCODING)) == 0) { // Transfer-Encoding:
            if (strncmp(&line_buf[strlen(ENCODING)], CHUNKED, strlen(CHUNKED)) == 0) {
                chunked = true;
            }
        }
        else if (strncmp(line_buf, CONTENT, strlen(CONTENT)) == 0) { // Content-length:
            int i = strlen(CONTENT);
            while (line_buf[i] != '\r') { 
                i++;
            }
            line_buf[i] = '\0';
            content_length = atoi(&line_buf[strlen(CONTENT)]);
        }
        else if (strncmp(line_buf, COOKIE, strlen(COOKIE)) == 0) { // Set-Cookie:
            int i = strlen(COOKIE);
            while (line_buf[i] != '\r' && line_buf[i] != ';' && line_buf[i] != ',') {
                printf("%c", line_buf[i++]);
            }
            printf("\n");
        }
        
    } while (line_size);
    
    
    free(line_buf);
    return true;
}

void read_body_chunked(FILE *stream) {
    char *line_buf = NULL;
    size_t line_buf_size = 0;
    ssize_t line_size = 0;
    
    do {
        line_size = getline(&line_buf, &line_buf_size, stream);
        int i = 0;
        while (line_buf[i] != '\r') {
            i++;
        }
        line_buf[i] = '\0';
        
        int chunk_size = (int)strtol(line_buf, NULL, 16);
    
        if (chunk_size == 0) { // last chunk
            break;
        }
        
        content_length += chunk_size;
        
        for (i = 0; i < chunk_size + 2; i++) { // skip chunk_size + 2 bytes
            fgetc(stream);
        }
    } while (line_size > 0);
    
    
    free(line_buf);
}

void read_body_not_chunked(FILE *stream) {
    for (int i = 0; i < content_length; i++) {
        fgetc(stream);
    }
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
    
    FILE *fp = fdopen(sock, "r");
    if (!fp) {
        syserr("failed fdopen");
    }
    
    
    send_get_request(sock);
    
    
    if (read_header(fp)) {
        if (chunked) {
            read_body_chunked(fp);
        }
        else {
            read_body_not_chunked(fp);
        }
        printf("Dlugosc zasobu: %d\n", content_length);
    }    
    
    
    free(host_addr);
    fclose(fp);
    close(sock);
}
