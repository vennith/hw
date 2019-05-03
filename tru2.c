/*
Cameron Brown
May 2, 2019
CSC331
pzip.c
*/
//Special thanks to user Saggarwall9 on github for being a reliable resource for the parallel threads in this assignment!

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <stdbool.h>
#include <errno.h>
#include <string.h>
#include <stdarg.h>

//global variables
int thread_count; //number of threads employed
char *map;  //mmapped array
char *zipArray;  //what will be written to the output
int *encode_size; //set to -1 by the end

//a simple decimal to binary converter
int decimalToBinary(int decimal)
{
	int remainder;
	int binary = 0;
	int i = 1;
	while (decimal != 0)
	{
		remainder = decimal % 2;
		decimal = decimal / 2;
		binary = binary + remainder * i;
		i = i * 10;
	}
	return binary;
}

//we use this during the zip to wait for threads to be finished!
bool waitForThread() {
	bool flag = true;
	while (flag) {
		flag = false;
		for (int i = 0; i < thread_count; i++) {
			if (encode_size[i] == -1) {
				flag = true;
			}
		}
	}
	return true;
}

void zip(char* src, size_t start, size_t end, bool isFinished, int position)
{
	int runLength;
	char sizecount[50];
	int  j = 0;
	int i, k;
	
	int size = 0;
	int i;
	for (i = start; i < end; i++) {
		size += 2;
		while (i + 1 < end && src[i] == src[i + 1]) {
			i++;
		}
	}
	encode_size[position] = size;

	//Wait for all threads to populate
	if (waitForThread()) {}


	for (i = 0; i < position; i++) {
		j += encode_size[i];
	}


	for (i = start; i < end; i++) {
		char hold = src[i];
		runLength = 1;
		while (i + 1 < end && src[i] == src[i + 1]) {
			runLength++;
			i++;
		}

		runLength = decimalToBinary(runLength);
		sprintf(sizecount, "%d", runLength);

		for (k = 0; *(sizecount + k); j++, k++) {
			zipArray[j] = sizecount[k];
		}
		zipArray[j++] = hold;
	}
	if (isFinished) {
		zipArray[j] = '\0';
	}
}

//struct suggested to be used by Saggarwal9
struct threadDetails
{
	size_t start, end;
	pthread_t tid;
	int position;
	bool isFinished;
};

//method that the threads run
void *mythread(void *arg) {
	struct threadDetails *t = (struct threadDetails*) arg;
	zip(map, t->start, t->end, t->isFinished, t->position);
	return NULL;
}

//checks if the file size is correct
static void
check(int test, const char * message, ...)
{
	if (test) {
		va_list args;
		va_start(args, message);
		vfprintf(stderr, message, args);
		va_end(args);
		fprintf(stderr, "\n");
		exit(EXIT_FAILURE);
	}
}


int main(int argc, char *argv[])
{
	if (argc < 3) {
		printf("./pzip <num_threads> file > file.z");
		exit(1);
	}
	for (int v = 2; v < argc; v++) {
		char *FILEPATH = argv[v];

		int i, fd, status;
		size_t size;
		struct stat s;
		thread_count = atoi(argv[1]);
		encode_size = (int*)malloc(sizeof(int) * thread_count);
		for (i = 0; i < thread_count; i++) {
			encode_size[i] = -1;
		}

		fd = open(FILEPATH, O_RDONLY);
		if (fd == -1) {
			perror("Error opening file for reading");
			exit(EXIT_FAILURE);
		}

		//use fstat() to get the file's size
		status = fstat(fd, &s);
		size = s.st_size;

		//it was suggested we use mmap to "read" the file. Incredibly tricky to work with, especially during the zip.
		map = mmap(0, size, PROT_READ, MAP_SHARED, fd, 0);
		if (map == MAP_FAILED) {
			close(fd);
			perror("Error mmapping the file");
			exit(EXIT_FAILURE);
		}
		int IDX_N = size;

		//initialize zipArray
		zipArray = (char*)malloc(sizeof(char) * (size * 2 + 1));

		//This portion follows the scaffolding suggested by Saggarwal9 on github!
		struct threadDetails t[thread_count];
		size_t idx_start, idx_end, idx_n = (IDX_N / thread_count);
		idx_start = 0;
		idx_end = idx_start + idx_n;


		for (i = 0; i < thread_count; i++) {
			t[i].start = idx_start;
			if (i == (thread_count - 1)) {
				t[i].end = IDX_N;
				t[i].isFinished = true;
			}
			else {
				t[i].end = idx_end;
				t[i].isFinished = false;
			}
			t[i].position = i;
			pthread_create(&t[i].tid, NULL, mythread, (void*)&t[i]);
			idx_start = idx_end;
			idx_end = (idx_end + idx_n < IDX_N ? idx_end + idx_n : IDX_N);
		}
		for (i = 0; i < thread_count; i++) {
			pthread_join(t[i].tid, NULL);
		}


		close(fd);

		int totalsize = 0;
		for (int i = 0; i < thread_count; i++) {
			totalsize += encode_size[i];
		}

		//writes it to the file all at once
		fwrite(zipArray, sizeof(char), totalsize, stdout);

		//free the memory used!
		free(zipArray);
		free(encode_size);

	}
	return 0;
}
