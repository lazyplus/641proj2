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

int handle_timeout(struct RouteDaemon * rd);
int handle_new_cgi_conn(struct RouteDaemon * rd);
int handle_LSA(struct RouteDaemon * rd);
int handle_cgi_req(struct RouteDaemon * rd);

// main serving routine
int daemon_serve(struct RouteDaemon * rd, int * running){
    struct timeval time_out;

    while (*running){
        time_out.tv_sec = 1;
        time_out.tv_usec = 0;
        rd->ready_read_set = rd->read_set;
        rd->ready_cnt = select(rd->max_fd + 1, & rd->ready_read_set, NULL, NULL, & time_out);

        if(rd->ready_cnt < 0)
            continue;
        
        if(rd->ready_cnt == 0){
            handle_timeout(rd);
        }else{
            handle_new_cgi_conn(rd);
            handle_LSA(rd);
            handle_cgi_req(rd);
        }
    }
    return 0;
}

int add_neighbor(struct RouteDaemon * rd, char * buf);
int add_local_object(struct RouteDaemon * rd, char * name, char * path);
int init_ports(struct RouteDaemon * rd);

// allocate a new routing daemon based on the cmd line arguments
struct RouteDaemon * get_new_rd(char ** config){
    static char buffer[BUF_SIZE], name[BUF_SIZE], path[BUF_SIZE];

    write_log(INFO, "Allocating new routing daemon");
    struct RouteDaemon * rd = malloc(sizeof(struct RouteDaemon));
    sscanf(config[1], "%d", &rd->node_id);
    rd->config_file = strdup(config[2]);
    rd->file_list = strdup(config[3]);
    sscanf(config[4], "%d", &rd->adv_timeout);
    sscanf(config[5], "%d", &rd->neighbor_timeout);
    sscanf(config[6], "%d", &rd->retran_timeout);
    sscanf(config[7], "%d", &rd->lsa_timeout);

    memset(rd->conn_fd, -1, sizeof(rd->conn_fd));
    INIT_LIST_HEAD( & rd->objects.list);
    INIT_LIST_HEAD( & rd->nodes.list);
    INIT_LIST_HEAD( & rd->neighbors.list);
    INIT_LIST_HEAD( & rd->local_objects.list);

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

    write_log(INFO, "Allocated new routing daemon at %p", rd);
    return rd;
}

// close a routing daemon and release resources
int close_daemon(struct RouteDaemon * rd){
    free(rd->config_file);
    free(rd->file_list);

    ///TODO: free the list of NodeInfo, Objects, Neighbors, LocalObjects
    ///TODO: close sockets
    free(rd);
    return 0;
}


/********************************
 * implementation details
 *******************************/

int handle_timeout(struct RouteDaemon * rd){
    /// TODO: resend LSA to neighbors didn't response ACK
    /// TODO: expire too old LSA and announce the expiration
    /// TODO: delete too old LSA after all neighbors responded ACK
    return 0;
}

int add_cgi_conn(struct RouteDaemon * rd, int fd);
int rm_cgi_conn(struct RouteDaemon * rd, int id);

int handle_new_cgi_conn(struct RouteDaemon * rd){
    struct sockaddr_in cli_addr;
    socklen_t cli_size = sizeof(struct sockaddr_in);
    
    if(rd->ready_cnt <= 0)
        return 0;

    if(FD_ISSET(rd->local_fd, & rd->ready_read_set)){
        FD_CLR(rd->local_fd, & rd->ready_read_set);
        int fd = accept(rd->local_fd, (struct sockaddr *) & cli_addr, & cli_size);
        if(fd == -1){
            write_log(ERROR, "Cannot accept more connections");
        }else{
            int id = add_cgi_conn(rd, fd);
            write_log(INFO, "New client from %s connect_id: %d fd: %d", 
                inet_ntoa(cli_addr.sin_addr), id, rd->conn_fd[id]);
        }
    }
    return 0;
}

