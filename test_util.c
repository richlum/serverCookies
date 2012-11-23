#include <stdio.h>
#include <string.h>

#include "util.h"
#define bufsize 64

int main(void) {
    
    char req[] = "Post http://www.example.com/test?name=value&name2=value2 HTTP/1.1\r\n"
        "Content-Length: 12345\r\n"
        "Host:   www.example.com  \r\n"
        "Pragma: no-cache\r\n"
        "Accept-Encoding: text/plain\r\n"
        "\r\nTHIS_IS_THE_BODY";
    int len = strlen(req);
    
    char argname[bufsize];
    char argvalue[bufsize];
    char* nextarg;
    nextarg = uri_argnamevalue(req,argname,sizeof(argname),argvalue,sizeof(argvalue));
    printf("arg: %s %s\n", argname, argvalue);
    while (nextarg!=NULL){
    	nextarg = uri_argnamevalue(nextarg,argname,sizeof(argname),argvalue,sizeof(argvalue));
    	printf("arg: %s %s\n", argname, argvalue);
    }

    printf("Body: %s\n", http_parse_body(req, len));
    printf("Method: %d (%s)\n", http_parse_method(req), http_method_str[http_parse_method(req)]);
    printf("URI: '%s' (path is '%s')\n", http_parse_uri(req), http_parse_path(http_parse_uri(req)));
    printf("Pragma: '%s'\n", http_parse_header_field(req, len, "Pragma"));
    printf("Content-length: '%s'\n", http_parse_header_field(req, len, "Content-length"));
    printf("Accept-Encoding: '%s'\n", http_parse_header_field(req, len, "Accept-Encoding"));
    printf("Host: '%s'\n", http_parse_header_field(req, len, "Host"));

    
    return 0;
}
