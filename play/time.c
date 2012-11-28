#include <stdio.h>
#include <time.h>

#define MAXTIMESZ 64

int main ( int argc, char** argv)
{


char timestring[MAXTIMESZ];
time_t timer = time(&timer);
struct tm* curtime = gmtime(&timer);
char* fmt = "%Y%m%d %a %H:%M:%S %z";


int size = strftime(timestring, sizeof(timestring), fmt, curtime);
printf("gmt time = %s\n", timestring);

struct tm* ltime = localtime(&timer);

size = strftime(timestring, sizeof(timestring), fmt, ltime);
printf("local time = %s\n", timestring);
}

