/*
 * File: service.c
 */


#include <string.h>

#include <unistd.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>

#include "service.h"
#include "util.h"
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

// #define _XOPEN_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include "mytime.h"

#include <time.h>

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
const char* appl_octet_http_contenttype = "Content-Type: application/octet-stream";
const char* default_http_cache_control = "Cache-Control: public";
const char* nocache_http_cache_control = "Cache-Control: no-cache";
const char* lineend = "\r\n";
const char* default_http_cookie_opt = "; path=/; Max-Age=86400;";
const char* expirenow_http_cookie_opt = "; path=/; Max-Age=0;";
const char* default_http_cookie_header = "Set-Cookie: ";
const char* default_http_charset = "Accept-Charset: IOS-8859-1,utf-8;q=0.7,*;q=0.3;";
const char* default_http_lang = "Accept-Languate: en-US,en;q=0.8";
const char* default_http_encoding = "Accept-Encoding:gzip,deflate,sdch";
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
	int				dontmodifybody;
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

	if (path==NULL)
		return NULL;
	char* p = strchr(path, '?');
	if (p==NULL)
		return NULL;
	p++;
	DBGMSG("p=%s\n", p);
	DBGMSG("argname=%s\n",argname);
	int match;
	while((p)&&((match=(!strncasecmp(p,argname,strlen(argname))))==0)){
		p=strchr(p,'&');
		if (p)
			p++;
		TRACE
//		DBGMSG("p=%s\n", p);
	}
	TRACE
	if(p==NULL){
		TRACE
		return NULL;
	}
	TRACE
	if ((match)&&(p!=NULL)){

		int i=0;
		p=strchr(p,'=');
		p++;
		TRACE
		if (p==NULL)
			return NULL;
		//DBGMSG("p=%s\n", p);
		while((p!=NULL)&&(*p!=' ')&&(*p!='&')&&(*p!='\r')&&(*p!='\0')){
			value[i++]=*p;
			p++;
		}
		if (p==NULL)
			return NULL;
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
	TRACE
	return NULL;
}


