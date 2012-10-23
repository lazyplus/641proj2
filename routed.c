/*
 * Main routine of route daemon
 * by Yu Su <ysu1@andrew.cmu.edu> Hanshi Lei <hanshil@andrew.cmu.edu>
 */

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <syslog.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include "rd.h"
#include "utility.h"

void usage(char * name){
    printf("Usage: %s <node ID> <config file> <file list> <adv cycle time> \
<neighbor timeout> <retran timeout> <LSA timeout>\n", name);
    exit(0);
}

int running = 1;

void signal_handler(int sig){
    switch(sig){
    case SIGHUP:
        /* rehash the server */
        break;          
    case SIGTERM:
        /* finalize and shutdown the server */
        running = 0;
        break;    
    default:
        /* unhandled signal */
        break;
    }
}

int daemonize(int argc, char ** argv){
    int pid = fork();
    if(pid < 0){
        write_log(ERROR, "Cannot fork working process");
        return 0;
    }
    if(pid > 0)
        return 0;

    setsid();

    // print log on screen
    set_log_fd(1);

    signal(SIGCHLD, SIG_IGN); /* child terminate signal */
    signal(SIGHUP, signal_handler); /* hangup signal */
    signal(SIGTERM, signal_handler); /* software termination signal from kill */

    struct RouteDaemon * rd = get_new_rd(argv);
    daemon_serve(rd, & running);
    write_log(INFO, "Closing Routing Daemon %p", rd);
    close_daemon(rd);
    return 0;
}

int main(int argc, char ** argv){
    if(argc != 8){
        usage(argv[0]);
    }

    daemonize(argc, argv);

    return 0;
}
