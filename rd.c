/*
 * Route Daemon implementation
 * by Yu Su <ysu1@andrew.cmu.edu> Hanshi Lei <hanshil@andrew.cmu.edu>
 */

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
    memset(rd->conn_req, 0, sizeof(rd->conn_req));
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

    rd->announce_count_down = 1;

    write_log(INFO, "Allocated new routing daemon at %p", rd);
    return rd;
}

// close a routing daemon and release resources
int close_daemon(struct RouteDaemon * rd){
    free(rd->config_file);
    free(rd->file_list);

    free_objects(&(rd->objects));

    struct list_head * pos, * q;
    list_for_each_safe(pos, q, &(rd->nodes.list)){
        struct NodeInfo * n = list_entry(pos, struct NodeInfo, list);
        if(n->lsa)
            free_LSA(n->lsa);
        SAFE_FREE(n->hostname);
        list_del(pos);
        free(n);
    }

    free_id_list(&(rd->neighbors));

    free(rd);
    return 0;
}


/********************************
 * implementation details
 *******************************/
void shortest_path(struct RouteDaemon * rd);
void free_objects(struct ObjectInfo *objects){
    struct list_head *pos;
    list_for_each(pos, &(objects->list)){
        struct ObjectInfo *one_object = list_entry(pos, struct ObjectInfo, list);
        free(one_object->name);
        free_id_list(&(one_object->nodes));
    }
}

void free_local_objects(struct LocalObject *objects){
    struct list_head *pos, *q;
    list_for_each_safe(pos, q, &(objects->list)){
        struct LocalObject *one_object = list_entry(pos, struct LocalObject, list);
        free(one_object->name);
        free(one_object->path);
        list_del(pos);
        free(one_object);
    }
}

// generate the shortest path and object table
int calc_OSPF(struct RouteDaemon * rd){
    shortest_path(rd);

    // write_log(INFO, "Node %d Generating Object List", rd->node_id);
    struct list_head *pos_node;
    struct list_head *pos_node_object;
    struct list_head *pos_objects;
    // delect the old table
    free_objects(&(rd->objects));
    // init the new table
    INIT_LIST_HEAD(&(rd->objects.list)); 
    list_for_each(pos_node, &(rd->nodes.list)){
        // for every node
        struct NodeInfo* node_iter = list_entry(pos_node, struct NodeInfo, list);

        if(node_iter->lsa == NULL || node_iter->active == 0)
            continue;

        list_for_each(pos_node_object, &(node_iter->lsa->objects.list)){
            // for every object
            struct str_list* object_iter = list_entry(pos_node_object, struct str_list, list);
            int found = 0;
            list_for_each(pos_objects, &(rd->objects.list)){
                struct ObjectInfo * local_objects_iter = list_entry(pos_objects, struct ObjectInfo, list);
                if (!strcmp(local_objects_iter->name, object_iter->str)){
                    // object already exists
                    struct id_list * new_id = (struct id_list*)malloc(sizeof(struct id_list));
                    new_id->id = node_iter->node_id;
                    list_add(&(new_id->list), &(local_objects_iter->nodes.list));
                    found = 1;
                    break;
                }
            }

            if(found == 0){
                // write_log(INFO, "Adding New Object %s on %d", object_iter->str, node_iter->node_id);
                // new object
                struct ObjectInfo * new_obj = (struct ObjectInfo*)malloc(sizeof(struct ObjectInfo));
                new_obj->name = strdup(object_iter->str);
                INIT_LIST_HEAD(&(new_obj->nodes.list));
                struct id_list *first_node = (struct id_list*)malloc(sizeof(struct id_list));
                first_node->id = node_iter->node_id;
                list_add(&(first_node->list), &(new_obj->nodes.list));
                list_add(&(new_obj->list), &(rd->objects.list));
            }
        }
    }
    return 0;
}

int is_neighbor(struct RouteDaemon * rd, int id){
    struct list_head * pos;
    list_for_each(pos, &(rd->neighbors.list)){
        struct id_list * e = list_entry(pos, struct id_list, list);
        if(e->id == id)
            return 1;
    }
    return 0;
}

