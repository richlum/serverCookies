#include <string.h>
#include <stdio.h>

int main(int argc, char** argv){
	char* str1 = "this is string1 of strings";
	char* str2 = "string";

	printf("%d\n", strncmp(str1,str2,strlen(str1)));
	printf("%d\n", strncmp(str2,str1,strlen(str1)));


	printf("%s\n", strstr(str1,str2));

}
