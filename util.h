/*
 * File: util.h
 */

#ifndef _UTIL_H_
#define _UTIL_H_

typedef enum {
    METHOD_GET, METHOD_POST, METHOD_HEAD, METHOD_OPTIONS, METHOD_PUT,
    METHOD_DELETE, METHOD_TRACE, METHOD_CONNECT, METHOD_UNKNOWN
} http_method;

// all http messages should terminate with crlf pair (empty line)
#define MESSAGE_TERMINATOR "\r\n\r\n"
#define HTTPV10STRING "HTTP/1.0"
#define MAXHDRSEARCHBYTES 2048

extern const char* header_connect;
extern const char *http_method_str[];

//#define TRACE printf( "%s %d\n", __FILE__, __LINE__);

#ifdef DEBUG
	#define TRACE printf("Entered:%s:  AT %s:%d\n",__FUNCTION__, __FILE__, __LINE__);
	#define DBGMSG( x, ...) printf(x, __VA_ARGS__);
#else
	#define TRACE
	#define DBGMSG(x, ...)
#endif




int http_header_complete(const char *request, int length);
http_method http_parse_method(const char *request);
char *http_parse_uri(char *request);
const char *http_parse_path(const char *uri);
char *http_parse_header_field(char *request, int length, const char *header_field);
const char *http_parse_body(const char *request, int length);
char *encode(const char *original, char *encoded);
char *decode(const char *original, char *decoded);
int  uri_has_args(char* request);
char* uri_argnamevalue(char* request,char* name, int namelen, char*value, int valuelen	);
int message_has_newlines(char* buf);
int is_httpVer_1_0(char* buf);
char* doubleBufferSize(char* buffer, unsigned int* msgsize);
char* increaseBufferSizeBy(char* buffer, unsigned int* bufsize, unsigned int increase);

#endif