int handle_LSA(struct RouteDaemon * rd){
    static char buf[BUF_SIZE];

    if(rd->ready_cnt <= 0)
        return 0;

    struct sockaddr_in cli_addr;
    socklen_t cli_len = sizeof(cli_addr);

    if(FD_ISSET(rd->routing_fd, & rd->ready_read_set)){
        FD_CLR(rd->routing_fd, & rd->ready_read_set);
        int cnt = recvfrom(rd->routing_fd, buf, BUF_SIZE, 0, (struct sockaddr *)&cli_addr, &cli_len);
        struct LSA * newLSA = unmarshal_LSA(buf, cnt);
        if(newLSA == NULL)
            return 0;

        write_log(INFO, "Got new LSA");
        print_LSA(newLSA);
        int len = marshal_LSA(newLSA, buf, BUF_SIZE);
        sendto(rd->routing_fd, buf, len, 0, (struct sockaddr *)&cli_addr, sizeof(cli_addr));
    }

    ///TODO: handle new LSA (update or echo-back)
    ///TODO: update: re-compute shortest path and flood
    ///TODO: echo-back: send neighbor's last LSA back

    ///TODO: handle ACK
    return 0;
}

int parse_req(struct RouteDaemon * rd, char * buf, int len, int conn_id);
int handle_cgi_req(struct RouteDaemon * rd){
    ///TODO: parse the input
    ///TODO: lookup in local and remote
    ///TODO: add new file and flood LSA

    // code below: ugly local lookup, change it
    static char buf[BUF_SIZE];
    int i;
    for(i=0; i<MAX_CONNECT && rd->ready_cnt > 0; ++i){
        int fd = rd->conn_fd[i];
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
            rm_cgi_conn(rd, i);
        }
    }
    return 0;
}

int add_neighbor(struct RouteDaemon * rd, char * line){
    static int id, rport, lport, sport;
    static char hostname[BUF_SIZE];

    // write_log(INFO, "Adding Neighbor %s", line);
    sscanf(line, "%d%s%d%d%d", &id, hostname, &rport, &lport, &sport);

    if(id == rd->node_id){
        rd->rport = rport;
        rd->lport = lport;
        rd->sport = sport;
    }else{
        struct NodeInfo * ni = (struct NodeInfo *) malloc(sizeof(struct NodeInfo));
        ni->node_id = id;
        ni->hostname = strdup(hostname);
        ni->rport = rport;
        ni->lport = lport;
        ni->sport = sport;
        list_add(&(ni->list), &(rd->neighbors.list));
    }

    return 0;
}

int add_local_object(struct RouteDaemon * rd, char * name, char * path){
    struct LocalObject * lo = (struct LocalObject *) malloc(sizeof(struct LocalObject));
    lo->name = strdup(name);
    lo->path = strdup(path);
    list_add(&(lo->list), &(rd->local_objects.list));
    return 0;
}

int init_ports(struct RouteDaemon * rd){
    rd->local_fd = get_listen_fd(rd->lport);
    rd->routing_fd = get_udp_listen_fd(rd->rport);

    write_log(INFO, "RD %p is listening on %d for TCP, %d for UDP", rd, rd->lport, rd->rport);

    rd->max_fd = rd->local_fd;
    if(rd->max_fd < rd->routing_fd)
        rd->max_fd = rd->routing_fd;

    rd->ready_cnt = 0;
    FD_ZERO( & rd->read_set);
    FD_SET(rd->local_fd, & rd->read_set);
    FD_SET(rd->routing_fd, & rd->read_set);
    return 0;
}

int add_cgi_conn(struct RouteDaemon * rd, int fd){
    int i;
    for(i=0; i<MAX_CONNECT; ++i)
        if(rd->conn_fd[i] == -1)
            break;
    if(i == MAX_CONNECT)
        return -1;
    
    write_log(INFO, "New connection_id %d", i);
    if(rd->max_fd < fd)
        rd->max_fd = fd;

    rd->conn_fd[i] = fd;
    FD_SET(fd, & rd->read_set);
    init_read_buffer( & rd->connect_buf[i], fd);
    return i;
}

int rm_cgi_conn(struct RouteDaemon * rd, int id){
    write_log(INFO, "Close connection_id %d", id);
    close(rd->conn_fd[id]);
    FD_CLR(rd->conn_fd[id], & rd->read_set);
    free_read_buffer( & rd->connect_buf[id]);
    rd->conn_fd[id] = -1;
    return 0;
}

