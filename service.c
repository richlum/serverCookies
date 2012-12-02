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
#include <assert.h>

#define bufsize 256
#define maxcookiesize 4096
#define MAXITEMS 12
#define MAXITEMLEN 64

const char* responsestr[] ={
		"100 Continue", //0
		"101 Switching Protocols", //1
		"200 OK",		//2
		"201 Created",	//3
		"202 Accepted",	//4
		"203 Non-Authoritative Information",
		"204 No Content",	//6
		"205 Reset Content",	//7
		"206 Partial Content",	//8
		"300 Multiple Choices",	//9
		"301 Moved Permanently",	//10
		"302 Found",
		"303 See Other",
		"304 Not Modified",
		"305 Use Proxy",
		"307 Temporary Redirect", //15
		"400 Bad Request",	//16
		"401 Unauthorized",  //17
		"402 Payment Required",
		"403 Forbidden",	//19
		"404 Not Found",	//20
		"405 Method Not Allowed",	//21
		"406 Not Acceptable",	//22
		"407 Proxy Authentication Required",
		"408 Request Timeout", //24
		"409 Conflict",	//25
		"410 Gone",	//26
		"411 Length Required"	//27,
		"412 Precondition Failed",
		"413 Request Entity Too Large",	//29
		"414 Request-URI Too Long",	//30
		"415 Unsupported Media Type",
		"416 Requested Range Not Satisfiable",
		"417 Expectation Failed",
		"500 Internal Server Error", //34
		"501 Not Implemented",  //35
		"502 Bad Gateway",
		"503 Service Unavailable",  //36
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
const char* close_http_connection ="Connection: close";
const char* default_http_contenttype = "Content-Type: text/plain";
const char* default_http_cache_control = "Cache-Control: public";
const char* nocache_http_cache_control = "Cache-Control: no-cache";
const char* lineend = "\r\n";
const char* default_http_cookie_opt = "; path=/; Max-Age=86400;";
const char* expirenow_http_cookie_opt = "; path=/; Max-Age=0;";
const char* default_http_cookie_header = "Set-Cookie: ";
const char* timeformatstr ="%a, %d %b %Y %T %Z";

typedef enum  {
	NOCONTENTTYPE, PLAIN, APPL_OCTET
}contenttype;
typedef enum {
	NOCONNECTION, CLOSE, KEEPALIVE
}connectiontype;
typedef enum{
	NOCACHECONTROL, NOCACHE, PRIVATE, PUBLIC
}cachecontrol;

//todo this was added in later.  so far contentlength is used
//cache is only used to set no-cache option.
// others and defaults ignore this structure for now.
typedef struct resp_setting{
	contenttype 	content;
	connectiontype  connection;
	cachecontrol	cache;
	int				contentlength;
} resp_setting;

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
	TRACE
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
	for (i=0; i<= CMDCLOSE; i++){
		if (!strncasecmp(p, server_commands[i], strlen(server_commands[i]))){
			DBGMSG("returning %i, %s\n", i, server_commands[i]);
			return i;
		}
	}
	DBGMSG("no command found, returning %i\n", -1);
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
		to=doubleBufferSize(to,to_bufsize);
	}
	strcat(to,from	);
	strcat(to,lineend);
	return to;
}

// get the date for http respone field output
char* get_default_http_date(char* str,int len){
	char timestr[bufsize];
	time_t timer = time(&timer);
	//char* fmt ="%a %b %d %H:%M:%S %Z %Y";
	struct tm* currtime = gmtime(&timer);
	strftime(timestr,bufsize,timeformatstr,currtime);
	strcpy (str, "Date: ");
	strcat (str, timestr);
	DBGMSG("datefield = %s\n",str);
	return str;
}


// get the date for http respone field output
char* get_localtime(char* str,int len){
	time_t timer = time(&timer);
	//char* fmt ="%a %b %d %H:%M:%S %Z %Y";
	struct tm* currtime = localtime(&timer);
	strftime(str,len,timeformatstr,currtime);
	DBGMSG("localtime = %s\n",str);
	return str;
}

