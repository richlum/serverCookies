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
#include <time.h>
#include "service.h"
#include "util.h"

#define bufsize 256

const char* responsestr[] ={
		"100 Continue",
		"101 Switching Protocols",
		"200 OK",
		"201 Created",
		"202 Accepted",
		"203 Non-Authoritative Information",
		"204 No Content",
		"205 Reset Content",
		"206 Partial Content",
		"300 Multiple Choices",
		"301 Moved Permanently",
		"302 Found",
		"303 See Other",
		"304 Not Modified",
		"305 Use Proxy",
		"307 Temporary Redirect",
		"400 Bad Request",
		"401 Unauthorized",
		"402 Payment Required",
		"403 Forbidden",
		"404 Not Found",
		"405 Method Not Allowed",
		"406 Not Acceptable",
		"407 Proxy Authentication Required",
		"408 Request Timeout",
		"409 Conflict",
		"410 Gone",
		"411 Length Required",
		"412 Precondition Failed",
		"413 Request Entity Too Large",
		"414 Request-URI Too Long",
		"415 Unsupported Media Type",
		"416 Requested Range Not Satisfiable",
		"417 Expectation Failed",
		"500 Internal Server Error",
		"501 Not Implemented",
		"502 Bad Gateway",
		"503 Service Unavailable",
		"504 Gateway Timeout",
		"505 HTTP Version Not Supported"
};

typedef enum {
    CMDLOGIN, CMDLOGOUT,CMDSERVERTIME,CMDBROWSER,CMDREDIRECT,
    CMDGETFILE,CMDPUTFILE,CMDADDCART,CMDDELCART,CMDCHECKOUT,
    CMDCLOSE
} servcmd;

const char* server_commands[] = {
		"login",
		"logout",
		"servertime",
		"browser",
		"redirect",
		"getfile",
		"putfile",
		"addcart",
		"delcart",
		"checkout",
		"close"
};

const char* httpver = "HTTP/1.1";
const char* default_http_connection = "Connection: keep-alive";
const char* default_http_contenttype = "Content-Type: text/plain";
const char* default_http_cache_control = "Cache-Control: public";


void hexprint(const char* buf, int len){

	int i ;
	for (i=0;i<len;i++){
		printf("\t%2d %x %c\n", i, buf[i], buf[i]);
	}
}

//release allocated resources for this connection
void release_connection_resources(char* msgbuf){
	if (msgbuf)
		free(msgbuf);
	msgbuf=NULL;

}
//search for content-length header field and return value
int getContentLength(char* msgbuf,int length){
	char* c_len =  http_parse_header_field(msgbuf, length, "Content-Length");
	if (c_len)
		return atoi(c_len);
	return 0;
}

//calculate the size of the header based upon finding
// the crlf pair.  Includes the crlf pair as part of size
// if no crlf pair found, return 0;
int getSizeofHeader(char* msgbuf){

	char* p = strstr(msgbuf, MESSAGE_TERMINATOR);

	if(p){
		DBGMSG ("offset of start of crlf term = %d\n", (int) (p-msgbuf));
		return (int)(p-msgbuf+4);
	}
	DBGMSG("Header not terminated yet :%d\n",__LINE__);
	return 0;
}


int command_from_string(const char* path){
	int i;
	//fist char should be a slash that can be ignored
	//all commands must be relative to root
	const char* p=path+1;
	for (i=0; i< CMDCLOSE; i++){
		if (!strncasecmp(p, server_commands[i], strlen(server_commands[i]))){
			//matched
			return i;
		}
	}
	//not found
	return -1;
}


char* addheader(char* to, int respidx){
	strcat(to,httpver	);
	strcat(to, " ");
	strcat(to, responsestr[respidx]);
	strcat(to,"\r\n");
	return to;

}
// add given field into http response
// allocate more size if required.
//
char* addfield(char* to, const char* from, unsigned int* to_bufsize){
	while ((*to_bufsize-strlen(to))<=(strlen(from)+3)){
		doubleBufferSize(to,to_bufsize);
	}
	strcat(to,from	);
	strcat(to,"\r\n");
	return to;
}

