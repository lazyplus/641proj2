#!/usr/bin/env python

from flask import Flask, redirect, url_for, request, Response
import sys
import os
import hashlib
import socket
import urllib
import flask
import re

app = Flask(__name__)
rd_host = "localhost"

def parse(ss):
    if (ss[0:2] == "OK"):
        return 0
    else:
        return 1
    
@app.route('/')
def index():
    return redirect(url_for('static', filename='index.html'))

@app.route('/rd/<int:p>', methods=["GET"])
def rd_getrd(p):
    #1. Figure out the <object-name> from the request
    request_obj = request.args.get('object')
    print "Object:" + request_obj
    #2. Connect to the routing daemon on port p
    request_line = "GETRD " + str(len(request_obj)) + " " + request_obj
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    #sock.setblocking(0)
    print "local port: " + str(p)
    local_port = p
    sock.connect((rd_host, local_port))
    #print sock
    #3. Do GETRD <object-name>
    sock.send(request_line) 
    print "Request: " + request_line
    #4. Parse the response from the routing daemon
    rd_response_buff = ""
    # ret = rd_response_buff.find("\r\n")
    # while (ret == -1):
    recv_buff = sock.recv(1024)
    print "APP Get " + recv_buff
    # rd_response_buff = rd_response_buff + recv_buff
    print recv_buff
    recv_arr = re.split("\s+", recv_buff)
    if len(recv_arr) == 2:
        return "Content not found"
    rd_response = recv_arr[2]
    # rd_response = rd_response_buff[:ret+2]
    # rd_response_buff = rd_response_buff[ret+2:]
    print rd_response    
    #rd_response = "OK 25 http://localhost:9999/static/index.html"
    # L = rd_response.split(' ')    
    L = recv_arr
    if (L[0] == "OK"):
        print L[2]
        result = urllib.urlopen(L[2])
        file_content = result.read()
        response = flask.make_response(file_content)
        for header_line in str(result.info()).split("\r\n"):
            if (header_line != ""):
                pair = header_line.split(":")
                key = pair[0]
                value = pair[1]
                response.headers[key] = value
        if (L[2].find("http://localhost:") == -1):
            # file not exist in local host
            file_name = request_obj
            local_file = open("static/" + hashlib.sha256(file_name).hexdigest(), "w")
            local_file.write(file_content)
            local_file.close()
            request_line = "ADDFILE " + str(len(file_name)) + " " + file_name + " " + str(len("/static/" + hashlib.sha256(file_name).hexdigest())) + " /static/" + hashlib.sha256(file_name).hexdigest()
            sock.send(request_line)
            print "Send:" + request_line
            # sock.recv(1024)
            recv_buff = sock.recv(1024)
            print "APP Get " + recv_buff
            if(recv_buff.find("OK") == -1):
                return "Add local file failed"
        return response
    else:
        return "Content not available"
    #4 a)If response is OK <URL>, the open the URL
    #4 b) If response is 404, then show that content is not available
    #### You may factor out things from here and rd_getrd() function and form a separate sub-routine
    
    #return "Unimplemented"
            

@app.route('/rd/addfile/<int:p>', methods=["POST"])
def rd_addfile(p):
    local_port = p
    #1. Figure out the object-name and the file details/content from the request
    file_name = request.form.get('object')
    f = request.files['uploadFile']        
    #2. Find the sha256sum of the file content
    #3. Save the file in the static directory under the sha256sum name and compute the relative path
    f.save("static/" + hashlib.sha256(file_name).hexdigest())
    #4. Connect to the routing daemon on port p
    request_line = "ADDFILE " + str(len(file_name)) + " " + file_name + " " + str(len("/static/" + hashlib.sha256(file_name).hexdigest())) + " /static/" + hashlib.sha256(file_name).hexdigest() + "\r\n"
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.connect((rd_host, local_port))
    #5. Do ADDFILE <object-name> <relative-path> 
    sock.send(request_line)
    #6. Based on the response from the routing daemon display whether the object has been successfully uploaded/added or not 
    rd_response_buff = ""
    ret = rd_response_buff.find("\r\n")
    while (ret == -1):
        recv_buff = sock.recv(1024)
        rd_response_buff = rd_response_buff + recv_buff
        ret = rd_response_buff.find("\r\n")
        if (len(rd_response_buff) > 10*1024):
            print "No EOF found"
            exit()
    rd_response = rd_response_buff[:ret+2]
    rd_response_buff = rd_response_buff[ret+2:]
    print rd_response
    L = rd_response.split(' ')    
    if (L[0] == "OK"):
        print L[1]
    else:
        print "Content not available"
    sock.close()    
    return "Unimplemented"


@app.route('/rd/<int:p>/<obj>', methods=["GET"])
def rd_getrdpeer(p, obj):
    #1. Figure out the <object-name> from the request
    local_port = p
    request_obj = obj
    print "Object:" + request_obj + " port:" + str(p)
    #2. Connect to the routing daemon on port p
    request_line = "GETRD " + str(len(request_obj)) + " " + request_obj
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    #sock.setblocking(0)
    print "Conn: " + rd_host + ":" + str(local_port)
    sock.connect((rd_host, local_port))
    #print sock
    #3. Do GETRD <object-name>
    sock.send(request_line) 
    print "Request: " + request_line
    #4. Parse the response from the routing daemon
    rd_response_buff = ""
    # ret = rd_response_buff.find("\r\n")
    # while (ret == -1):
    recv_buff = sock.recv(1024)
    # rd_response_buff = rd_response_buff + recv_buff
    print recv_buff
    recv_arr = re.split("\s+", recv_buff)
    rd_response = recv_arr[2]
    # rd_response = rd_response_buff[:ret+2]
    # rd_response_buff = rd_response_buff[ret+2:]
    print rd_response    
    #rd_response = "OK 25 http://localhost:9999/static/index.html"
    # L = rd_response.split(' ')    
    L = recv_arr
    if (L[0] == "OK"):
        print L[2]
        result = urllib.urlopen(L[2])
        response = flask.make_response(result.read())
        for header_line in str(result.info()).split("\r\n"):
            if (header_line != ""):
                pair = header_line.split(":")
                key = pair[0]
                value = pair[1]
                response.headers[key] = value
        return response
    else:
        return "Content not available"



if __name__ == '__main__':
    if (len(sys.argv) > 1):
        servport = int(sys.argv[1])
        app.run(host='0.0.0.0', port=servport, threaded=True, processes=1, debug=True)
    else:    
        print "Usage ./webserver <server-port> \n"
