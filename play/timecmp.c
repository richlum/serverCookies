
#include <stdio.h>
#include <time.h>
time_t to_seconds(const char *date)
{
        struct tm storage={0,0,0,0,0,0,0,0,0};
        char *p=NULL;
        time_t retval=0;

        p=(char *)strptime(date,"%d-%b-%Y",&storage);
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

int main()
{
   char *date1="20-JUN-2006";
   char *date2="21-JUN-2006";
   time_t d1=to_seconds(date1);
   time_t d2=to_seconds(date2);
   
   printf("date comparison: %s %s ",date1,date2);
   if(d1==d2) printf("equal\n");
   if(d2>d1)  printf("second date is later\n");
   if(d2<d1)  printf("seocnd date is earlier\n");
   return 0;
}
