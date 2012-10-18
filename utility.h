/*
 * Utilities such as log and read buffer
 * by Yu Su <ysu1@andrew.cmu.edu>
 */

#ifndef UTILITY_H

#define UTILITY_H

#include "list.h"

#define MAX_LOG_LEN 1024

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

struct LinkedList;

struct LinkedList * get_linked_list();

int insert_linked_list(struct LinkedList * list, void * data);

void * remove_linked_list(struct LinkedList * list);

void free_linked_list(struct LinkedList * list);

// Get current time and format it into string
int get_time(char * buf, int buf_size);

// listen on TCP port and return the listen fd
int get_listen_fd(int port);

// bind on UDP port and return the listen fd
int get_udp_listen_fd(int port);

#endif
