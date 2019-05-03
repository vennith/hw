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

#define MAX_RLEN 50

int thread_count;
char *map;  /* mmapped array of char's */
char *dest;  //tentative final written array
int *encode_size; //will be set to -1, until all threads set this, we do not write to dest

/*a simple decimal to binary converter*/
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

//finds the size the encoding will return by adding all elements in encode_size[]
int sum_size() {
	int size = 0;
	for (int i = 0; i < thread_count; i++) {
		size += encode_size[i];
	}
	return size;
}

void find_encode_size(char* src, size_t start, size_t end, int position) {
	//pretty simple, basically you're just going to keep adding 2, to calc the size
	int size = 0; //return this
	int i;
	for (i = start; i < end; i++) {
		size += 2;
		while (i + 1 < end && src[i] == src[i + 1]) {
			i++;
		}
	}

	encode_size[position] = size;
}

//what an eyesore, couldn't get it to stop giving me warnings
bool pass_size_inspection() {
	bool passed_inspection = true;
	while (passed_inspection) {
		// printf("am i stuck?");
		passed_inspection = false;
		for (int i = 0; i < thread_count; i++) {
			if (encode_size[i] == -1) {
				passed_inspection = true;
			}
		}
	}
	return true;
}

void encode(char* src, size_t start, size_t end, bool end_flag, int position)
{
	int rLen;
	char count[MAX_RLEN];
	int  j = 0;
	int i, k;
	find_encode_size(src, start, end, position); //populating array encode_size
	if (pass_size_inspection()) { //wait until all the sizes are populated
	}
	//so, iterate through the array, and add up all the values. start there.
	for (i = 0; i < position; i++) {
		j += encode_size[i];
	}


	/* traverse the input string one by one */
	for (i = start; i < end; i++) {
		/* Copy the first occurrence of the new character */
		char hold = src[i];
		rLen = 1;
		while (i + 1 < end && src[i] == src[i + 1]) {
			rLen++;
			i++;
		}

		rLen = decimalToBinary(rLen);

		/* Store rLen in a character array count[] */
		sprintf(count, "%d", rLen);
		/* Copy the count[] to destination */
		for (k = 0; *(count + k); j++, k++) {
			dest[j] = count[k];
		}
		dest[j++] = hold;
	}
	if (end_flag) {
		dest[j] = '\0';
	}
}

struct thread_details
{
	size_t start, end;
	pthread_t tid;
	int position;
	bool end_flag;

};

//method that the threads run
void *mythread(void *arg) {
	struct thread_details *t = (struct thread_details*) arg;
	encode(map, t->start, t->end, t->end_flag, t->position);
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
	//too few arguments will exit
	if (argc < 3) {
		printf("There are not enough arguments");
		exit(1);
	}
	for (int v = 2; v < argc; v++) {
		char *FILEPATH = argv[v];

		int i, fd, status;
		size_t size;
		struct stat s;
		thread_count = atoi(argv[1]);
		encode_size = (int*)malloc(sizeof(int) * thread_count); //sizing to thread count
		for (i = 0; i < thread_count; i++) {
			encode_size[i] = -1;  //initializing to all -1
		}


		/*opens the file using open*/
		fd = open(FILEPATH, O_RDONLY);
		if (fd == -1) {
			perror("Error opening file for reading");
			exit(EXIT_FAILURE);
		}

		/* Get the size of the file. */
		status = fstat(fd, &s);
		check(status < 0, "stat %s failed: %s", FILEPATH, strerror(errno));
		size = s.st_size;



		//mmap happens here
		map = mmap(0, size, PROT_READ, MAP_SHARED, fd, 0);
		if (map == MAP_FAILED) {
			close(fd);
			perror("Error mmapping the file");
			exit(EXIT_FAILURE);
		}

		//this is the number of elements in the file.
		int IDX_N = size;

		//writing to the location that will be encoded.
		dest = (char*)malloc(sizeof(char) * (size * 2 + 1));

		/*creating the references as seen in
		https://stackoverflow.com/questions/37104895/dividing-work-up-between-threads-
		pthread*/
		struct thread_details t[thread_count];
		size_t idx_start, idx_end, idx_n = (IDX_N / thread_count);
		idx_start = 0;
		idx_end = idx_start + idx_n;


		for (i = 0; i < thread_count; i++) {
			t[i].start = idx_start;
			/*if the loop is on it's last step, the thread will take on the remaining elements
			in the array, and flag is set to true, which may be used in encode*/
			if (i == (thread_count - 1)) {
				t[i].end = IDX_N;
				t[i].end_flag = true;
			}
			else {
				t[i].end = idx_end;
				t[i].end_flag = false;
			}
			t[i].position = i;
			pthread_create(&t[i].tid, NULL, mythread, (void*)&t[i]);
			idx_start = idx_end;
			//Potential failsafe, should not hit the else condition
			idx_end = (idx_end + idx_n < IDX_N ? idx_end + idx_n : IDX_N);
		}
		for (i = 0; i < thread_count; i++) {
			pthread_join(t[i].tid, NULL);
		}

		if (munmap(map, size) == -1) {
			perror("Error un-mmapping the file");
		}
		close(fd);
		//use sum_size so dest doesn't write in empty elements
		fwrite(dest, sizeof(char), sum_size(), stdout);

		free(dest);
		free(encode_size);
		//end of initial for loop
	}
	return 0;
}
