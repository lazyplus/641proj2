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
    ./static          local static files for website
    ./test            files for testing


[RUN] 
---------------------------------------
   Run flask server
       ./webserver <server-port>
   Run route daemon
       ./routed <nodeid> <config file> <file list> <adv cycle time> <LSA timeout> <neighbor timeout> <retran timeout>

