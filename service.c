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

#define bufsize 8192

void handle_client(int socket) {
    
    /* TODO Loop receiving requests and sending appropriate responses,
     *      until one of the conditions to close the connection is
     *      met.
     */
	char buf[bufsize];
	const char *abody;
	int flag=0;
	http_method method;
	const char* path;
	char* value;
	//int len;
	int contentlength;
	const char* body;

	// if true will loop waiting for more data
	int persist_connection=1;
	// connectionheader value recived
	char* connection_value;

	buf


	memset(buf,'\0',sizeof(buf));

	int bytesin = recv(socket, &buf, bufsize, flag);



	if (bytesin==0){
		fprintf(stderr, "remote closed connection, child closing\n");
		return;
	}
	fprintf(stderr, "received:$%s$\n",buf);
	connection_value=http_parse_header_field(buf, MAXHDRSEARCHBYTES,header_connect );
	fprintf(stderr, "connection: %s\n", connection_value);
	if ((is_httpVer_1_0(buf))||
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
		method = http_parse_method(buf);
		fprintf(stderr,  "method=%d, %s\n ", method, http_method_str[method]);

		switch (method){

		case METHOD_GET:
			buf[bytesin+1]='\0';
			int morebytes=0;
			while (!message_has_newlines(buf)&&morebytes){
				int nextbyte = bytesin;
				morebytes = recv(socket,&buf[bytesin], bufsize-bytesin,flag);
				nextbyte=bytesin+morebytes;
				fprintf(stderr,"incomplete get message, recv more bytes, in=%d, new=%d, out=%d\n",
						bytesin, morebytes, nextbyte);
				int i=0;
				for (i=0;i<nextbyte;i++){
						printf("\t%d  %c  %x\n", i, buf[i], buf[i]);
				}

				buf[nextbyte+1]='\0';
				fprintf(stderr,"msg: %s", buf);
			}

			path = http_parse_path(http_parse_uri(buf));
			fprintf(stderr, "path=%s\n", path );

			//extract attributes?

			break;
		case METHOD_POST:
			//handle partial request

			path = http_parse_path(buf);
			fprintf(stderr, "path=%s\n", path );
			value = http_parse_header_field(buf,sizeof(buf),(const char*)"Content-length");
			contentlength=atoi(value);
			// is this count include /r/n - exclude headers?
			body = http_parse_body(buf,contentlength);

			abody = http_parse_body(buf,bufsize);
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
		memset(buf,'\0',sizeof(buf));
		bytesin = recv(socket, &buf, bufsize, flag);
	}
	TRACE
    return;
}
