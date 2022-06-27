
#include <cstdio>
#include <mutex>
#include <pthread.h>
#include <unistd.h>

std::mutex mtx;

void *handler1(void *){

	mtx.lock();
	sleep(2);
	mtx.unlock();

	printf("unlocked\n");
	fflush(stdout);

	return NULL;
}

void *handler2(void *){

	sleep(1);
	printf("egal\n");
	fflush(stdout);

	return NULL;
}

int main(){
	pthread_t thread_ids[2];
	pthread_create(&thread_ids[0], NULL, handler1, NULL);
	pthread_create(&thread_ids[1], NULL, handler2, NULL);
	pthread_join(thread_ids[0],NULL);
	pthread_join(thread_ids[1],NULL);
	return 0;
}

