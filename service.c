/*
 * File: service.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>

#include "service.h"
#include "util.h"

#define bufsize 5



void handle_client(int socket) {
    
    /* TODO Loop receiving requests and sending appropriate responses,
     *      until one of the conditions to close the connection is
     *      met.
     */
	TRACE
	//request message - accumulates unitl \r\n\r\n
	char* msgbuf;
	//request body if any
	const char *abody;
	const char* body;
	//recv parameter
	int flag=0;
	//request method
	http_method method;
	//request path
	const char* path;

	char* value;
	//int len;
	int contentlength;


	// if true will loop waiting for more data
	int persist_connection=1;
	// connectionheader value recived
	char* connection_value;

	unsigned int mbufsize = bufsize;
	unsigned int *msgbufsize= &mbufsize;
	msgbuf=(char*) malloc(*msgbufsize);
	memset(msgbuf,'\0',sizeof(msgbuf));


	int bytesin = recv(socket, msgbuf, sizeof(msgbuf), flag);
	int msgsize = bytesin;
	fprintf(stderr, "$%2d:%s\n",bytesin, msgbuf);
	while((!message_has_newlines(msgbuf))&&(bytesin>0)){
		int sizeleft = *msgbufsize - msgsize-1;
		fprintf(stderr, "sizeleft = %d\n", sizeleft);
		while (sizeleft<bytesin){
			msgbuf=doubleBufferSize(msgbuf, msgbufsize);
			sizeleft= *msgbufsize - msgsize - 1;
		}
		char* appendbuf = msgbuf + msgsize;
		bytesin = recv(socket, appendbuf, sizeleft, flag);
		msgsize+=bytesin;
		fprintf(stderr, "$%2d:%2d:%s\n",msgsize, bytesin, msgbuf);
		fprintf(stderr, "strlen msgbug = %d\n", strlen(msgbuf)	);
	}

	//now have complete first part of message since we have blank line ie \r\n\r\n

	if (bytesin==0){
		fprintf(stderr, "remote closed connection, child closing\n");
		return;
	}
	fprintf(stderr, "received:$%s$\n",msgbuf);
	connection_value=http_parse_header_field(msgbuf, MAXHDRSEARCHBYTES,header_connect );
	fprintf(stderr, "connection: %s\n", connection_value);
	if ((is_httpVer_1_0(msgbuf))||
			(strncasecmp(connection_value,"close", 10)==0)){
		//either http/1.0 or request for not persistent
		fprintf(stderr,"Will NOT persist connection\n");
		//todo: should send "Connection: close"   (8.1.2.1)
		persist_connection=0;
	}else {
		fprintf(stderr,"Connection: keep-alive, persisting connection\n");
		persist_connection=1;

	}

	while (bytesin>0){
		TRACE
		method = http_parse_method(msgbuf);
		fprintf(stderr,  "method=%d, %s\n ", method, http_method_str[method]);

		switch (method){

		case METHOD_GET:
			path = http_parse_path(http_parse_uri(msgbuf));
			fprintf(stderr, "path=%s\n", path );

			//extract attributes?

			break;
		case METHOD_POST:
			//handle partial request

			path = http_parse_path(msgbuf);
			fprintf(stderr, "path=%s\n", path );
			value = http_parse_header_field(msgbuf,sizeof(msgbuf),(const char*)"Content-length");
			contentlength=atoi(value);
			// is this count include /r/n - exclude headers?
			body = http_parse_body(msgbuf,contentlength);

			abody = http_parse_body(msgbuf,bufsize);
			if (abody == NULL){
				fprintf(stderr,"nobody %d \n", sizeof(abody));
			}else{
				fprintf(stderr, "body = %s\n\n",abody);
			}


			break;

		case METHOD_HEAD:
		case METHOD_OPTIONS:
		case METHOD_PUT:
		case METHOD_DELETE:
		case METHOD_TRACE:
		case METHOD_CONNECT:
			fprintf(stderr,"Unspported Method called %s \n", http_method_str[method]);
			break;
		case METHOD_UNKNOWN:
			fprintf(stderr,"unknown method called %d \n", method);
			break;
		default:
			break;
		}

		if (!persist_connection){
			TRACE
			break;
		}
		memset(msgbuf,'\0',sizeof(msgbuf));
		bytesin = recv(socket, &msgbuf, bufsize, flag);
	}
	TRACE
    return;
}