//given content length, make an http response header field
char* get_http_content_length(int contlength,char* contlenstr){
	strcpy( contlenstr, "Content-Length: ");
	char sizestr[bufsize];
	sprintf(sizestr,"%d",contlength);
	strcat( contlenstr, sizestr);
	return contlenstr;
}

//given path that includes arguments
// extract the argument that has the given name
char* getargvalue(const char* argname, const char* path, char* value){
	TRACE
	char* p = strchr(path, '?');
	p++;
	DBGMSG("p=%s\n", p);
	DBGMSG("argname=%s\n",argname);
	int match;
	while(((match=(!strncasecmp(p,argname,strlen(argname))))==0)&&(p)){
		p=strchr(p,'&');
		if (p)
			p++;
		TRACE
		DBGMSG("p=%s\n", p);
	}
	if ((match)&&p){

		int i=0;
		p=strchr(p,'=');
		p++;
		TRACE
		DBGMSG("p=%s\n", p);
		while((*p!=' ')&&(*p!='&')&&(*p!='\r')&&(*p!='\0')){
			value[i++]=*p;
			p++;
		}
		value[i]='\0';
		TRACE
		DBGMSG("%d, value=%s\n", strlen(value), value);

		char* decodedvalue = (char*)malloc (strlen(value)*2);
		memset(decodedvalue,'\0',strlen(value)*2);
		decodedvalue = decode(value,decodedvalue);
		strcpy (value, decodedvalue);
		free(decodedvalue);
		decodedvalue=NULL;
		TRACE
		DBGMSG("%d, value=%s\n", strlen(value), value);
		return value;
	}
	return NULL;
}


char* getdecodedCookieAttribute(char* cookieptr, char* attribName, char* valuestore){
	TRACE
	DBGMSG("searching for (%d) attribute =%s\n", strlen(attribName),attribName);
	char* p = cookieptr;
	char* result;
	// expecting ptr to first attribute name or blanks preceding it.
	TRACE
	if (p==NULL)
		return NULL;
	DBGMSG("p=%s\n",p);
	while (*p==' '){
		p++;
	}
	TRACE
	DBGMSG("p=%s\n",p);
	DBGMSG("*p=%x %c\n",*p, *p);
	DBGMSG("attribName (%d) = %s\n", strlen(attribName), attribName);
//	int rc = strncasecmp(p,attribName,strlen(attribName));
//	DBGMSG("strncasecmp result = %d\n",rc );
	while((*p!='\0')&&(*p!='\r')&&(*p!='\n')
			&&(strncasecmp(p,attribName,strlen(attribName))!=0)){
		p=strchr(p,';');
		TRACE
		DBGMSG("p=%s\n",p);
		if (!p){
			TRACE
			return NULL;
		}
		p++;
		DBGMSG("p=%s\n",p);
		while((*p==' '))
			p++;
	}
	//have ptr to attribute name
	TRACE
	DBGMSG("p=%s\n",p);
	if ((*p!='\0')&&(*p!='\r')&&(*p!='\n')){
		p=strchr(p,'=');
		p++;
		//finally have *p pointing to value of attribName
		int i=0;
		char value[bufsize];
		while((*p!='\0')&&(*p!='\r')&&(*p!='\n')&&(*p!=';')){
			value[i++]=*p;
			p++;
		}
		value[i]='\0';
		assert (i<=bufsize);
		result = decode(value, valuestore) ;
		DBGMSG("decoded attribute=%s\n",result);
		return result;
	}
	TRACE
	return NULL;
}


//make a string "item1" given the number
char* getItemLabel(int index, char* buffer){
	strcpy(buffer, "item\0");
	char itemnumberstr[bufsize];
	sprintf(itemnumberstr,"%d",index);
	strcat (buffer,itemnumberstr);
	return buffer;
}


