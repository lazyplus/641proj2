#!/usr/bin/env python

from flask import Flask, redirect, url_for, request, Response
import sys
import os
import hashlib
import socket
import urllib
import flask

app = Flask(__name__)
rd_host = "unix11.andrew.cmu.edu"
local_port = 5000

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
	request_line = "GETRD " + str(len(request_obj)) + " " + request_obj + "\r\n"
	sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
	#sock.setblocking(0)
	sock.connect((rd_host, local_port))
	#print sock
	#3. Do GETRD <object-name>
	sock.send(request_line) 
	print "Request: " + request_line
	#4. Parse the response from the routing daemon
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
	#rd_response = "OK 25 http://localhost:9999/static/index.html"
	L = rd_response.split(' ')	
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
	#4 a)If response is OK <URL>, the open the URL
	#4 b) If response is 404, then show that content is not available
    #### You may factor out things from here and rd_getrd() function and form a separate sub-routine
	
	#return "Unimplemented"


@app.route('/rd/addfile/<int:p>', methods=["POST"])
def rd_addfile(p):
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
    	#1. Connect to the routing daemon on port p
	sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
	sock.connect((rd_host, local_port))
    	#2. Do GETRD <object-name>
	request_line = "GETRD " + str(len(obj)) + " " + obj + "\r\n"
	sock.send(request_line) 
	print "Request: " + request_line 
   	 #3. Parse the response from the routing daemon
    	#3 a) If response is OK <URL>, the open the URL
    	#3 b) If response is 404, then show that content is  not available
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
