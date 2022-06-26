#include <cstdio>
#include <cstdlib>
#include <netinet/in.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

void *connecter(void *data){

	return NULL;
}

int main(){
	
	int sockets[2] = {0,0};
	if(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) != 0){
		perror("failed to create socket pair");
		return 1;
	}

	int fuzzfd = sockets[0];
	int appfd = sockets[1];

	char buf1[20],buf2[20];

	memset(buf1,0,20);
	strcpy(buf2,"hallo test");

	send(fuzzfd,buf2,strlen("hallo test") + 1,0);
	recv(appfd,buf1,strlen("hallo test") + 1,0);

	printf("%s = %s\n",buf1,buf2);

	memset(buf2,0,20);
	strcpy(buf1,"hallo test2");

	send(appfd,buf1,strlen("hallo test2") + 1,0);
	recv(fuzzfd,buf2,strlen("hallo test2") + 1,0);

	printf("%s = %s\n",buf1,buf2);


	int sockets2[2] = {0,0};
	if(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets2) != 0){
		perror("failed to create socket pair");
		return 1;
	}

	int fuzzfd2 = sockets2[0];
	int appfd2 = sockets2[1];

	memset(buf1,0,20);
	strcpy(buf2,"hallo test");

	send(fuzzfd2,buf2,strlen("hallo test") + 1,0);
	recv(appfd2,buf1,strlen("hallo test") + 1,0);

	printf("%s = %s\n",buf1,buf2);

	memset(buf2,0,20);
	strcpy(buf1,"hallo test2");

	send(appfd2,buf1,strlen("hallo test2") + 1,0);
	recv(fuzzfd2,buf2,strlen("hallo test2") + 1,0);

	printf("%s = %s\n",buf1,buf2);

	close(fuzzfd2);
	close(appfd2);
	close(fuzzfd);
	close(appfd);

	return 0;
}