int construct_LSA(struct RouteDaemon * rd);
int send_LSA(struct RouteDaemon * rd, struct LSA * lsa, char * host, int port);
int flood_LSA(struct RouteDaemon * rd, struct LSA * lsa, int except_id);
int handle_timeout(struct RouteDaemon * rd){
    struct list_head * pos;

    /// resend LSA to neighbors didn't response ACK
    list_for_each(pos, &(rd->nodes.list)){
        struct NodeInfo * e = list_entry(pos, struct NodeInfo, list);

        if(!is_neighbor(rd, e->node_id) || e->active == 0)
            continue;

        struct list_head * pos2;
        list_for_each(pos2, &(e->ack.list)){
            struct ACKCD * ae = list_entry(pos2, struct ACKCD, list);
            if(ae->ack_count_down > 0){
                if(-- ae->ack_count_down == 0){
                    write_log(INFO, "Retransmit to node %d of %d", e->node_id, ae->lsa->sender_id);
                    send_LSA(rd, ae->lsa, e->hostname, e->rport);
                    ae->ack_count_down = rd->retran_timeout;
                }
            }
        }
    }

    /// expire too old neighbor LSA and announce the expiration
    /// delete too old LSA after all neighbors responded ACK (in get_ack)
    struct list_head * q;
    list_for_each_safe(pos, q, &(rd->nodes.list)){
        struct NodeInfo * e = list_entry(pos, struct NodeInfo, list);
        if(e->last_lsa > 0){
            if(-- e->last_lsa == 0){
                // list_del(pos);
                e->active = 0;
                calc_OSPF(rd);
                if(is_neighbor(rd, e->node_id) && e->lsa != NULL){
                    e->lsa->ttl = 0;
                    flood_LSA(rd, e->lsa, e->node_id);
                }
            }
        }
    }

    /// announce my LSA
    if(-- rd->announce_count_down == 0){
        construct_LSA(rd);
        flood_LSA(rd, rd->lsa, rd->node_id);
        rd->announce_count_down = rd->adv_timeout;
    }

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

int get_id(struct RouteDaemon * rd, char * host, int port);
int flood_LSA(struct RouteDaemon * rd, struct LSA * lsa, int except_id);
int get_ack(struct RouteDaemon * rd, struct LSA * lsa, int from);
int update_LSA(struct RouteDaemon * rd, struct LSA * lsa, int from);
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

        if(newLSA->type == 0){
            newLSA->type = 1;
            // write_log(INFO, "Sending ACK to %d for %d", get_id(rd, inet_ntoa(cli_addr.sin_addr), ntohs(cli_addr.sin_port)), newLSA->sender_id);
            send_LSA(rd, newLSA, inet_ntoa(cli_addr.sin_addr), ntohs(cli_addr.sin_port));
            newLSA->type = 0;
        }

        // print_LSA(newLSA);

        int node_id = get_id(rd, inet_ntoa(cli_addr.sin_addr), ntohs(cli_addr.sin_port));
        if(node_id == -1){
            write_log(WARN, "LSA from unknown peer %s:%d", inet_ntoa(cli_addr.sin_addr), ntohs(cli_addr.sin_port));
            return 0;
        }

        if(newLSA->type == 0)
            write_log(INFO, "Got LSA from %d(%d) by %d", newLSA->sender_id, newLSA->seq_num, node_id);

        // An ACK
        if(newLSA->type == 1){
            get_ack(rd, newLSA, node_id);

            // if I just recovered from crash
            if(newLSA->sender_id == rd->node_id){
                if(newLSA->seq_num > rd->lsa->seq_num)
                    rd->lsa->seq_num = newLSA->seq_num;
            }

            free_LSA(newLSA);
            return 0;
        }

        // An announcement 
        if(newLSA->type == 0){
            return update_LSA(rd, newLSA, node_id);
        }

        write_log(INFO, "Unrecognized LSA");
    }
    return 0;
}