// get the date for http respone field output
char* get_default_http_date(char* str,int len){
	time_t timer = time(&timer);
	char* fmt ="%a %b %d %H:%M:%S %Z %Y";
	struct tm* currtime = gmtime(&timer);
	strftime(str,len,fmt,currtime);
	DBGMSG("timestamp = %s\n",str);
	return str;
}


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
	//const char* body;
	//recv parameter
	int flag=0;
	//request method
	http_method method;
	//request path
	const char* path;

//	char* value;
	//int len;
	int content_length;


	// if true will loop waiting for more data
	int persist_connection=1;
	// connectionheader value recived
	char* connection_value;

	unsigned int mbufsize = bufsize;
	unsigned int *msgbufsize= &mbufsize;
	msgbuf=(char*) malloc(*msgbufsize);
	int bytesin=0;
	do{
		memset(msgbuf,'\0', *msgbufsize);
		TRACE
		DBGMSG("sizeof msgbuf = %d\n", *msgbufsize);

		bytesin = recv(socket, msgbuf, *msgbufsize, flag);
		if (bytesin==0){
			fprintf(stderr, "remote closed connection, child closing\n");
			release_connection_resources(msgbuf);
			return;
		}
		if (bytesin<0)
			perror("recv error:");

		int msgsize = bytesin;
		fprintf(stderr, "$%2d:%s\n",bytesin, msgbuf);
		int sizeleft=0;

	/***** recv header until complete ***********/
		while((!message_has_newlines(msgbuf))&&(bytesin>0)){
			TRACE
			sizeleft = *msgbufsize - msgsize-1;
			fprintf(stderr, "sizeleft = %d\n", sizeleft);
			//hexprint(msgbuf, msgsize+2);
			while (sizeleft<bytesin){
				msgbuf=doubleBufferSize(msgbuf, msgbufsize);
				sizeleft= *msgbufsize - msgsize - 1;
			}
			char* appendbuf = msgbuf + msgsize;
			bytesin = recv(socket, appendbuf, sizeleft, flag);
			if (bytesin==0){
				fprintf(stderr, "remote closed connection, child closing\n");
				release_connection_resources(msgbuf);
				return;
			}
			if (bytesin<0)
				perror("recv error while fetching rest of headers:");
			msgsize+=bytesin;
			fprintf(stderr, "$%2d:%2d:%s\n",msgsize, bytesin, msgbuf);
			fprintf(stderr, "strlen msgbug = %d\n", (int)strlen(msgbuf)	);
		}

		hexprint(msgbuf, msgsize+2);
		DBGMSG("complete message %d\n" , __LINE__);
		//now have complete first part of message since we have blank line ie \r\n\r\n
		fprintf(stderr, "received:$%s$\n",msgbuf);
		int sizeofheader = getSizeofHeader(msgbuf);
		DBGMSG("sizeofHeader = %d\n", sizeofheader);
		//int sizeofheader = getSizeofHeader(msgbuf);
		sizeofheader = http_header_complete(msgbuf, msgsize);
		DBGMSG("sizeofHeader = %d\n", sizeofheader);

	/****   get rest of body (if any) **************/
		content_length = getContentLength(msgbuf,msgsize);
		if (content_length>0){
			DBGMSG("content length = %d\n",content_length);
			int read = msgsize - sizeofheader;
			if (sizeleft<content_length-read){
				// incr size should be min of contentlength-read-sizeleft
				msgbuf = increaseBufferSizeBy(msgbuf,msgbufsize, content_length);
				sizeleft= *msgbufsize - msgsize -1;
			}
			TRACE
			DBGMSG("read = %d\n",read);
			DBGMSG("msgsize = %d\n",msgsize);

			while(read<content_length){
				//get the missing body parts
				DBGMSG("missing %d bytes from body",content_length-read);
				char* appendbuf = msgbuf + msgsize;
				bytesin = recv(socket, appendbuf, sizeleft, flag);
				if (bytesin==0){
					fprintf(stderr, "remote closed connection, child closing\n");
					release_connection_resources(msgbuf);
					return;
				}
				if (bytesin<0)
					perror("recv error:");
				read+=bytesin;
				msgsize+=bytesin;

			}
		}
		//now we have complete header and body (if any)

	/***** parse command ********************/
		TRACE

		//connection
		connection_value=http_parse_header_field(msgbuf, MAXHDRSEARCHBYTES,header_connect );
		if ((is_httpVer_1_0(msgbuf))||
				((connection_value)&&(strncasecmp(connection_value,"close", 10)==0))){
			//either http/1.0 or request for not persistent
			fprintf(stderr, "connection: %s\n", connection_value);
			fprintf(stderr,"Will NOT persist connection\n");
			//todo: should send "Connection: close"   (8.1.2.1)
			persist_connection=0;
		}else {
			fprintf(stderr,"persisting connection\n");
			persist_connection=1;

		}
		TRACE

		//method
		method = http_parse_method(msgbuf);
		fprintf(stderr,  "method=%d, %s\n ", method, http_method_str[method]);

		switch (method){

		case METHOD_GET:
			path = http_parse_path(http_parse_uri(msgbuf));
			fprintf(stderr, "path=%s\n", path );

			//extract attributes?
			TRACE
			break;
		case METHOD_POST:
			//handle partial request

			path = http_parse_path(msgbuf);
			fprintf(stderr, "path=%s\n", path );
			//char* value = http_parse_header_field(msgbuf,*msgbufsize,(const char*)"Content-length");
	//			contentlength=atoi(value);
	//			// is this count include /r/n - exclude headers?
	//			body = http_parse_body(msgbuf,contentlength);

			abody = http_parse_body(msgbuf,bufsize);
			if (abody == NULL){
				//todo fix this : sizeof will not work
				fprintf(stderr,"nobody %d \n", (int)sizeof(abody));
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

		TRACE
	/********** build response ******************************/
		TRACE
		hexprint(path, strlen(path)+2);
		int respindex;
		int cmd = command_from_string(path);
		switch (cmd){
		case CMDLOGIN:
			break;
		case CMDLOGOUT:
			break;
		case CMDSERVERTIME:
			TRACE
			respindex=2;
			break;
		case CMDBROWSER:
			break;
		case CMDREDIRECT:
			break;
		case CMDGETFILE:
			break;
		case CMDPUTFILE:
			break;
		case CMDADDCART:
			break;
		case CMDDELCART:
			break;
		case CMDCHECKOUT:
			break;
		case CMDCLOSE:
			break;
		case -1:
		default:
			break;
		}

		char* response;
		if (respindex>0){
			response = (char*)malloc(bufsize);
			memset(response,'\0',bufsize);
			unsigned int   responsebuffersize=bufsize;
			response = addheader(response,respindex);
			response = addfield(response, default_http_connection,&responsebuffersize);
			response = addfield(response, default_http_contenttype,&responsebuffersize);
			response = addfield(response, default_http_cache_control,&responsebuffersize);
			char timestr[bufsize];
			response = addfield(response, get_default_http_date(timestr,bufsize),&responsebuffersize);
		}else{
			//todo we always need some kind of response?
		}

		DBGMSG("RESPONSE: $%s", response);

	/*********  send response *******************************/
		TRACE
		int sent=0;
		int bytesout = send(socket,response,strlen(response),flag);
		if (bytesout==0){
			fprintf(stderr, "remote closed connection, child closing\n");
			release_connection_resources(msgbuf);
			return;
		}else if (bytesout<0){
			perror("send error:");
		}else {// if (bytesout>0){
			while(bytesout<(int)strlen(response)){
				sent+=bytesout;
				bytesout = send(socket,response+sent,strlen(response+sent),flag);
				if (bytesout==0){
							fprintf(stderr, "remote closed connection, child closing\n");
							release_connection_resources(msgbuf);
							release_connection_resources(response);
							return;
				}
				if (bytesout<0)
					perror("recv error:");
			}
		}

		if (!persist_connection){
			TRACE
			release_connection_resources(msgbuf);
			release_connection_resources(response);
			break;
		}
		TRACE
	} while( bytesin >0);




}
