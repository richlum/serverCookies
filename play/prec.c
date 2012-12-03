#include <stdio.h>

int main(int argc, char** argv){

int a = 7;
int b=  66;

int *c=&a;

printf("%d %d\n", *c, *c++);
printf("%d %d\n", *c, *c);

}