int update_LSA(struct RouteDaemon * rd, struct LSA * lsa, int from){
    // handle new LSA (update or echo-back)
    // update: re-compute shortest path and flood
    // echo-back: send neighbor's last LSA back
    struct list_head *pos;
    struct NodeInfo *iterator;
    int f_found=0; 
    // Check if it is from a new node
    list_for_each(pos, &(rd->nodes.list)){
        iterator = list_entry(pos, struct NodeInfo, list);
        if(iterator->node_id == lsa->sender_id){
            f_found = 1;
            break;
        }
    }

    if(f_found){
        if(!iterator->active){
            iterator->active = 1;
        }
        if(iterator->lsa == NULL || lsa->seq_num > iterator->lsa->seq_num){
            // update lsa
            free_LSA(iterator->lsa);
            iterator->lsa = dup_LSA(lsa);
            iterator->lsa->ttl = 32;
            iterator->last_lsa = rd->lsa_timeout;
            if(lsa->ttl > 0){
                lsa->ttl -= 1;
                flood_LSA(rd, lsa, from);
            }
            // generate shortest path and object table
            calc_OSPF(rd);
        }else{
            if(is_neighbor(rd, lsa->sender_id)){
                // from neighbor, echo back
                iterator->lsa->type = 1;
                send_LSA(rd, iterator->lsa, iterator->hostname, iterator->rport);
                iterator->lsa->type = 0;
            }
        }
    }else{
        // New node, add to the nodes list
        struct NodeInfo *new_node = (struct NodeInfo *)malloc(sizeof(struct NodeInfo));
        new_node->hostname = NULL;
        new_node->node_id = lsa->sender_id;
        new_node->last_lsa = rd->lsa_timeout;
        new_node->distance = 32;
        new_node->lsa = lsa;
        new_node->active = 1;
        list_add(&(new_node->list), &(rd->nodes.list));
        // generate shortest path and object table
        calc_OSPF(rd);
    }
    return 0;
}

int feed_req(struct Request * req, char * buf, int len);
int process_req(struct RouteDaemon * rd, int conn_id);
int handle_cgi_req(struct RouteDaemon * rd){
    static char buf[BUF_SIZE];
    int i;
    for(i=0; i<MAX_CONNECT && rd->ready_cnt > 0; ++i){
        int fd = rd->conn_fd[i];
        if(fd == -1 || !FD_ISSET(fd, &rd->ready_read_set))
            continue;
        
        -- rd->ready_cnt;
        int ret_r, ret_parse = 0;
        int ret_fill = fill_read_buffer(rd->conn_buf[i]);
        write_log(INFO, "Read from connect_id %d bytes %d", i, ret_fill);
        if(ret_fill > 0){
            // In case one read gets more lines, loop here
            do{
                ret_r = read_token(rd->conn_buf[i], buf, BUF_SIZE, rd->conn_req[i].next_token_len);
                if(ret_r > 0){
                    ret_parse = feed_req( & rd->conn_req[i], buf, ret_r);
                    if(ret_parse == 0 && rd->conn_req[i].state == FINISHED){
                        process_req(rd, i);
                    }
                }
            }while(ret_r > 0 && ret_parse == 0);
        }
        if(ret_fill < 0 || ret_parse < 0){
            rm_cgi_conn(rd, i);
        }
    }
    return 0;
}

int feed_req(struct Request * req, char * buf, int len){
    write_log(INFO, "get token %s", buf);
    switch(req->state){
    case CMD:
        req->state = NL;
        if(buf[0] == 'G')
            req->type = GET;
        else
            req->type = ADD;
        break;
    case NL:
        req->state = NAME;
        sscanf(buf, "%d", &req->name_len);
        req->next_token_len = req->name_len;
        break;
    case NAME:
        SAFE_FREE(req->name);
        req->name = strdup(buf);
        if(req->type == GET)
            req->state = FINISHED;
        else
            req->state = PL;
        req->next_token_len = 0;
        break;
    case PL:
        req->state = PATH;
        sscanf(buf, "%d", &req->path_len);
        req->next_token_len = req->path_len;
        break;
    case PATH:
        SAFE_FREE(req->path);
        req->path = strdup(buf);
        req->state = FINISHED;
        req->next_token_len = 0;
        break;
    default:
        return 1;
    }

    return 0;
}

