#ifndef RD_H

#define RD_H

#define MAX_CONNECT 1024

struct RouteDaemon;

struct RouteDaemon * get_new_rd(char ** config);

int daemon_serve(struct RouteDaemon * rd, int * running);

int close_daemon(struct RouteDaemon * rd);

#endif
