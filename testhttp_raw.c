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
#include <ctype.h>

#include "err.h"

#define GET_SIZE    10000
#define BUFFER_SIZE 10000

static const char *OK_CODE     = "HTTP/1.1 200";
static const char *ENCODING    = "Transfer-Encoding: ";
static const char *COOKIE      = "Set-Cookie: ";
static const char *CHUNKED     = "chunked";
static const char *CONTENT     = "Content-Length: ";
static const char *SEND_COOKIE = "Cookie: ";

char *conn_addr;
char *port;
char *cookies;
char *http_addr;
char *file_addr;
char *host_addr;

bool chunked = false;
int content_length = 0; 

// case insensitive compare (returns 0 if equal, 1 otherwise)
int cmp_no_case(char *a, const char *b, size_t len) {
    if (strlen(a) < len || strlen(b) < len) {
        return 1;
    }
    for (int i = 0; i < len; i++) {
        if (tolower(a[i]) != tolower(b[i])) {
            return 1;
        }
    }
    return 0;
}


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
    if (host_addr == NULL) {
        syserr("malloc");
    }
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
            while (i < strlen(argv[3]) && argv[3][i] != '/' && argv[3][i] != '?' && 
                   argv[3][i] != ';' && argv[3][i] != '#')  {
                host_addr[i - first] = argv[3][i];
                i++;
            }
            file_addr = &argv[3][i];
            break;
        }
        i++;
    }
}


void send_get_request(int sockfd, FILE *stream) { 
    char sendline[GET_SIZE + 1] = {0};
    
    if (strlen(file_addr) == 0) { // if empty file
        file_addr = "/";
    }
    
    if (file_addr[0] != '/') {
        snprintf(sendline, GET_SIZE, 
        "GET /%s HTTP/1.1\r\nConnection: Close\r\n"
        "Host: %s\r\n", file_addr, host_addr);
    }
    else {
        snprintf(sendline, GET_SIZE, 
        "GET %s HTTP/1.1\r\nConnection: Close\r\n"
        "Host: %s\r\n", file_addr, host_addr);
    }

    FILE *file = fopen(cookies, "r");
    
    if (!file) {
        free(host_addr);
        fclose(stream);
        close(sockfd);
        syserr("no cookie file");
    }
    
    char c;
    int i = strlen(sendline);
    strcat(sendline, SEND_COOKIE);
    i += strlen(SEND_COOKIE);
    while (fscanf(file, "%c", &c) != EOF) {
        if (c == '\n') {
            sendline[i++] = ';';
        }
        else {
            sendline[i++] = c;
        }
    }
    sendline[i++] = '\r';
    sendline[i++] = '\n';
    
    strcat(sendline, "\r\n");
    
    if (write(sockfd, sendline, sizeof(sendline)) < 0) {
        free(host_addr);
        syserr("bad write to socket");
    }
    
    fclose(file);
}


bool read_header(FILE *stream, int sock) {
    char *line_buf = NULL;
    size_t line_buf_size = 0;
    ssize_t line_size = 0;
    
    line_size = getline(&line_buf, &line_buf_size, stream);
    
    // check if response is "200 OK"
    if (cmp_no_case(line_buf, OK_CODE, strlen(OK_CODE)) != 0) { 
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
        
        if (cmp_no_case(line_buf, "\r\n", strlen("\r\n")) == 0) { // end of header
            break;
        }
        else if (cmp_no_case(line_buf, ENCODING, strlen(ENCODING)) == 0) { // Transfer-Encoding:
            if (cmp_no_case(&line_buf[strlen(ENCODING)], CHUNKED, strlen(CHUNKED)) == 0 ||
                cmp_no_case(&line_buf[strlen(ENCODING)+1], CHUNKED, strlen(CHUNKED)) == 0) {
                chunked = true;
            }
            else {
                free(host_addr);
                fclose(stream);
                close(sock);
                syserr("wrong value in Transfer-Encoding field");
            }
        }
        else if (cmp_no_case(line_buf, CONTENT, strlen(CONTENT)) == 0) { // Content-length:
            int i = strlen(CONTENT);
            while (line_buf[i] != '\r') { 
                i++;
            }
            line_buf[i] = '\0';
            content_length = atoi(&line_buf[strlen(CONTENT)]);
        }
        else if (cmp_no_case(line_buf, COOKIE, strlen(COOKIE)) == 0) { // Set-Cookie:
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
        free(host_addr);
        syserr("getaddrinfo: %s", gai_strerror(err));
    }
    else if (err != 0) { // other error (host not found, etc.)
        free(host_addr);
        fatal("getaddrinfo: %s", gai_strerror(err));
    }

    // initialize socket according to getaddrinfo results
    sock = socket(addr_result->ai_family, addr_result->ai_socktype, addr_result->ai_protocol);
    if (sock < 0) {
        free(host_addr);
        syserr("socket");
    }

    // connect socket to the server
    if (connect(sock, addr_result->ai_addr, addr_result->ai_addrlen) < 0) {
        free(host_addr);
        syserr("connect");
    }
    freeaddrinfo(addr_result);    
    
    FILE *fp = fdopen(sock, "r");
    if (!fp) {
        syserr("failed fdopen");
    }
    
    send_get_request(sock, fp);
    
    if (read_header(fp, sock)) {
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
