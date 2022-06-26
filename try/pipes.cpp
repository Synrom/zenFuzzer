#include <cstdio>
#include <cstring>
#include <unistd.h>
int main(){

	int pipes[2];
	if(pipe(pipes) < 0){
		perror("failed while creating a pipe");
		return 1;
	}

	int readfd = pipes[0];
	int writefd = pipes[1];

	printf("test read:\n");
	char buf[20],buf2[20];
	memset(buf2, 0, 20);
	strcpy(buf,"hallo test");

	write(readfd, buf, strlen("hallo test") + 1);
	read(readfd,buf2,strlen("hallo test") + 1);

	printf("%s = %s\n",buf, buf2);

	printf("test read:\n");

	memset(buf2, 0, 20);
	strcpy(buf,"hallo test");
	write(writefd, buf, strlen("hallo test") + 1);
	read(writefd,buf2,strlen("hallo test") + 1);

	printf("%s = %s\n",buf, buf2);

	return 0;
}
