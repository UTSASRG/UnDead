#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <stdlib.h>

#define LOCKAMOUNT 5
#define THREADAMOUNT 4

pthread_mutex_t l[LOCKAMOUNT];

pthread_mutex_t gl = PTHREAD_MUTEX_INITIALIZER;

void *threadProcSe(void* arg)
{
	pthread_mutex_lock(&l[3]); 
	pthread_mutex_lock(&l[2]); // deadlock_3
	pthread_mutex_unlock(&l[2]);
	pthread_mutex_unlock(&l[3]);

	pthread_mutex_lock(&l[3]); 
	pthread_mutex_lock(&l[1]); // deadlock_1
	pthread_mutex_unlock(&l[1]);
	pthread_mutex_unlock(&l[3]);

	pthread_mutex_lock(&l[2]); 
	pthread_mutex_lock(&l[4]);
	pthread_mutex_lock(&l[3]); // deadlock_2, deadlock_3
	pthread_mutex_unlock(&l[3]);
	pthread_mutex_unlock(&l[4]);
	pthread_mutex_unlock(&l[2]);

	return NULL;
}

void *threadProcRe(void* arg)
{
	pthread_mutex_lock(&l[1]);
	pthread_mutex_lock(&l[3]); // deadlock_1, deadlock_2
	pthread_mutex_lock(&l[2]); // deadlock_2
	pthread_mutex_unlock(&l[2]);
	pthread_mutex_unlock(&l[3]);
	pthread_mutex_unlock(&l[1]);
	return NULL;
}

int main()
{
	int ret;

	pthread_t thread[THREADAMOUNT];

	pthread_mutex_init(&l[0], NULL);
	pthread_mutex_init(&l[1], NULL);
	pthread_mutex_init(&l[2], NULL);
	pthread_mutex_init(&l[3], NULL);
	pthread_mutex_init(&l[4], NULL);

	for(int i = 0; i < THREADAMOUNT; i++) {
		if(i % 2 == 0)
			pthread_create(&thread[i], NULL, threadProcSe, NULL);
		else
			pthread_create(&thread[i], NULL, threadProcRe, NULL);
	}
	for(int i = 0; i < THREADAMOUNT; i++) {
		ret = pthread_join(thread[i],NULL);
	}

	return ret;
}
