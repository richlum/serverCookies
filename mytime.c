
#define _XOPEN_SOURCE
#include "mytime.h"
#include <string.h>

#include <time.h>
time_t to_seconds( char *date,  const char* timeformat)
{
        struct tm storage;//={0,0,0,0,0,0,0,0,0,0};
        memset (&storage,'\0',sizeof(struct tm));

        char *p=NULL;
        time_t retval=0;

        p=(char *)strptime(date,timeformat,&storage);
        if(p==NULL)
        {
                retval=0;
        }
        else
        {
                retval=mktime(&storage);
        }
        return retval;
}