struct NextHop{
    int prefix, dist;
    int next;
    int lport, sport;
    int node_id;
};

int prefix(char * a, char * b, int * prefix){
    int n = strlen(a);
    int m = strlen(b);
    if(n < m && strncmp(a, b, n) == 0 && a[n-1] == '/'){
        * prefix = n;
        return 1;
    }else if(n == m && strcmp(a, b) == 0){
        * prefix = n;
        return 1;
    }
    * prefix = 0;
    return 0;
}

int response_cgi(struct RouteDaemon * rd, int connection_id, char * buf, int len);
int process_req(struct RouteDaemon * rd, int conn_id){
    static char rep[BUF_SIZE], b[BUF_SIZE];

    struct Request * req = & rd->conn_req[conn_id];
    req->state = CMD;
    if(req->type == GET){
        struct list_head * pos;
        list_for_each(pos, &(rd->local_objects.list)){
            struct LocalObject * tmp = list_entry(pos, struct LocalObject, list);
            if(strcmp(tmp->name, req->name) == 0){
                sprintf(b, "http://localhost:%d%s", rd->sport, tmp->path);
                int len = strlen(b);
                sprintf(rep, "OK %d %s", len, b);
                response_cgi(rd, conn_id, rep, strlen(rep));
                return 0;
            }
        }
        ///look up in remote
        struct NextHop res;
        res.prefix = 0;
        res.dist = -1;

        list_for_each(pos, &(rd->objects.list)){
            struct ObjectInfo * e = list_entry(pos, struct ObjectInfo, list);

            struct NextHop tmp;
            tmp.dist = -1;
            int ret = prefix(e->name, req->name, &tmp.prefix);
            write_log(INFO, "prefix: %s %s %d %d", e->name, req->name, ret, tmp.prefix);
            if(!prefix(e->name, req->name, &tmp.prefix))
                continue;
            if(tmp.prefix < res.prefix)
                continue;

            struct list_head * id_pos;
            list_for_each(id_pos, &(e->nodes.list)){
                struct id_list * id_e = list_entry(id_pos, struct id_list, list);
                int node_id = id_e->id;
                struct list_head * npos;
                list_for_each(npos, &(rd->nodes.list)){
                    struct NodeInfo * n = list_entry(npos, struct NodeInfo, list);
                    if(n->node_id == node_id){
                        if(tmp.dist == -1 || tmp.dist > n->distance || (tmp.dist == n->distance && n->node_id < tmp.node_id)){
                            tmp.dist = n->distance;
                            tmp.next = n->next_hop;
                            tmp.sport = n->sport;
                            tmp.lport = n->lport;
                            tmp.node_id = n->node_id;
                        }
                    }
                }
            }

            if(tmp.dist == -1)
                continue;

            if(res.dist == -1 || tmp.dist < res.dist || (tmp.dist == res.dist && tmp.node_id < res.node_id)){
                res = tmp;
            }            
        }

        write_log(INFO, "res: %d %d", res.next, res.dist);

        if(res.dist != -1){
            struct list_head * npos;
            list_for_each(npos, &(rd->nodes.list)){
                struct NodeInfo * n = list_entry(npos, struct NodeInfo, list);
                if(n->node_id == res.next){
                    sprintf(b, "http://%s:%d/rd/%d/%s", n->hostname, n->sport, n->lport, req->name);
                    int len = strlen(b);
                    sprintf(rep, "OK %d %s", len, b);
                    response_cgi(rd, conn_id, rep, strlen(rep));
                    return 0;
                }
            }
        }

        sprintf(rep, "NOTFOUND 0");
        response_cgi(rd, conn_id, rep, strlen(rep));
    }else{
        add_local_object(rd, req->name, req->path);
        sprintf(rep, "OK 0");
        response_cgi(rd, conn_id, rep, strlen(rep));

        /// update self-LSA and flood it
        construct_LSA(rd);
        flood_LSA(rd, rd->lsa, rd->node_id);
    }
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
    printf("%d %d %d\n%d %d\n%d %d\n", lsa->version, lsa->ttl, lsa->type, lsa->sender_id, lsa->seq_num, lsa->n_entries, lsa->n_objects);
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
        ni->next_hop = id;
        ni->distance = 1;
        ni->active = 1;
        ni->last_lsa = rd->neighbor_timeout;
        INIT_LIST_HEAD( & (ni->ack.list));
        list_add(&(ni->list), &(rd->nodes.list));
        struct id_list * nn = (struct id_list *) malloc(sizeof(struct id_list));
        nn->id = id;
        list_add(&(nn->list), &(rd->neighbors.list));
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
    init_read_buffer( & rd->conn_buf[i], fd);
    return i;
}