int response_cgi(struct RouteDaemon * rd, int connection_id, char * buf, int len){
    write_log(INFO, "Write to connection %d bytes: %d", connection_id, len);
    char * pos = buf;
    int remain = len;
    int fd = rd->conn_fd[connection_id];
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
    // printf("spliting %s\n", buf);
    req->cmd = buf;
    char * p = buf;
    while(*p != ' ') ++p;
    int name_len;
    sscanf(++p, "%d", &name_len);
    while(*p >= '0' && *p <= '9') ++p;
    // printf("name %d %.*s\n", name_len, name_len, p+1);
    req->name = ++p;
    p = p + name_len;
    *p = 0;
    if(p >= len + buf)
        return 0;
    int path_len;
    sscanf(++p, "%d", &path_len);
    while(*p >= '0' && *p <= '9') ++p;
    // printf("path %d %.*s\n", path_len, path_len, p+1);
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

        struct list_head *pos;
        list_for_each(pos, &(rd->local_objects.list)){
            struct LocalObject * tmp = list_entry(pos, struct LocalObject, list);
            if(strcmp(tmp->name, req.name) == 0){
                sprintf(b, "http://localhost:%d%s\r\n", rd->sport, tmp->path);
                int len = strlen(b);
                sprintf(rep, "OK %d %s", len, b);
                response_cgi(rd, conn_id, rep, strlen(rep));
                return 0;
            }
        }
        sprintf(rep, "NOTFOUND 0\r\n");
        response_cgi(rd, conn_id, rep, strlen(rep));
    }else if(strncmp(req.cmd, "ADDFILE", strlen("ADDFILE")) == 0){
        add_local_object(rd, req.name, req.path);
        sprintf(rep, "OK 0\r\n");
        response_cgi(rd, conn_id, rep, strlen(rep));
    }
    return 0;
}

struct LSAParser{
    char version, ttl;
    short type;
    int sender_id, seq_num;
    int n_entries, n_objects;
};

int marshal_LSA(struct LSA * lsa, char * buf, int len){
    struct LSAParser parser = * (struct LSAParser * )lsa;

    int total_size = sizeof(struct LSAParser);
    total_size += sizeof(int) * lsa->n_entries;
    struct list_head *pos;
    list_for_each(pos, &(lsa->objects.list)){
        struct str_list * entry = list_entry(pos, struct str_list, list);
        total_size += strlen(entry->str);
    }
    if(total_size > len)
        return -1;

    memcpy(buf, (char *) &parser, sizeof(struct LSAParser));

    char * p = buf + sizeof(struct LSAParser);
    list_for_each(pos, &(lsa->entries.list)){
        struct id_list * entry = list_entry(pos, struct id_list, list);
        memcpy(p, &entry->id, sizeof(entry->id));
        p += sizeof(entry->id);
    }

    list_for_each(pos, &(lsa->objects.list)){
        struct str_list * entry = list_entry(pos, struct str_list, list);
        strcpy(p, entry->str);
        p += strlen(entry->str);
        * p = '\0';
        ++ p;
    }
    return p - buf;
}

struct LSA * unmarshal_LSA(char * buf, int len){
    struct LSAParser * parser = (struct LSAParser *) buf;
    struct LSA * lsa = (struct LSA *) malloc(sizeof(struct LSA));
    lsa->version = parser->version;
    lsa->ttl = parser->ttl;
    lsa->type = parser->type;
    lsa->sender_id = parser->sender_id;
    lsa->seq_num = parser->seq_num;
    lsa->n_entries = parser->n_entries;
    lsa->n_objects = parser->n_objects;
    INIT_LIST_HEAD( & (lsa->entries.list));
    INIT_LIST_HEAD( & (lsa->objects.list));
    char * s = buf + sizeof(struct LSAParser);
    int i;
    for(i=0; i<lsa->n_entries; ++i){
        struct id_list * n = (struct id_list *) malloc(sizeof(struct id_list));
        n->id = * (int *) s;
        s += sizeof(int);
        list_add( & (n->list), & (lsa->entries.list));
    }
    for(i=0; i<lsa->n_objects; ++i){
        struct str_list * n = (struct str_list *) malloc(sizeof(struct str_list));
        n->str = strdup(s);
        s += strlen(s) + 1;
        list_add( & (n->list), & (lsa->objects.list));
    }
    return lsa;
}

int print_LSA(struct LSA * lsa){
    printf("%d %d %d\n%d %d\n", lsa->version, lsa->ttl, lsa->type, lsa->n_entries, lsa->n_objects);
    struct list_head *pos;
    list_for_each(pos, &(lsa->entries.list)){
        struct id_list * entry = list_entry(pos, struct id_list, list);
        printf("%d ", entry->id);
    }
    printf("\n");

    list_for_each(pos, &(lsa->objects.list)){
        struct str_list * entry = list_entry(pos, struct str_list, list);
        printf("%s\n", entry->str);
    }
    return 0;
}
