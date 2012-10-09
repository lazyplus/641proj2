/*
 * Utilities such as log and read buffer
 * by Yu Su <ysu1@andrew.cmu.edu>
 */
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

#include <stdio.h>
#include <unistd.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <sys/socket.h>
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

int read_line(struct ReadBuffer * rb, char * buf, int buf_cap){
	int i=0;
	for(; i<rb->cnt - 1; ++i){
		if(rb->buf[i] == '\r' && rb->buf[i+1] == '\n'){
			if(i > buf_cap)
				return -2;
			memcpy(buf, rb->buf, (i+2) * sizeof(char));
			buf[i+2] = 0;
			int offset = i + 2;
			memmove(rb->buf, rb->buf + offset, (rb->cnt - offset) * sizeof(char));
			rb->cnt -= offset;
			return i+2;
		}
	}
    // a very long line would be cut and returned.
    if(rb->cnt == BUF_SIZE){
        int move = BUF_SIZE - 2;
        if(buf_cap - 1 < move)
            move = buf_cap - 1;

        memcpy(buf, rb->buf, move * sizeof(char));
        buf[move] = 0;
        memmove(rb->buf, rb->buf + move, (BUF_SIZE - move) * sizeof(char));
        rb->cnt = BUF_SIZE - move;
        return BUF_SIZE - move;
    }
	return 0;
}

int read_cnt(struct ReadBuffer * rb, char * buf, int buf_cap, int cnt){
    if(rb->cnt < cnt)
        cnt = rb->cnt;
	memcpy(buf, rb->buf, cnt * sizeof(char));
	buf[cnt] = 0;
	memmove(rb->buf, rb->buf + cnt, (rb->cnt - cnt) * sizeof(char));
	rb->cnt -= cnt;
	return cnt;
}

struct LinkedListNode{
    void * data;
    struct LinkedListNode * next;
};

struct LinkedList{
    struct LinkedListNode * head, * tail;
};

struct LinkedList * get_linked_list(){
    struct LinkedList * l = (struct LinkedList *) malloc(sizeof(struct LinkedList));
    l->head = l->tail = NULL;
    return l;
}

int insert_linked_list(struct LinkedList * list, void * data){
    struct LinkedListNode * n = (struct LinkedListNode *) malloc(sizeof(struct LinkedListNode));
    n->data = data;
    n->next = NULL;
    if(list->head == NULL){
        list->head = list->tail = n;
    } else {
        list->tail->next = n;
        list->tail = n;
    }
    return 0;
}

void * remove_linked_list(struct LinkedList * list){
    if(list->head == NULL)
        return NULL;

    struct LinkedListNode * n = list->head;
    list->head = n->next;
    if(list->head == NULL)
        list->tail = NULL;

    void * data = n->data;
    free(n);
    return data;
}

void free_linked_list(struct LinkedList * list){
    struct LinkedListNode * p;
    while((p = remove_linked_list(list)) != NULL){
        free(p);
    }
    free(list);
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

void get_filetype(char * file_name, char * file_type){
	int n = strlen(file_name);

	if(strcasecmp(file_name + n - strlen(".html"), ".html") == 0){
		strcpy(file_type, "text/html");
	}else if(strcasecmp(file_name + n - strlen(".css"), ".css") == 0){
		strcpy(file_type, "text/css");
	}else if(strcasecmp(file_name + n - strlen(".png"), ".png") == 0){
		strcpy(file_type, "image/png");
	}else if(strcasecmp(file_name + n - strlen(".jpg"), ".jpg") == 0){
		strcpy(file_type, "image/jpeg");
	}else if(strcasecmp(file_name + n - strlen(".gif"), ".gif") == 0){
		strcpy(file_type, "image/gif");
	}else{
		strcpy(file_type, "text/plain");
	}
}

char* ENVP[] = {
    "QUERY_STRING=",
    "REMOTE_ADDR=",
    "REQUEST_METHOD=",
    "REQUEST_URI=",
    "SCRIPT_NAME=",
    "SERVER_PORT=",
    "GATEWAY_INTERFACE=CGI/1.1",
    "SERVER_PROTOCOL=HTTP/1.1",
    "SERVER_SOFTWARE=Liso/1.0",
    "CONTENT_LENGTH=",
    "CONTENT_TYPE=",
    "HTTP_ACCEPT_ENCODING=",
    "HTTP_ACCEPT_LANGUAGE=",
    "HTTP_ACCEPT_CHARSET=",
    "HTTP_ACCEPT=",
    "HTTP_REFERER=",
    "HTTP_HOST=",
    "HTTP_COOKIE=",
    "HTTP_USER_AGENT=",
    "HTTP_CONNECTION=",
    NULL
};

char* ENVP_C[] = {
    "QUERY-STRING",
    "REMOTE-ADDR",
    "REQUEST-METHOD",
    "REQUEST-URI",
    "SCRIPT-NAME",
    "SERVER-PORT",
    "GATEWAY-INTERFACE=CGI/1.1",
    "SERVER-PROTOCOL=HTTP/1.1",
    "SERVER-SOFTWARE=Liso/1.0",
    "CONTENT-LENGTH",
    "CONTENT-TYPE",
    "ACCEPT-ENCODING",
    "ACCEPT-LANGUAGE",
    "ACCEPT-CHARSET",
    "ACCEPT",
    "REFERER",
    "HOST",
    "COOKIE",
    "USER-AGENT",
    "CONNECTION",
    NULL
};

int match_envp(struct LinkedList * l, char * args[]){
    static int matched[MAX_ENV_CNT];

    int i=0;
    write_log(INFO, "match envp");
    memset(matched, 0, sizeof(matched));
    struct LinkedListNode * p;
    int last_arg = 9;
    for(p=l->head; p!=NULL; p=p->next){
        for(i=0; ENVP_C[i] != NULL; ++i){
            if(matched[i])
                continue;
            if(strncasecmp((char *)p->data, ENVP_C[i], strlen(ENVP_C[i])) == 0){
                matched[i] = 1;
                
                char * sep = strchr((char *)p->data, ':') + 1;
                while(*sep == ' ') ++ sep;

                
                int n = strlen(sep);
                n += strlen(ENVP[i]);
                args[last_arg] = malloc(n + 1);
                strcpy(args[last_arg], ENVP[i]);
                strcat(args[last_arg], sep);
                printf("matched: from %s get: %s final %s\n", (char *)p->data, sep, args[last_arg]);
                ++last_arg;
                break;
            }
        }
        if(ENVP_C[i] == NULL)
            printf("unmatched: %s\n", (char *)p->data);
    }

    return 0;
}