int rm_cgi_conn(struct RouteDaemon * rd, int id){
    write_log(INFO, "Close connection_id %d", id);
    close(rd->conn_fd[id]);
    FD_CLR(rd->conn_fd[id], & rd->read_set);
    free_read_buffer( & rd->conn_buf[id]);
    rd->conn_fd[id] = -1;
    return 0;
}

int send_LSA(struct RouteDaemon * rd, struct LSA * lsa, char * host, int port){
    static char buf[BUF_SIZE];

    int len = marshal_LSA(lsa, buf, BUF_SIZE);
    return send_packet(rd->routing_fd, host, port, buf, len);
}

struct LSA * dup_LSA(struct LSA * lsa);

int flood_LSA(struct RouteDaemon * rd, struct LSA * lsa, int except_id){
    struct list_head * pos, * apos;
    list_for_each(pos, & (rd->nodes.list)){
        struct NodeInfo * ne = list_entry(pos, struct NodeInfo, list);
        if(ne->node_id == except_id || !is_neighbor(rd, ne->node_id) || ne->active == 0)
            continue;

        int found = 0;
        list_for_each(apos, & (ne->ack.list)){
            struct ACKCD * ae = list_entry(apos, struct ACKCD, list);
            if(ae->lsa->sender_id == lsa->sender_id){
                free_LSA(ae->lsa);
                ae->lsa = dup_LSA(lsa);
                send_LSA(rd, ae->lsa, ne->hostname, ne->rport);
                ae->ack_count_down = rd->retran_timeout;
                found = 1;
            }
        }
        
        if(!found){
            struct ACKCD * ae = (struct ACKCD *) malloc(sizeof(struct ACKCD));
            ae->lsa = dup_LSA(lsa);
            ae->ack_count_down = rd->retran_timeout;
            send_LSA(rd, ae->lsa, ne->hostname, ne->rport);
            list_add( & (ae->list), & (ne->ack.list));
        }
    }
    return 0;
}

void free_id_list(struct id_list * list){
    struct list_head * pos, * q;
    list_for_each_safe(pos, q, &list->list){
         struct id_list * tmp = list_entry(pos, struct id_list, list);
         list_del(pos);
         free(tmp);
    }
}

void free_str_list(struct str_list * list){
    struct list_head * pos, * q;
    list_for_each_safe(pos, q, &list->list){
         struct str_list * tmp = list_entry(pos, struct str_list, list);
         list_del(pos);
         SAFE_FREE(tmp->str);
         free(tmp);
    }
}

void free_LSA(struct LSA * lsa){
    if(lsa == NULL)
        return;
    free_str_list( & lsa->objects);
    free_id_list( & lsa->entries);
    free(lsa);
}

struct LSA * dup_LSA(struct LSA * lsa){
    static char buf[BUF_SIZE];
    int len = marshal_LSA(lsa, buf, BUF_SIZE);
    return unmarshal_LSA(buf, len);
}