char* getdecodedCookieAttribute(char* cookieptr, char* attribName, char* valuestore){
	TRACE
	DBGMSG("searching for (%d) attribute='%s'\n", strlen(attribName),attribName);
	char* p = cookieptr;
	char* result;
	// expecting ptr to first attribute name or blanks preceding it.
	//TRACE
	if (p==NULL){
		TRACE
		return NULL;
	}
	//DBGMSG("p=%s\n",p);
	while (*p==' '){
		p++;
	}
	//TRACE
	//DBGMSG("p=%s\n",p);
	//DBGMSG("*p=%x %c\n",*p, *p);
	//DBGMSG("attribName (%d) = %s\n", strlen(attribName), attribName);
//	int rc = strncasecmp(p,attribName,strlen(attribName));
//	DBGMSG("strncasecmp result = %d\n",rc );
	while((*p!='\0')&&(*p!='\r')&&(*p!='\n')
			&&(strncasecmp(p,attribName,strlen(attribName))!=0)){
		p=strchr(p,';');
		//TRACE
		//DBGMSG("p=%s\n",p);
		if (!p){
			TRACE
			return NULL;
		}
		p++;
		//DBGMSG("p=%s\n",p);
		while((*p==' '))
			p++;
	}
	//have ptr to attribute name
	//TRACE
	//DBGMSG("p=%s\n",p);
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
	TRACE
	for (i=0; i<=itemcount;i++){
		char* p = &items[i*MAXITEMLEN];
		DBGMSG("item=%s\n",p);
		sprintf(cstr,"%d",i);
		strcat(cartbody,cstr);
		strcat(cartbody, ". ");
		strcat(cartbody,p);
		strcat(cartbody,lineend);
		//DBGMSG("%d  %s\n",i,cartbody);
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
		resp = (resp_setting){ 0,0,0,0,0};

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
		TRACE
		int msgsize = bytesin;
		fprintf(stderr, "$%2d:%s\n",bytesin, msgbuf);
		int sizeleft=0;

	/***** recv header until complete ***********/
		while((!message_has_newlines(msgbuf))&&(bytesin>0)){
			TRACE
			sizeleft = *msgbufsize - msgsize-1;
			fprintf(stderr, "sizeleft = %d\n", sizeleft);
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

		char* endhdrs = strstr(msgbuf,"\r\n\r\n");
		DBGMSG("strlen endhdrs = %d\n", strlen(endhdrs));
		TRACE
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
		//hexprint(path, strlen(path)+2);

		//int contlength=0;
		int cmd = command_from_string(path);

		char username[maxcookiesize];
		// init http response fields to be added
		char cmdresponsefields[maxcookiesize];
		memset (cmdresponsefields,'\0',bufsize);
		//initialize body of response
		char* body;
		body = (char*)malloc(bufsize);
		memset(body,'\0',bufsize);
		//initialize cartbody that can hold items
		char* cartbody;
		cartbody = (char*)malloc(MAXITEMS*MAXITEMLEN);
		memset (cartbody,'\0',MAXITEMS*MAXITEMLEN);
		//array of cart items
		char items[MAXITEMS][MAXITEMLEN];
		int itemcount;
		char itemlabel[bufsize];
		char itemstring[bufsize];
		char* itemlabelptr;
		char* itemptr;

		//initialize usernamebody that will be set by login or cookie
		// in requests
		char* usernamevalue=NULL;
		char* usernamebody;
		char user[bufsize];
		usernamebody = (char*)malloc(bufsize);
		memset(usernamebody,'\0',bufsize);
		TRACE
		char * cookieptr = http_parse_header_field(msgbuf, sizeofheader, "Cookie");
		if (cookieptr&&(strlen(cookieptr)>0)){
			TRACE
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
			resp.contentlength=strlen(body);
		}else{

			switch (cmd){
			DBGMSG("username=%s\n",usernamevalue);
			case CMDLOGIN:  ;  //http://shareprogrammingtips.com/c-language-programming-tips/why-variables-can-not-be-declared-in-a-switch-statement-just-after-labels/
				;
				TRACE
				if (path==NULL){
					TRACE
					respindex=19;
					strcpy (body, "No Path provided");
					resp.contentlength=strlen(body);
					break;
				}
				char* user = getargvalue("username", path, username);
				TRACE
				if ((user)&&(strlen(user)>0)){
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
				}else{
					respindex=19;
					strcpy(body,"Not Logged in");
					resp.contentlength+=strlen(body);
				}
				break;
			case CMDLOGOUT:
				TRACE
				if ((usernamevalue)&&(strlen(usernamevalue)>0)){
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
					TRACE
					respindex=19;
					strcpy(body,"Not Logged in");
					resp.contentlength+=strlen(body);
				}
				TRACE
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
				resp.contentlength+=strlen(body);
				break;
			case CMDREDIRECT:
				TRACE
				respindex=12;
				char* location = getargvalue("url", path, username);
				if (location==NULL){
					TRACE
					respindex=19;
					strcpy(body,"No url found");
					resp.contentlength+=strlen(body);
					break;
				}

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
			case CMDGETFILE:  ;
				TRACE
				char filename[bufsize];
				char* pfn = getargvalue("filename", path, filename);
				DBGMSG("pfn = %s\n",pfn);
				if (pfn==NULL){
					TRACE
					respindex=19;
					strcpy(body,"No filneame given");
					resp.contentlength+=strlen(body);
					break;
				}
				char lastmodifileddate[bufsize];
				char* lstmodfldate = getargvalue("If-Modified-Since",path, lastmodifileddate);


				struct stat filestat;
				//int file = open(pfn, O_RDONLY);
				if (stat (pfn,&filestat)<0)
					perror("failed to stat file");
				resp.contentlength = filestat.st_size;

				DBGMSG ("filesize = %d\n", resp.contentlength);
				time_t modtime = filestat.st_mtime;
				struct tm* mtm = gmtime(&modtime);
				char filetime[bufsize];
				char* pftime = filetime;
				strftime(filetime, bufsize, timeformatstr,mtm);
				DBGMSG("file mod time : %s\n",filetime);
				int downloadfile=1;
				//time_t btime;
				if (lstmodfldate!=NULL){
					DBGMSG("lstmodfldate = %s\n",lstmodfldate);

					struct tm storage;// ={0,0,0,0,0,0,0,0,0};
					memset (&storage,'\0', sizeof(storage));
					//char *p=NULL;
					//p=(char*)strptime(lstmodfldate,timeformatstr,&storage);
					time_t ftime = to_seconds(pftime, timeformatstr);
					time_t itime = to_seconds(lstmodfldate, timeformatstr);

					if (itime<ftime){
					//if (mktime(&storage)<mktime(mtm)){
						downloadfile=1;
						TRACE
						//file has been modified since browsers informed time
					}else{
						TRACE
						downloadfile=0;
						respindex = 13;
						strcpy(body,"Not Modified");
						resp.contentlength=sizeof(body);
					}
				}
				if (downloadfile==1){
					if (bufsize < resp.contentlength){
						free(body);
						body = (char*)malloc (resp.contentlength + 100);
					}
//					strcat(body, "filename=download.txt&content=");
					char* bptr =body;// + strlen("filename=download.txt&content=");
					FILE* fp;
					fp = fopen(pfn,"r");
					size_t bytes = fread(bptr, 1, resp.contentlength, fp);
					//int bytes = read(file, body, resp.contentlength );
					fprintf(stderr,"bytes read = %d\n", bytes);

					//resp.contentlength=strlen(body);
					resp.contentlength=bytes;
					DBGMSG("bodysize = %d\n",resp.contentlength);
					int fres = fclose(fp);

					fprintf(stderr, "close rc=%d\n", fres);
				}

				TRACE
				if ((strlen(cmdresponsefields))!=0){
					strcat (cmdresponsefields,lineend);
				}
				strcat(cmdresponsefields, "Content-Location: ");
				strcat(cmdresponsefields, pfn);
				strcat(cmdresponsefields, lineend);
				strcat(cmdresponsefields, "Last-Modified: ");
				strcat(cmdresponsefields, filetime);
				//strcat(cmdresponsefields, lineend);

				resp.content=APPL_OCTET;
				respindex=2;
				resp.dontmodifybody=1;
				//strcpy(body,"file transfer: ");
				//strcpy(body, pfn);
				//resp.contentlength+=strlen(body);
				TRACE
				DBGMSG("size of body = %d\n",strlen(body));
				DBGMSG("resp.contentlength = %d\n",resp.contentlength);
				break;
			case CMDPUTFILE: ;
				TRACE
				char * lengthstr = http_parse_header_field(msgbuf, *msgbufsize, "Content-Length");
				int length = atoi(lengthstr);
				DBGMSG("contentlength=%d\n",length);
				DBGMSG("msgbuf='%s'\n",msgbuf	);
				//TRACE
				//hexprint(endhdrs,80);
				//const char *postbody = strstr(msgbuf, "\r\n\r\n");
				const char* postbody = endhdrs+4;
				//DBGMSG("postbody=%s\n",postbody)

				if (strncasecmp(postbody,"filename",strlen("filename"))!=0){
					respindex = 25;
					strcpy(body,"Missing required filename argument");
					resp.contentlength=strlen(body);
					break;
				}
				TRACE
				char* bptr = strchr(postbody,'=');
				bptr++;
				hexprint(bptr,40);
				char uploadfn[bufsize];
				memset (uploadfn,'\0',bufsize);
				int i=0;
				while((*bptr!='&')&&(*bptr!='\r')&&
						(*bptr!='\n')&&(*bptr!='\0')){
					uploadfn[i]=*bptr;
					bptr++;
					i++;
				}
				uploadfn[i]='\0';
				DBGMSG("filename=%s\n",uploadfn);
				TRACE

				while(*bptr=='&'){
					bptr++;
				}
				TRACE
				if (strncasecmp(bptr,"content=",strlen("content="))!=0){
					respindex = 25;
					strcpy(body,"Missing required content argument");
					resp.contentlength=strlen(body);
					break;
				}
				bptr = strchr(bptr,'=');
				bptr++;
				TRACE
				//now pointing at first byte of content, of size 'length'
				char *decode(const char *original, char *decoded) ;
				char* decodedcontent = (char*) malloc(length+20);
				memset(decodedcontent,'\0',length+20);
				decodedcontent = decode(bptr, decodedcontent);
				int newsize = strlen(decodedcontent);

//				if (access(uploadfn, W_OK)!=0){
//					respindex = 19;
//					strcpy(body,"Insufficient access privilige");
//					resp.contentlength=strlen(body);
//					break;
//				}

				TRACE
				FILE* fp = fopen(uploadfn,"a");
				i=0;
				while((i<newsize)&&(fputc(*decodedcontent, fp))){
					decodedcontent++;
					i++;
				}
				fclose(fp);
				strcat (body,"file saved: ");
				strcat (body, uploadfn);
				resp.contentlength=strlen(body);
				respindex=2;

				TRACE
				break;
			case CMDADDCART: ;
				TRACE
				char an_item[MAXITEMLEN];
				char* cartitem = getargvalue("item", path, an_item);
				// get the items from cookies
				//shopping cart structure for handling addcart items

				memset(items,'\0',MAXITEMS*MAXITEMLEN);
				itemcount = 0;
				memset(itemstring,'\0',bufsize);
				itemlabelptr = getItemLabel(itemcount,itemlabel);
				itemptr = cookieptr;
				TRACE

				while((itemptr = getdecodedCookieAttribute(cookieptr, itemlabelptr, itemstring))
						!=NULL){
					strcpy(items[itemcount],itemptr);
					itemcount++;
					itemlabelptr = getItemLabel(itemcount,itemlabel);
				}
				// we now have recovered all addcart items from cookies
				//itemcount is the next blank
				assert(itemcount<=MAXITEMS);

				if (cartitem){
					assert (strlen (cartitem)< MAXITEMLEN);
					strcpy(items[itemcount],cartitem);

					itemlabelptr = getItemLabel(itemcount,itemlabel);
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
					respindex=19;
					strcpy(usernamebody,"No user name found");
				}
				TRACE
				DBGMSG("cmdresponsefields='%s'\n",cmdresponsefields );

				break;
			case CMDDELCART:  ;
				TRACE
				char del_item[MAXITEMLEN];
				char* delete_item = getargvalue("itemnr", path, del_item);
				TRACE
				DBGMSG("del item = %s\n",delete_item);
				if (delete_item==NULL){
					TRACE
					respindex=19;
					strcpy(usernamebody,"No itemnr found");
					break;
				}
				//delete_item+=strlen("item");
				int itemnumber = atoi(delete_item);
				if ((itemnumber<0)||(itemnumber>MAXITEMS)){
					fprintf(stderr, "itemnumber (%d) out of range",itemnumber);
				}
				DBGMSG("itemnumber to delete=%d\n",itemnumber);
				// get the items from cookies
				//shopping cart structure for handling addcart items

				memset(items,'\0',MAXITEMS*MAXITEMLEN);
				itemcount = 0;
				memset(itemstring,'\0',bufsize);
				itemlabelptr = getItemLabel(itemcount,itemlabel);
				itemptr = cookieptr;
				TRACE

				while(itemptr!=NULL){
					itemptr = getdecodedCookieAttribute(cookieptr, itemlabelptr, itemstring);
					TRACE
					if (itemptr!=NULL){
						DBGMSG("%d  itemptr=%s\n",itemcount,itemptr);
						strcpy(items[itemcount*MAXITEMLEN],itemptr);
						TRACE
						itemcount++;
						itemlabelptr = getItemLabel(itemcount,itemlabel);
					}else{
						break;
					}

				}
				TRACE
				// we now have recovered all addcart items from cookies
				//int i;
				for (i=0;i<=itemcount-1;i++){
					DBGMSG("i=%d\n",i);
					DBGMSG("ITEM='%s'\n",items[i*MAXITEMLEN]);
					if (i<itemnumber){
						//do nothing
						TRACE
					}else if (i==itemcount-1){
						//delete this one
						TRACE
						itemlabelptr = getItemLabel(i,itemlabel);
						strcat(cmdresponsefields, default_http_cookie_header);
						strcat(cmdresponsefields," ");
						strcat(cmdresponsefields,itemlabelptr);
						strcat(cmdresponsefields, "=");
						strcat(cmdresponsefields, items[i*MAXITEMLEN]);
						strcat(cmdresponsefields, expirenow_http_cookie_opt);
						//items[itemcount]=NULL;
						DBGMSG("final del (%d) cmdresponsefields='%s'\n",i,cmdresponsefields);
					}else{
						TRACE
						//all these elements have to shift down one
						itemlabelptr = getItemLabel(i,itemlabel);
						strcat(cmdresponsefields, default_http_cookie_header);
						//strcat(cmdresponsefields," ");
						strcat(cmdresponsefields,itemlabelptr);
						strcat(cmdresponsefields, "=");
						strcat(cmdresponsefields, items[(i+1)*MAXITEMLEN]);
						strcat(cmdresponsefields, default_http_cookie_opt);
						strcat(cmdresponsefields, lineend);
						strncpy(items[i*MAXITEMLEN],items[(i+1)*MAXITEMLEN],MAXITEMLEN);
					}
				}
				TRACE
				memset (cartbody,'\0',MAXITEMS*MAXITEMLEN);
				char tempstr[bufsize];
				memset(tempstr,'\0',bufsize);
				for (i=0;i<itemcount-1;i++){
					fprintf(stderr, "%s\n",items[i*MAXITEMLEN]);
					sprintf(tempstr,"%d. ",i);
					strcat(cartbody, tempstr);
					strcat(cartbody, items[i*MAXITEMLEN]);
					strcat(cartbody, lineend);
				}
				if (itemcount<=1)
					strcpy(cartbody," EMPTY CART");
				TRACE
				DBGMSG("delcart cmdresponse = '%s'\n",cmdresponsefields);
				DBGMSG("cartbody = '%s'\n",cartbody);
				resp.contentlength+=strlen(cartbody);
				itemcount--;
				respindex=2;


				break;
			case CMDCHECKOUT:
				TRACE
				if ((usernamevalue)&&(strlen(usernamevalue)>0)){
					if ((strlen(cmdresponsefields))!=0){
						strcat (cmdresponsefields,lineend);
					}


					memset(items,'\0',MAXITEMS*MAXITEMLEN);
					itemcount = 0;
					memset(itemstring,'\0',bufsize);
					itemlabelptr = getItemLabel(itemcount,itemlabel);
					itemptr = cookieptr;
					TRACE

					while(itemptr!=NULL){
						itemptr = getdecodedCookieAttribute(cookieptr, itemlabelptr, itemstring);
						TRACE
						if (itemptr!=NULL){
							DBGMSG("%d  itemptr=%s\n",itemcount,itemptr);
							strcpy(items[itemcount*MAXITEMLEN],itemptr);
							TRACE
							itemcount++;
							itemlabelptr = getItemLabel(itemcount,itemlabel);
						}else{
							TRACE
							break;
						}

					}
					TRACE
					// items contains array of item strings.
					FILE* fp = fopen("CHECKOUT.txt","a");
					if (fp==NULL){
						perror("failed to open file");
					}
					//username
					fprintf(fp, "%s\r\n", usernamebody);
					char tempstr[maxcookiesize];
					for(i=0;i<itemcount;i++){
						sprintf(tempstr,"%d. ",i);
						strcat(tempstr, items[i*MAXITEMLEN] );
						strcat(tempstr, lineend);
						fprintf(fp, "%s", tempstr);

					}
					int res = fclose(fp);
					if (res!=0){
						perror("failed to clos file");
					}
					TRACE
					for (i=0;i<itemcount;i++){
						itemlabelptr = getItemLabel(i,itemlabel);
						strcat(cmdresponsefields, default_http_cookie_header);
						strcat(cmdresponsefields,itemlabelptr);
						strcat(cmdresponsefields, "=");
						strcat(cmdresponsefields, items[i*MAXITEMLEN]);
						strcat(cmdresponsefields, expirenow_http_cookie_opt);
						if (i<itemcount-1)
							strcat(cmdresponsefields, lineend);
					}
					DBGMSG("cmdresponsefiles='%s'\n",cmdresponsefields);
					strcat(body, "Thank YOU for shopping at WalMart. Please come again");
					resp.contentlength+=strlen(body);
					respindex=2;
				}else{
					TRACE
					respindex=19;
					strcpy(body,"User must be logged in to checkout");
					resp.contentlength+=strlen(body);
				}

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
				sprintf(body,"%s","Command not found");
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
		DBGMSG("resp.contentlength = %d\n",resp.contentlength);
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

			DBGMSG("resp.contentlength = %d\n",resp.contentlength);
			if (resp.content==APPL_OCTET)
				response = addfield(response, appl_octet_http_contenttype,&responsebuffersize);
			else
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
			DBGMSG("resp.contentlength = %d\n",resp.contentlength);
			// adjust content length: prepend all output with username info if any
			if ((strlen(usernamebody)!=0)&&(resp.dontmodifybody!=1)){
					resp.contentlength+=strlen(usernamebody) + strlen(lineend) ;
					//contlength += strlen() + strlen(lineend) ;
			}
			DBGMSG("resp.contentlength = %d\n",resp.contentlength);
			if (resp.contentlength!=0){
				TRACE
				char contlenstr[bufsize];
				response = addfield(response, get_http_content_length(resp.contentlength,contlenstr),&responsebuffersize);
			}

			response = addfield(response, "" ,&responsebuffersize);

			// attach the body now
			// prepend username before response
			if ((strlen(usernamebody)!=0)&&(resp.dontmodifybody!=1)){
					response = addfield(response, usernamebody, &responsebuffersize);
			}
			// response body
			if ((resp.contentlength!=0)&&(strlen(body)>0)){
				response = addfield(response, body,&responsebuffersize);
				TRACE
				DBGMSG("body size = %d\n", strlen(body));
				//hexprint(response,strlen(response));
			}
			//if a cart exists show itemslist
			if ((strlen(cartbody)!=0)&&(resp.dontmodifybody!=1)){
				response = addfield(response, cartbody,&responsebuffersize);
			}

		}else{
			//todo we always need some kind of response?
			TRACE
			//we should never get here since all cases should be handled
			//above already
		}

		fprintf(stderr,"RESPONSE: $%s", response);

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
	fprintf(stderr,"child exiting\n");

}
