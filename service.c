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

#define bufsize 64000

void handle_client(int socket) {
    
    /* TODO Loop receiving requests and sending appropriate responses,
     *      until one of the conditions to close the connection is
     *      met.
     */
	char buf[bufsize];
	int flag=0;
	http_method method;
	const char* path;
	char* value;
	//int len;
	int contentlength;
	const char* body;
	int bytesin = recv(socket, &buf, bufsize, flag);
	if (bytesin==0)
		fprintf(stderr, "remote socket closed\n");
	else{

		method = http_parse_method(buf);
		fprintf(stderr,  "method=%d, %s\n ", method, http_method_str[method]);

		switch (method){

		case METHOD_GET:
			path = http_parse_path(buf);
			fprintf(stderr, "path=%s\n", path );
			//extract attributes?

			break;
		case METHOD_POST:
			path = http_parse_path(buf);
			fprintf(stderr, "path=%s\n", path );
			value = http_parse_header_field(buf,sizeof(buf),(const char*)"Content-length");
			contentlength=atoi(value);
			// is this count include /r/n - exclude headers?
			body = http_parse_body(buf,contentlength);


			break;
		case METHOD_HEAD:
			break;
		case METHOD_OPTIONS:
			break;
		case METHOD_PUT:
			break;
		case METHOD_DELETE:
			break;
		case METHOD_TRACE:
			break;
		case METHOD_CONNECT:
			break;
		case METHOD_UNKNOWN:
			break;
		default:
			break;
		}


	}

    return;
}