int get_id(struct RouteDaemon * rd, char * host, int port){
    struct list_head * pos;
    list_for_each(pos, & (rd->nodes.list)){
        struct NodeInfo * e = list_entry(pos, struct NodeInfo, list);

        if(port == e->rport && strcmp(e->hostname, host) == 0){
            return e->node_id;
        }
    }
    return -1;
}

int get_ack(struct RouteDaemon * rd, struct LSA * lsa, int from){
    struct list_head * pos;
    list_for_each(pos, & (rd->nodes.list)){
        struct NodeInfo * e = list_entry(pos, struct NodeInfo, list);

        // if(e->node_id == lsa->sender_id){
        //     e->last_lsa = rd->lsa_timeout;
        // }

        if(e->node_id != from)
            continue;
        struct list_head * apos, * q;
        list_for_each_safe(apos, q, & (e->ack.list)){
            struct ACKCD * ae = list_entry(apos, struct ACKCD, list);
            if(ae->lsa->sender_id == lsa->sender_id){
                // write_log(INFO, "Got ACK from %d for %d", from, lsa->sender_id);
                list_del(apos);
                free_LSA(ae->lsa);
                free(ae);

                e->last_lsa = rd->neighbor_timeout;
            }
        }
    }
    return 0;
}

int construct_LSA(struct RouteDaemon * rd){
    int seq_num = 0;
    if(rd->lsa)
        seq_num = rd->lsa->seq_num + 1;
    free_LSA(rd->lsa);

    rd->lsa = (struct LSA *) malloc(sizeof(struct LSA));
    rd->lsa->version = 1;
    rd->lsa->ttl = 32;
    rd->lsa->type = 0;
    rd->lsa->sender_id = rd->node_id;
    rd->lsa->seq_num = seq_num;
    rd->lsa->n_entries = 0;
    rd->lsa->n_objects = 0;
    INIT_LIST_HEAD( & (rd->lsa->entries.list));
    INIT_LIST_HEAD( & (rd->lsa->objects.list));

    struct list_head * pos;
    list_for_each(pos, &(rd->nodes.list)){
        struct NodeInfo * e = list_entry(pos, struct NodeInfo, list);
        if(e->active == 0 || !is_neighbor(rd, e->node_id))
            continue;

        struct id_list * ne = (struct id_list *) malloc(sizeof(struct id_list));
        ne->id = e->node_id;

        ++ rd->lsa->n_entries;
        list_add( & (ne->list), & (rd->lsa->entries.list));
    }

    list_for_each(pos, &(rd->local_objects.list)){
        struct LocalObject * e = list_entry(pos, struct LocalObject, list);
        struct str_list * ne = (struct str_list *) malloc(sizeof(struct str_list));
        ne->str = strdup(e->name);
        ++ rd->lsa->n_objects;
        list_add( & (ne->list), & (rd->lsa->objects.list));
    }
    return 0;
}

void shortest_path(struct RouteDaemon * rd){
    struct list_head *pos;
    struct list_head *v_pos;
    struct list_head *e_pos;
    struct list_head *v_pos2;

    list_for_each(pos, &(rd->nodes.list)){
        // for each V
        list_for_each(v_pos, &(rd->nodes.list)){
            struct NodeInfo *iterator = list_entry(v_pos, struct NodeInfo, list);

            if(iterator->active == 0)
                continue;

            if(iterator->lsa == NULL)
                continue;

            // for each E
            list_for_each(e_pos, &(iterator->lsa->entries.list)){
                struct id_list *each_edge = list_entry(e_pos, struct id_list, list);
                // find the corresponding V and compare the cost 
                list_for_each(v_pos2, &(rd->nodes.list)){
                    struct NodeInfo *iterator2 = list_entry(v_pos2, struct NodeInfo, list);
                    if(each_edge->id == iterator2->node_id){
                        if (iterator2->distance > iterator->distance + 1){
                            // a better path found
                            iterator2->distance = iterator->distance +1;
                            iterator2->next_hop = iterator->next_hop;
                        }
                        // work down, leave
                        break;
                    }
                }
            }
        }
    }
}
