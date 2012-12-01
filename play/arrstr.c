#include <string.h>
#include <stdio.h>

int main(int arg, char** argv)
{
char myarray[30];

memset (myarray, '\0', 10);
strcpy (myarray,"testing");
int size = strlen(myarray);
printf("\n%d\n%s\n", size,myarray);

int i;
for (i=0;i<strlen(myarray); i++){
	printf ("\t%c\n", myarray[i]);

}

}
