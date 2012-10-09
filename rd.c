#include <netinet/in.h>
#include <netinet/ip.h>
#include <sys/socket.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <arpa/inet.h>
#include "rd.h"
#include "utility.h"

#define BUF_SIZE 8192

struct ListNode{
    char * name;
    char * path;
    struct ListNode * next;
};

struct RouteDaemon{
    int node_id;
    int adv_timeout;
    int neighbor_timeout;
    int retran_timeout;
    int lsa_timeout;
    char * config_file;
    char * file_list;

    int routing_port;
    int local_port;
    int server_port;

    int max_fd;
    int connect_fd[MAX_CONNECT];
    int local_fd;
    fd_set ready_read_set;
    fd_set read_set;
    int ready_cnt;

    struct ListNode * local_objects;

    struct ReadBuffer * connect_buf[MAX_CONNECT];
};


int add_neighbor(struct RouteDaemon * rd, char * line){
    static int id, rport, lport, sport;
    static char hostname[BUF_SIZE];

    sscanf(line, "%d%s%d%d%d", &id, hostname, &rport, &lport, &sport);

    if(id == rd->node_id){
        rd->routing_port = rport;
        rd->local_port = lport;
        rd->server_port = sport;
    }

    return 0;
}

int add_local_object(struct RouteDaemon * rd, char * name, char * path){
    struct ListNode * ln = malloc(sizeof(struct ListNode));
    ln->name = strdup(name);
    ln->path = strdup(path);
    ln->next = rd->local_objects;
    rd->local_objects = ln;
    return 0;
}

int init_ports(struct RouteDaemon * rd){
    int fd = get_listen_fd(rd->local_port);
    rd->local_fd = fd;
    rd->max_fd = fd;
    rd->ready_cnt = 0;
    FD_ZERO( & rd->read_set);
    FD_SET(fd, & rd->read_set);
    return 0;
}

struct RouteDaemon * get_new_rd(char ** config){
    static char buffer[BUF_SIZE], name[BUF_SIZE], path[BUF_SIZE];

    struct RouteDaemon * rd = malloc(sizeof(struct RouteDaemon));
    sscanf(config[1], "%d", &rd->node_id);
    rd->config_file = strdup(config[2]);
    rd->file_list = strdup(config[3]);
    sscanf(config[4], "%d", &rd->adv_timeout);
    sscanf(config[5], "%d", &rd->neighbor_timeout);
    sscanf(config[6], "%d", &rd->retran_timeout);
    sscanf(config[7], "%d", &rd->lsa_timeout);

    memset(rd->connect_fd, -1, sizeof(rd->connect_fd));

    FILE * fin = fopen(rd->config_file, "r");
    while(fgets(buffer, BUF_SIZE, fin)){
        add_neighbor(rd, buffer);
    }
    fclose(fin);

    fin = fopen(rd->file_list, "r");
    while(fgets(buffer, BUF_SIZE, fin)){
        sscanf(buffer, "%s%s", name, path);
        add_local_object(rd, name, path);
    }
    fclose(fin);

    init_ports(rd);

    return rd;
}

int add_connect(struct RouteDaemon * rd, int fd){
    int i;
    for(i=0; i<MAX_CONNECT; ++i)
        if(rd->connect_fd[i] == -1)
            break;
    if(i == MAX_CONNECT)
        return -1;
    
    write_log(INFO, "New connection_id %d", i);
    if(rd->max_fd < fd)
        rd->max_fd = fd;

    rd->connect_fd[i] = fd;
    FD_SET(fd, & rd->read_set);
    init_read_buffer( & rd->connect_buf[i], fd);
    return i;
}

int remove_connection(struct RouteDaemon * rd, int id){
    write_log(INFO, "Close connection_id %d", id);
    close(rd->connect_fd[id]);
    FD_CLR(rd->connect_fd[id], & rd->read_set);
    free_read_buffer( & rd->connect_buf[id]);
    rd->connect_fd[id] = -1;
    return 0;
}

int handle_new_conn(struct RouteDaemon * rd){
    struct sockaddr_in cli_addr;
    socklen_t cli_size = sizeof(struct sockaddr_in);
    
    if(FD_ISSET(rd->local_fd, & rd->ready_read_set)){
        FD_CLR(rd->local_fd, & rd->ready_read_set);
        int fd = accept(rd->local_fd, (struct sockaddr *) & cli_addr, & cli_size);
        if(fd == -1){
            write_log(ERROR, "Cannot accept more connections");
        }else{
            int id = add_connect(rd, fd);
            write_log(INFO, "New client from %s connect_id: %d fd: %d", 
                inet_ntoa(cli_addr.sin_addr), id, rd->connect_fd[id]);
        }
    }
    return 0;
}

