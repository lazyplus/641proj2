############################
#  15641 Proj2 readme.txt  #
#  by hanshil & ysu1       #
###########################
[TOC] Table of Content
---------------------------------------
    [TOC] Table of Content
    [DES] Description of files
    [RUN] How to Run

[DES]
-----------------------------------------
    Our routing daemon is based on select() function with a timeout.
    If the new messages come before timeout, whether it is from the flask or other routing daemon, the 
    routing daemon will handle it accordingly. If timeout happens in the select(), the routing daemon will
    update the status of the topology accordingly.
    
    list.h            header file of linked list
    Makefile          makefile for all the code
    rd.h              header file of route daemon
    rd.c              source file of route daemon
    utility.h         header file for utility function
    utility.c         source file for utility function
    readme.txt        this file
    routed.c          program main entry for daemonize the route daemon
    run_routed.sh     bash for running the route daemoan
    webserver.py      flask server
    vulnerability.txt vulnerabilities
    test.txt          description of testing
    node1.conf        neighbor list of node 1
    node1.files       local file list of node 1
    node2.conf        neighbor list of node 
    node2.files       local file list of node 2
    node3.conf        neighbor list of node 3
    node3.files       local file list of node 3
    ./static          local static files for website
    ./test            files for testing


[RUN] 
---------------------------------------
   Run flask server
       ./webserver <server-port>
   Run route daemon
       ./routed <nodeid> <config file> <file list> <adv cycle time> <LSA timeout> <neighbor timeout> <retran timeout>

