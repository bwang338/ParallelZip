#include <pthread.h>
#include <sys/time.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/sysinfo.h>
#include <fcntl.h>

// constants
int queueSize = 32;

//struct definitions
typedef struct {
	char *start;
	size_t index;
	size_t size;
} QUEUE;

typedef struct {
	char *final;
	size_t size;
} ENTIREPIECE;

//global variables
int pgsize;						
QUEUE *queue;				
static int fillptr = 0;
static int useptr = 0;
static int numfull = 0;
ENTIREPIECE *finished;			
static int chunks;				
volatile int done;			
//lock stuff
pthread_mutex_t lock  = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t fill = PTHREAD_COND_INITIALIZER;
pthread_cond_t empty = PTHREAD_COND_INITIALIZER;


//method declarations
void *consumer(void *ptr);
void do_fill(QUEUE chunk);
QUEUE do_get();
void zip(QUEUE chunk, ENTIREPIECE *tmp);

int main(int argc, char **argv)
{	
	if (argc <= 1){
		printf("pzip: file1 [file2 ...]\n");
		exit(1);
	}
	done = 1;
	int count = 0;
	pgsize = getpagesize()*8;
	int nprocs = get_nprocs();
	int numfiles = argc - 1;
	int size = 0;
	chunks = 0;
	int next = 1;
	int toProcess = 0;
	int offset = 0;
	int chunksize = 0;
	void *map = NULL;
		int rc = 0;
	//calculate total number of chunks to be processed
	for (int i = 1; i < numfiles + 1; i++) 
	{
		int fd = open(argv[i], O_RDONLY);
		if (fd == -1)
			continue;

		struct stat statbuf;
		fstat(fd, &statbuf);
		double fsize = (double) statbuf.st_size;

		int a = fsize / ((double)pgsize);
		if ( ((size_t) fsize) % pgsize != 0)
		a++;
		if (fsize != 0)
		chunks += a;
		close(fd);
	}

	//allocate
	queue = malloc(sizeof(QUEUE) * queueSize);
	finished = malloc(sizeof(ENTIREPIECE) * chunks);

	//create threads
	pthread_t thrds[nprocs];
	for (int i = 0; i < nprocs; i++) 
	{
		pthread_create(&thrds[i], NULL, consumer, NULL);
	}

	//loop over files and prepare chunks to be added to queue
	for (int file = 1; file < numfiles + 1;) 
	{
		if (next) 
		{
			rc = open(argv[file], O_RDONLY);
			if (rc == -1)
			{
				file++;
				continue;
			}

			struct stat statbuf;
			fstat(rc, &statbuf);
			toProcess = (size_t) statbuf.st_size;
			offset = 0;
			next = 0;
		}

		if (toProcess > pgsize){
			chunksize = pgsize;
		}
		else{
			chunksize = toProcess;
		}
		if (chunksize == 0) 
		{
			file++;
			close(rc);
			next = 1;
			continue;
		}

		//map chunks
		map = mmap(NULL, chunksize, PROT_READ , MAP_PRIVATE, rc, offset);

		QUEUE args;
		args.start = map;
		args.size  = chunksize;
		args.index = count;

		//producer
		pthread_mutex_lock(&lock);
		while (numfull == queueSize)
		pthread_cond_wait(&empty, &lock);
		do_fill(args);
		pthread_cond_signal(&fill);
		pthread_mutex_unlock(&lock);

		toProcess -= chunksize;
		offset  += chunksize;

		if (toProcess <= 0) 
		{
			file++;
			close(rc);
			next = 1;
		}

		count++;
	}
	done= 0;
	
	pthread_cond_broadcast(&fill);
	for (int i = 0; i < nprocs; i++) 
	{
		pthread_join(thrds[i], NULL);
	}
	
	char *prev = NULL;

	//Iterate over files and if adjacent letters are the same then add up counts.
	//Print out in binary
	for (int i = 0; i < chunks; i++) 
	{
		char *bin = finished[i].final;
		int n = finished[i].size;

		if (n == 0)
		{
			continue;
		}
		if (prev && prev[4] == bin[4])
		{
			if (n == 5) *((int*)prev) += *((int*)bin);
			else
			{
				*((int*)prev) += *((int*)bin);
				fwrite(prev, 5, 1, stdout);
				fwrite(bin+5, n-10, 1, stdout);
				prev = bin + n - 5;
			}
		}
		else if (prev)
		{
			fwrite(prev, 5, 1, stdout);
			fwrite(bin, n-5, 1, stdout);
			prev = bin + n - 5;
		}
		else if (n == 5)
		{
			prev = bin;
		}
		else
		{
			fwrite(bin, n-5, 1, stdout);
			prev = bin + n - 5;
		}
	}
	fwrite(prev, 5, 1, stdout);
	return 0;
}


void do_fill(QUEUE chunk)
{
	queue[fillptr] = chunk;
	fillptr = (fillptr + 1) % queueSize;
	numfull++;
}

QUEUE do_get()
{
	QUEUE temp = queue[useptr];
	useptr = (useptr + 1) % queueSize;
	numfull--;
	return temp;
}

void *consumer(void *ptr) 
{
	while (1) 
	{
		QUEUE consumed;
		pthread_mutex_lock(&lock);
		while (numfull == 0 && done)
		{
			pthread_cond_wait(&fill, &lock);
		}
		if (numfull == 0 && !done)
		{
			pthread_mutex_unlock(&lock);
			pthread_exit(0);
		}
		consumed = do_get();
		pthread_cond_signal(&empty);
		pthread_mutex_unlock(&lock);
		ENTIREPIECE *tmp = &finished[consumed.index];
		zip(consumed, tmp);
	}
	pthread_exit(0);
}

//function to do run length encoding
void zip(QUEUE chunk, ENTIREPIECE *tmp)
{
	char* start = chunk.start;
	char compare = *start;
	char *output = malloc(chunk.size * 8);
	char *current = output;
	int count = 0;
	size_t len = chunk.size;
	for (int i = 0; i < len; ++i)
	{
		if (start[i] == '\0') continue;
		if(start[i] != compare)
		{
			if (count == 0)
			{
				compare = start[i];
				count = 1;
			}
			else
			{
				*((int*)current) = count;
				current[4] = compare;
				current += 5;
				compare = start[i];
				count = 1;
			}
		}
		else count++;
	}

	if (start[len-1] == '\0')
	{
		if (count != 0)
		{
			*((int*)current) = count;
			current[4] = compare;
			current += 5;
		}
	}
	else
	{
		*((int*)current) = count;
		current[4] = start[len-1];
		current += 5;
	}
	size_t fin_size = current - output;
	char *res = malloc(fin_size);
	memcpy(res, output, current - output);
	tmp->size = fin_size;
	tmp->final = res;
	free(output);
	munmap(chunk.start, chunk.size);
	return;
}