int handle_ospf(struct RouteDaemon * rd){
    return 0;
}


int write_data(struct RouteDaemon * rd, int connection_id,
    char * buf, int len){

    write_log(INFO, "Write to connection %d bytes: %d", connection_id, len);
    char * pos = buf;
    int remain = len;
    int fd = rd->connect_fd[connection_id];
    while(remain > 0){
        int ret = 0;
        ret = write(fd, pos, remain);
        if(ret <= 0)
            return -1;
        remain -= ret;
        pos += ret;
    }
    return 0;
}

struct Request{
    char * cmd;
    char * name;
    char * path;
};

int split_req(char * buf, int len, struct Request * req){
    printf("spliting %s\n", buf);
    req->cmd = buf;
    char * p = buf;
    while(*p != ' ') ++p;
    int name_len;
    sscanf(++p, "%d", &name_len);
    while(*p >= '0' && *p <= '9') ++p;
    printf("name %d %.*s\n", name_len, name_len, p+1);
    req->name = ++p;
    p = p + name_len;
    *p = 0;
    if(p >= len + buf)
        return 0;
    int path_len;
    sscanf(++p, "%d", &path_len);
    while(*p >= '0' && *p <= '9') ++p;
    printf("path %d %.*s\n", path_len, path_len, p+1);
    req->path = ++p;
    p = p + path_len;
    *p = 0;
    return 0;
}

int parse_req(struct RouteDaemon * rd, char * buf, int len, int conn_id){
    static char rep[BUF_SIZE], b[BUF_SIZE];

    struct Request req;
    buf[len-2] = 0;
    split_req(buf, len-2, &req);

    if(strncmp(req.cmd, "GETRD", strlen("GETRD")) == 0){
        int namelen;
        sscanf(buf + strlen("GETRD"), "%d", &namelen);

        struct ListNode * ln = rd->local_objects;
        while(ln != NULL){
            if(strcmp(ln->name, req.name) == 0){
                sprintf(b, "http://localhost:%d%s\r\n", rd->server_port, ln->path);
                int len = strlen(b);
                sprintf(rep, "OK %d %s", len, b);
                write_data(rd, conn_id, rep, strlen(rep));
                break;
            }
            ln = ln->next;
        }
        if(ln == NULL){
            sprintf(rep, "NOTFOUND 0\r\n");
            write_data(rd, conn_id, rep, strlen(rep));
        }
    }else if(strncmp(req.cmd, "ADDFILE", strlen("ADDFILE")) == 0){
        add_local_object(rd, req.name, req.path);
        sprintf(rep, "OK 0\r\n");
        write_data(rd, conn_id, rep, strlen(rep));
    }
    return 0;
}

int handle_clients(struct RouteDaemon * rd){
    static char buf[BUF_SIZE];
    int i;
    for(i=0; i<MAX_CONNECT && rd->ready_cnt > 0; ++i){
        int fd = rd->connect_fd[i];
        if(fd == -1 || !FD_ISSET(fd, &rd->ready_read_set))
            continue;
        
        -- rd->ready_cnt;
        int ret_r, ret_parse = 0;
        int ret_fill = fill_read_buffer(rd->connect_buf[i]);
        write_log(INFO, "Read from connect_id %d bytes %d", i, ret_fill);
        if(ret_fill > 0){
            // In case one read gets more lines, loop here
            do{
                ret_r = read_line(rd->connect_buf[i], buf, BUF_SIZE);

                if(ret_r > 0){
                    ret_parse = parse_req(rd, buf, ret_r, i);
                }
            }while(ret_r > 0 && ret_parse == 0);
        }
        if(ret_fill < 0 || ret_parse < 0){
            remove_connection(rd, i);
        }
    }
    return 0;
}

int check_timeout(struct RouteDaemon * rd){
    return 0;
}

int daemon_serve(struct RouteDaemon * rd, int * running){
    struct timeval time_out;

    while (*running){
        time_out.tv_sec = 1;
        time_out.tv_usec = 0;
        rd->ready_read_set = rd->read_set;
        rd->ready_cnt = select(rd->max_fd + 1, & rd->ready_read_set, NULL, NULL, & time_out);
        
        check_timeout(rd);

        if(rd->ready_cnt <= 0)
            continue;

        handle_new_conn(rd);

        handle_ospf(rd);
        
        handle_clients(rd);
    }
    return 0;
}

int close_daemon(struct RouteDaemon * rd){
    return 0;
}
