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

// linked list for id
struct id_list{
    int id;
    struct list_head list;
};

// linked list for string
struct str_list{
    char * str;
    struct list_head list;
};
void free_str_list(struct str_list * list);

// Link State data structure
struct LSA{
    int version, ttl, type;
    int sender_id, seq_num;
    int n_entries, n_objects;
    struct id_list entries;
    struct str_list objects;
};
struct LSA * parse_LSA(char * buf, int len);
void free_LSA(struct LSA * lsa);

// all info belongs to a node
struct NodeInfo{
    // basic
    int node_id;
    char * hostname;
    int rport, sport, lport;

    // routing info
    int next_hop;
    int distance;

    // timeout
    int last_lsa;
    int ack_count_down;

    // last received LSA
    struct LSA * lsa;

    // linked list of NodeInfo
    struct list_head list;
};

// every object has a linked list of node containing it
struct ObjectInfo{
    char * name;

    // which node have this object
    struct id_list nodes;

    // linked list
    struct list_head list;
};

struct LocalObject{
    char * name;
    char * path;

    struct list_head list;
};

// all info of a routing daemon
struct RouteDaemon{
    int node_id;

    // how long would my LSA be re-flooded
    int adv_timeout;
    // how long a neighbor would be treated as down
    int neighbor_timeout;
    // how long to wait for retransmitting LSA again if ACK is not received
    int retran_timeout;
    // how long a outside-LSA would expire
    int lsa_timeout;

    // initial config
    char * config_file;
    char * file_list;

    // ports for communicate with peer daemon and flask app
    int rport, sport, lport;

    // linked list of all known objects
    struct ObjectInfo objects;

    // linked list of all known routing daemons, includes mine
    struct NodeInfo nodes;

    // my link state
    struct LSA * lsa;

    // neighbors
    struct NodeInfo neighbors;

    // all local objects
    struct LocalObject local_objects;

    // fd mask for select()
    int max_fd;
    fd_set ready_read_set;
    fd_set read_set;
    int ready_cnt;

    // tcp connection from flask app
    int conn_fd[MAX_CONNECT];
    // read buffer for tcp connection
    struct ReadBuffer * connect_buf[MAX_CONNECT];

    // for accepting tcp connection from flask app
    int local_fd;
    // listening udp port
    int routing_fd;
};


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
