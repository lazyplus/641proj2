/*
 * Route daemon header file
 * by Yu Su <ysu1@andrew.cmu.edu> Hanshi Lei <hanshil@andrew.cmu.edu>
 */

#ifndef RD_H

#define RD_H

#include "utility.h"

#define MAX_CONNECT 1024
#define BUF_SIZE 8192


// linked list for id
struct id_list{
    int id;
    struct list_head list;
};
void free_id_list(struct id_list * list);

// linked list for string
struct str_list{
    char * str;
    struct list_head list;
};
void free_str_list(struct str_list * list);

// Link State data structure
struct LSA{
    char version, ttl;
    short type;
    int sender_id, seq_num;
    int n_entries, n_objects;
    struct id_list entries;
    struct str_list objects;
};
struct LSA * unmarshal_LSA(char * buf, int len);
int marshal_LSA(struct LSA * lsa, char * buf, int len);
int print_LSA(struct LSA * lsa);
struct LSA * dup_LSA(struct LSA * lsa);
void free_LSA(struct LSA * lsa);

// Data structure for parsing and handing request
struct Request{
    enum{
        GET,
        ADD
    }type;
    int name_len;
    char * name;
    int path_len;
    char * path;
    enum{
        CMD = 0,
        NL,
        NAME,
        PL,
        PATH,
        FINISHED
    }state;
    int next_token_len;
};
void free_request(struct Request *r);

// Pending LSA waiting for ACK
struct ACKCD{
    struct LSA * lsa;
    int ack_count_down;
    struct list_head list;
};

// all info belongs to a node
struct NodeInfo{
    // basic
    int node_id;
    char * hostname;
    int rport, sport, lport;
    int active;

    // routing info
    int next_hop;
    int distance;

    // timeout
    int last_lsa;
    struct ACKCD ack;

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
void free_objects(struct ObjectInfo *objects);

struct LocalObject{
    char * name;
    char * path;

    struct list_head list;
};
void free_local_objects(struct LocalObject *objects);

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
    int announce_count_down;

    // neighbors
    struct id_list neighbors;

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
    struct ReadBuffer * conn_buf[MAX_CONNECT];
    struct Request conn_req[MAX_CONNECT];

    // for accepting tcp connection from flask app
    int local_fd;
    // listening udp port
    int routing_fd;
};

struct RouteDaemon * get_new_rd(char ** config);

int daemon_serve(struct RouteDaemon * rd, int * running);

int close_daemon(struct RouteDaemon * rd);

#endif