//items is array of items string
//itemcount is qty of items
//cartbody is buffer to hold string result that will be a part of http response body
char* buildCartBody(char* items, int itemcount, char* cartbody){
	int i;
	char countstr[bufsize];
	char* cstr=countstr;
	for (i=0; i<=itemcount;i++){
		char* p = &items[i*MAXITEMLEN];
		sprintf(cstr,"%d",i);
		strcat(cartbody,cstr);
		strcat(cartbody, ". ");
		strcat(cartbody,p);
		strcat(cartbody,lineend);
	}
	//strcat(cartbody,"\0");
	return cartbody;
}



void handle_client(int socket) {
    
    /* TODO REFACTOR This.
     * It's one big ugly do loop but wasnt sure what I needed
     * access to during developement.  Once it works, we need to
     * pull up some methods to make it more readable and define
     * interfaces for sub methods.
     */
	TRACE

	//request body if any
	const char *abody;

	//socket recv parameter
	int flag=0;
	//request method
	http_method method;
	//request path
	const char* path;
	// if true will loop waiting for more data
	int persist_connection=1;
	// connectionheader value recived
	char* connection_value;
	//input buffer management
	//request message - accumulates unitl \r\n\r\n
	char* msgbuf;
	unsigned int mbufsize = bufsize;
	unsigned int *msgbufsize= &mbufsize;
	msgbuf=(char*) malloc(*msgbufsize);
	int bytesin=0;
	//resp structure for control of output
	resp_setting resp;



	int inContentLen = 0;

	do{
		resp = (resp_setting){ 0,0,0,0};

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

		//hexprint(msgbuf, msgsize+2);
		DBGMSG("complete message %d\n" , __LINE__);
		//now have complete first part of message since we have blank line ie \r\n\r\n
		fprintf(stderr, "received:$%s$\n",msgbuf);
		int sizeofheader = getSizeofHeader(msgbuf);
		DBGMSG("sizeofHeader = %d\n", sizeofheader);
		//int sizeofheader = getSizeofHeader(msgbuf);
		sizeofheader = http_header_complete(msgbuf, msgsize);
		DBGMSG("sizeofHeader = %d\n", sizeofheader);

	/****   get rest of body (if any) **************/
		//content_length = getContentLength(msgbuf,msgsize);
		inContentLen = getContentLength(msgbuf,msgsize);
		if (inContentLen>0){
			DBGMSG("content length = %d\n",resp.contentlength);
			int read = msgsize - sizeofheader;
			if (sizeleft<inContentLen-read){
				// incr size should be min of contentlength-read-sizeleft
				msgbuf = increaseBufferSizeBy(msgbuf,msgbufsize, inContentLen);
				sizeleft= *msgbufsize - msgsize -1;
			}
			TRACE
			DBGMSG("read = %d\n",read);
			DBGMSG("msgsize = %d\n",msgsize);

			while(read<inContentLen){
				//get the missing body parts
				DBGMSG("missing %d bytes from body",inContentLen-read);
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
		int respindex=-1;
		path = http_parse_path(http_parse_uri(msgbuf));
		switch (method){

		case METHOD_GET:
			DBGMSG("path=%s\n", path );
			TRACE
			break;
		case METHOD_POST:
			//handle partial request
			DBGMSG("path=%s\n", path );
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
			respindex=34;
			break;
		case METHOD_UNKNOWN:
			fprintf(stderr,"unknown method called %d \n", method);
			respindex=34;
			break;
		default:


			break;
		}

		TRACE
	/********** process cmd to  build response ******************************/
		TRACE
		//hexprint(path, strlen(path)+2);

		//int contlength=0;
		int cmd = command_from_string(path);

		char username[maxcookiesize];
		// init http response fields to be added
		char cmdresponsefields[bufsize];
		memset (cmdresponsefields,'\0',bufsize);
		//initialize body of response
		char* body;
		body = (char*)malloc(bufsize);
		memset(body,'\0',bufsize);
		//initialize cartbody that can hold items
		char* cartbody;
		cartbody = (char*)malloc(MAXITEMS*MAXITEMLEN);
		memset (cartbody,'\0',MAXITEMS*MAXITEMLEN);

		//initialize usernamebody that will be set by login or cookie
		// in requests
		char* usernamevalue=NULL;
		char* usernamebody;
		char user[bufsize];
		usernamebody = (char*)malloc(bufsize);
		memset(usernamebody,'\0',bufsize);
		char * cookieptr = http_parse_header_field(msgbuf, sizeofheader, "Cookie");
		if (cookieptr&&(strlen(cookieptr)>0)){
			usernamevalue = getdecodedCookieAttribute(cookieptr, "username", user);
			if (usernamevalue){
				TRACE
				strcpy (usernamebody,"Username: ");
				strcat (usernamebody,usernamevalue);
				DBGMSG("usernamebody=%s\n",usernamebody);
			}else{
				TRACE
				DBGMSG("username=%s\n",usernamevalue);
			}
		}
		TRACE
		if (respindex!=-1){
			TRACE
			DBGMSG("username=%s\n",usernamevalue);
			//already encountered error condition from method
			//create response here and skip cmd processing
			strcpy (body, http_method_str[method]);
			strcat (body, " : not implemented");
			//contlength=strlen(body);
			resp.contentlength=strlen(body);
		}else{

			switch (cmd){
			DBGMSG("username=%s\n",usernamevalue);
			case CMDLOGIN:  ;  //http://shareprogrammingtips.com/c-language-programming-tips/why-variables-can-not-be-declared-in-a-switch-statement-just-after-labels/
				;
				TRACE
				char* user = getargvalue("username", path, username);

				//DBGMSG("user = %s\n", user);
				if (user){
					if ((strlen(cmdresponsefields))!=0){
						strcat (cmdresponsefields,lineend);
					}
					strcat(cmdresponsefields, default_http_cookie_header);
					strcat(cmdresponsefields, "username=");
					strcat(cmdresponsefields, user);
					strcat(cmdresponsefields, default_http_cookie_opt);

					respindex=2;
					strcpy(usernamebody,"Username: ");
					strcat(usernamebody,user);
					//strcat(usernamebody,lineend);
					DBGMSG(" usernamebody = %s\n", usernamebody);
				}else{
					// 400 bad request
					respindex=16;
					strcpy(usernamebody,"No user name found");
				}
				break;
			case CMDLOGOUT:
				TRACE
				if (strlen(usernamevalue)>0){
					if ((strlen(cmdresponsefields))!=0){
						strcat (cmdresponsefields,lineend);
					}
					strcat(cmdresponsefields, default_http_cookie_header);
					strcat(cmdresponsefields, "username=");
					char encodedname[bufsize*3+1];
					strcat(cmdresponsefields, encode(usernamevalue,encodedname));
					strcat(cmdresponsefields, expirenow_http_cookie_opt);

					respindex=2;
					strcpy(usernamebody,"User ");
					strcat(usernamebody,usernamevalue);
					strcat(usernamebody," was logged out");

				}else{
					respindex=16;
					strcpy(usernamebody,"Not Logged in");
				}

				break;
			case CMDSERVERTIME:
				TRACE
				respindex=2;  //ok

				body = get_localtime(body,bufsize);
				//contlength=strlen(body);
				resp.contentlength+=strlen(body);
				resp.cache=NOCACHE;
				DBGMSG("content length = %d\n",resp.contentlength);
				break;
			case CMDBROWSER:
				TRACE
				respindex=2;
				char* useragent = http_parse_header_field(msgbuf, bufsize, "User-Agent");
				if (useragent){
					strcpy(body, useragent);
				}else{
					respindex=6;
				}
				TRACE
				//contlength=strlen(body);
				DBGMSG("contentlength before browser =%d\n",resp.contentlength);
				resp.contentlength+=strlen(body);
				DBGMSG("content length = %d\n",resp.contentlength);
				break;
			case CMDREDIRECT:
				TRACE
				respindex=12;
				char* location = getargvalue("url", path, username);

				//DBGMSG("user = %s\n", user);
				if (location){
					char* decodedlocation = (char*)malloc(strlen(location));
					decodedlocation = decode(location,decodedlocation);

					if ((strlen(cmdresponsefields))!=0){
						strcat (cmdresponsefields,lineend);
					}
					strcat(cmdresponsefields, "Location: ");
					strcat(cmdresponsefields, decodedlocation);
				}

				break;
			case CMDGETFILE:
				TRACE
				break;
			case CMDPUTFILE:
				TRACE
				break;
			case CMDADDCART: ;
				char an_item[MAXITEMLEN];
				TRACE
				DBGMSG("username=%s\n",usernamevalue);
				char* cartitem = getargvalue("item", path, an_item);
				DBGMSG("username=%s\n",usernamevalue);
				TRACE
				//get username
				DBGMSG("username=%s\n",usernamevalue);
//				if (usernamevalue&&(strlen(usernamevalue)>0)&&(cartitem)){
//					TRACE
//					if ((strlen(cmdresponsefields))!=0){
//						strcat (cmdresponsefields,lineend);
//					}
//					strcat(cmdresponsefields, default_http_cookie_header);
//					strcat(cmdresponsefields, "username=");
//					char encodedname[bufsize*3+1];
//					strcat(cmdresponsefields, encode(usernamevalue,encodedname));
//					strcat(cmdresponsefields, ";");
//					DBGMSG("cmdresponsefields='%s'\n",cmdresponsefields );
//				}
				TRACE
				// get the items from cookies
				//shopping cart structure for handling addcart items
				char items[MAXITEMS][MAXITEMLEN];
				memset(items,'\0',MAXITEMS*MAXITEMLEN);
				int itemcount = 0;
				char itemlabel[bufsize];
				char itemstring[bufsize];
				char* itemlabelptr = getItemLabel(itemcount,itemlabel);
				char* itemptr = cookieptr;
				TRACE
				DBGMSG("itemlabel=%s\n",itemlabelptr);
				DBGMSG("cookieptr=%s\n",cookieptr);

				while((itemptr = getdecodedCookieAttribute(cookieptr, itemlabelptr, itemstring))
						!=NULL){
					strcpy(items[itemcount],itemptr);
					DBGMSG("%d itemptr = %s\n",itemcount,itemptr	);
					itemcount++;
					itemlabelptr = getItemLabel(itemcount,itemlabel);
					DBGMSG("itemlabel=%s\n",itemlabelptr);
				}
				// we now have recovered all addcart items from cookies
				//itemcount is the next blank
				assert(itemcount<=MAXITEMS);

				if (cartitem){
					assert (strlen (cartitem)< MAXITEMLEN);
					strcpy(items[itemcount],cartitem);

//					if ((strlen(cmdresponsefields))!=0){
//						strcat (cmdresponsefields,lineend);
//					}
					itemlabelptr = getItemLabel(itemcount,itemlabel);
//					char itemlabel[bufsize] = "item\0";
//					char itemnumberstr[bufsize];
//					sprintf(itemnumberstr,"%d",itemcount);
//					strcat (itemlabel,itemnumberstr);
					//if(!usernamevalue)
						strcat(cmdresponsefields, default_http_cookie_header);
					strcat(cmdresponsefields," ");
					strcat(cmdresponsefields,itemlabelptr);
					strcat(cmdresponsefields, "=");
					strcat(cmdresponsefields, cartitem);
					strcat(cmdresponsefields, default_http_cookie_opt);

					cartbody = buildCartBody((char*)items,itemcount, cartbody);
					resp.contentlength+=strlen(cartbody);
					itemcount++;
					respindex=2;
				}else{
					// 400 bad request
					respindex=16;
					strcpy(usernamebody,"No user name found");
				}
				TRACE
				DBGMSG("cmdresponsefields='%s'\n",cmdresponsefields );

				break;
			case CMDDELCART:
				TRACE



				break;
			case CMDCHECKOUT:
				TRACE
				break;
			case CMDCLOSE:
				TRACE
				persist_connection=0;
				respindex=2;
				resp.connection=CLOSE;
				strcpy(body, "The connection will now be closed");
				//contlength=strlen(body);
				resp.contentlength+=strlen(body);
				DBGMSG("content length = %d\n",resp.contentlength);
				break;
			case -1:
				TRACE
				respindex=20;
				sprintf(body,"%s",responsestr[respindex]);
				//contlength=strlen(body);
				resp.contentlength+=strlen(body);
				DBGMSG("content length = %d\n",resp.contentlength);
				break;
			default:
				break;
			}
		}
/**************** assemble response *******************/
		TRACE
		char* response;
		if (respindex>0){
			response = (char*)malloc(bufsize);
			memset(response,'\0',bufsize);
			unsigned int   responsebuffersize=bufsize;
			response = addheader(response,respindex);
			if (resp.connection==CLOSE)
				response = addfield(response, close_http_connection,&responsebuffersize);
			else
				response = addfield(response, default_http_connection,&responsebuffersize);
			response = addfield(response, default_http_contenttype,&responsebuffersize);
			if (resp.cache==NOCACHE)
				response = addfield(response, nocache_http_cache_control,&responsebuffersize);
			else
				response = addfield(response, default_http_cache_control,&responsebuffersize);
			char timestr[bufsize];
			char* timefield = get_default_http_date(timestr,bufsize);
			response = addfield(response, timefield,&responsebuffersize);

			if (strlen(cmdresponsefields)>0){
				TRACE
				response = addfield(response, cmdresponsefields,&responsebuffersize);
			}

			// adjust content length: prepend all output with username info if any
			if (strlen(usernamebody)!=0){
					resp.contentlength+=strlen(usernamebody) + strlen(lineend) ;
					//contlength += strlen() + strlen(lineend) ;
			}

			if (resp.contentlength!=0){
				TRACE
				char contlenstr[bufsize];
				response = addfield(response, get_http_content_length(resp.contentlength,contlenstr),&responsebuffersize);
			}

			response = addfield(response, "" ,&responsebuffersize);

			// attach the body now
			// prepend username before response
			if (strlen(usernamebody)!=0){
					response = addfield(response, usernamebody, &responsebuffersize);
			}
			DBGMSG("response=$'%s'\n",response);
			// response body
			if ((resp.contentlength!=0)&&(strlen(body)>0)){
				response = addfield(response, body,&responsebuffersize);
				TRACE
				//hexprint(response,strlen(response));
			}
			DBGMSG("response=$'%s'\n",response);
			//if a cart exists show itemslist
			if (strlen(cartbody)!=0){
				DBGMSG("cartbody$:'%s'",cartbody);
				response = addfield(response, cartbody,&responsebuffersize);
			}

		}else{
			//todo we always need some kind of response?
			TRACE
			//we should never get here since all cases should be handled
			//above already
		}

		DBGMSG("RESPONSE: $%s", response);

	/*********  send response *******************************/
		TRACE
		int sent=0;
		int bytesout = send(socket,response,strlen(response),flag);
		if (bytesout==0){
			fprintf(stderr, "remote closed connection, child closing\n");
			persist_connection=0;
			return;
		}else if (bytesout<0){
			perror("send error:");
		}else {// if (bytesout>0){
			while(bytesout<(int)strlen(response)){
				sent+=bytesout;
				bytesout = send(socket,response+sent,strlen(response+sent),flag);
				if (bytesout==0){
							fprintf(stderr, "remote closed connection, child closing\n");
							persist_connection=0;
							break;
				}
				if (bytesout<0)
					perror("recv error:");
			}
		}

		if (!persist_connection){
			TRACE
			release_connection_resources(msgbuf);
			release_connection_resources(response);
			release_connection_resources(body);
			release_connection_resources(usernamebody);
			int rc = close(socket);
			if (rc<0)
				perror("socket error while closing");
			break;
		}
		TRACE
	} while( bytesin >0);




}
