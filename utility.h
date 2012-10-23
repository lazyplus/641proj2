/*
 * Utilities such as log and read buffer
 * by Yu Su <ysu1@andrew.cmu.edu> Hanshi Lei <hanshil@andrew.cmu.edu>
 */

#ifndef UTILITY_H

#define UTILITY_H

// linked list from Linux kernel
#include "list.h"

#define MAX_LOG_LEN 1024

#define SAFE_FREE(a) do{if(a)free(a); a = NULL;}while(0);

enum LogLevel{
	INFO, WARN, ERROR
};

int set_log_fd(int fd);

int write_log(int level, char * fmt, ...);

int close_log_file();

#define BUF_SIZE 8192

struct ReadBuffer;

int init_read_buffer(struct ReadBuffer ** buf, int fd);

int free_read_buffer(struct ReadBuffer ** buf);

// Read from fd to buffer
int fill_read_buffer(struct ReadBuffer * buf);

// Get a line if there is any or read buffer is full
int read_line(struct ReadBuffer * rb, char * buf, int buf_cap);

// Get a fixed-length content into user's buffer
int read_cnt(struct ReadBuffer * rb, char * buf, int buf_cap, int cnt);

int read_token(struct ReadBuffer * rb, char * buf, int buf_cap, int token_mode);

// Get current time and format it into string
int get_time(char * buf, int buf_size);

// listen on TCP port and return the listen fd
int get_listen_fd(int port);

// bind on UDP port and return the listen fd
int get_udp_listen_fd(int port);

int send_packet(int sockfd, char * host, int port, char * payload, int len);

#endif
