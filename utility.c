/*
 * Utilities wrappers for log, read buffer and sockets
 * by Yu Su <ysu1@andrew.cmu.edu> Hanshi Lei <hanshil@andrew.cmu.edu>
 */
#include <stdio.h>
#include <unistd.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <ctype.h>
#include "utility.h"

static int log_fd = 0;

int set_log_fd(int fd){
	int old_fd = log_fd;
	log_fd = fd;
	return old_fd;
}

int write_log(int level, char * fmt, ...){
	static char buf[MAX_LOG_LEN];
	static char date[MAX_LOG_LEN];
	static char log_str[][10] = {"Info", "Warning", "Error"};
	va_list args;
	va_start(args, fmt);
	vsprintf(buf, fmt, args);
	va_end(args);
	get_time(date, MAX_LOG_LEN);
	sprintf(date, "%s %s: %s\n", date, log_str[level], buf);
	int remain = strlen(date);
	while(remain > 0){
		int ret = write(log_fd, date, strlen(date));
		remain -= ret;
	}
	return 0;
}

int close_log_file(){
	return close(log_fd);
}

struct ReadBuffer{
	char * buf;
	int cnt;
	int fd;
};

int init_read_buffer(struct ReadBuffer ** buff, int fd){
	struct ReadBuffer * buf = * buff = (struct ReadBuffer *) malloc(sizeof(struct ReadBuffer));
	buf->cnt = 0;
	buf->fd = fd;
	buf->buf = malloc(BUF_SIZE * sizeof(char));
	return -(buf->buf == NULL);
}

int free_read_buffer(struct ReadBuffer ** buff){
	struct ReadBuffer * buf = * buff;
	free(buf->buf);
    buf->buf = NULL;
    buf->cnt = 0;
    buf->fd = 0;
    * buff = NULL;
    return 0;
}

int fill_read_buffer(struct ReadBuffer * buf){
    int ret;

    ret = read(buf->fd, buf->buf + buf->cnt, BUF_SIZE - buf->cnt);

	if(ret <= 0){
		buf->fd = 0;
		return -1;
	}
	buf->cnt += ret;
	return ret;
}

int read_token(struct ReadBuffer * rb, char * buf, int buf_cap, int token_mode){
    int i;
    for(i=0; i<rb->cnt; ++i){
        if(!isspace(rb->buf[i]))
            break;
    }
    memmove(rb->buf, rb->buf + i, rb->cnt - i);
    rb->cnt -= i;

    for(i=0; i<rb->cnt; ++i)
        if(isspace(rb->buf[i]))
            break;

    if(i == rb->cnt && token_mode != i)
        return 0;

    if(token_mode == 0){
        if(!i)
            return 0;
        memcpy(buf, rb->buf, i);
        buf[i] = '\0';
        memmove(rb->buf, rb->buf + i + 1, rb->cnt - i - 1);
        rb->cnt = rb->cnt - i - 1;
        return i;
    }else if(i >= token_mode){
        i = token_mode;
        memcpy(buf, rb->buf, i);
        buf[i] = '\0';
        memmove(rb->buf, rb->buf + i, rb->cnt - i);
        rb->cnt = rb->cnt - i;
        return i;
    }

    return 0;
}

int get_time(char * buf, int buf_size){
	time_t rawtime;
	struct tm * timeinfo;
	time ( & rawtime);
	timeinfo = localtime ( & rawtime);
	return strftime(buf, buf_size, "%a, %d %b %Y %X GMT", timeinfo);
}

int get_listen_fd(int port){
	int sock;
	struct sockaddr_in addr;
    
    if((sock = socket(PF_INET, SOCK_STREAM, 0)) == -1){
        return -1;
    }
    
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;
    
    if(bind(sock, (struct sockaddr *) &addr, sizeof(addr))){
        close(sock);
        return -1;
    }
    
    if(listen(sock, 50)){
        close(sock);
        return -1;
    }

    return sock;
}

int get_udp_listen_fd(int port){
    int sockfd;
    struct sockaddr_in serv_addr;

    // setup the socket
    sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if(sockfd < 0) {
        printf("error on socket() call");
        return -1;
    }

    // Setup the server address and bind to it
    memset(&serv_addr, '\0', sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(port);
    if (bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
        printf("error binding socket\n");
        return -1;
    }
    return sockfd;
}

int send_packet(int sockfd, char * host, int port, char * payload, int len){
    // write_log(INFO, "sending to %s:%d %d", host, port, len);

    struct sockaddr_in cli_addr;
    struct hostent * h;

    // Lets set up the structure for who we want to contact
    if((h = gethostbyname(host))==NULL) {
        printf("error resolving host\n");
        return -1;
    }

    memset(&cli_addr, '\0', sizeof(cli_addr));
    cli_addr.sin_family = AF_INET;
    cli_addr.sin_addr.s_addr = *(in_addr_t *)h->h_addr;
    cli_addr.sin_port = htons(port);
    return sendto(sockfd, payload, len, 0, (struct sockaddr *)&cli_addr, sizeof(cli_addr));
}
