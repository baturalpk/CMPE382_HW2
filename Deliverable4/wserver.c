#include <stdio.h>
#include <pthread.h>
#include "request.h"
#include "io_helper.h"

#define TRUE 1

typedef struct conn_fd_t conn_fd;
struct conn_fd_t {
	int fd;
	conn_fd *next;
};

typedef struct queue_t queue;
struct queue_t {
	conn_fd *first;
	int len;
	int connection_count;
	pthread_cond_t ready;
	pthread_mutex_t mutex;
};

void *worker_routine(void *);

char default_root[] = ".";

//
// ./wserver [-p <portnum>] [-t threads] [-b buffers] 
// 
// e.g.
// ./wserver -p 2022 -t 5 -b 10
// 
int main(int argc, char *argv[]) {
    int c;
    char *root_dir = default_root;
    int port = 10000;
	int threads = 2;
	int buffer_size = 5;
    
    while ((c = getopt(argc, argv, "p:t:b:")) != -1)
		switch (c) {
		case 'p':
			port = atoi(optarg);
			break;
		case 't':
			threads = atoi(optarg);
			break;
		case 'b':
			buffer_size = atoi(optarg);
			break;
		default:
			fprintf(stderr, "usage: ./wserver [-p <portnum>] [-t threads] [-b buffers] \n");
			exit(1);
		}

	
	
	printf("Server running on port: %d, threads: %d, buffer: %d\n", port, threads, buffer_size);

    // run out of this directory
    chdir_or_die(root_dir);

	queue *q = (queue *) malloc(1 * sizeof(queue));
	q->len = 0;
	q->connection_count = 0;
	q->first = NULL;
	pthread_cond_init(&(q->ready), NULL);
	pthread_mutex_init(&(q->mutex), NULL);

	pthread_t pool[threads];
	for (int i = 0; i < threads; ++i) {
		int rc = pthread_create(&(pool[i]), NULL, worker_routine, q);
		if (rc != 0) {
			perror("failed to create thread");
			exit(1);
		}
	}

    int listen_fd = open_listen_fd_or_die(port);
    while (1) {
		struct sockaddr_in client_addr;
		int client_len = sizeof(client_addr);

		conn_fd *cfd = (conn_fd *) malloc(1 * sizeof(conn_fd));
		cfd->fd = accept_or_die(listen_fd, (sockaddr_t *) &client_addr, (socklen_t *) &client_len);

		if ((q->connection_count + 1) > buffer_size) {
			close_or_die(cfd->fd);
			printf("connection dropped: exceeds buffer size: %d\n", q->connection_count + 1);
			free(cfd);
			continue;
		}

		pthread_mutex_lock(&(q->mutex));
		if (q->len != 0) {
			q->first->next = cfd;
		}
		else {
			q->first = cfd;
		}

		q->len += 1;
		q->connection_count += 1;
		pthread_cond_signal(&(q->ready));

		pthread_mutex_unlock(&(q->mutex));
	}
    return 0;
}

void *worker_routine(void *arg) {
	queue *q = (queue *) arg;
	conn_fd *connection;

	pthread_mutex_lock(&(q->mutex));

	while (TRUE) {
		if (q->len == 0) {
			pthread_cond_wait(&(q->ready), &(q->mutex));
			continue;
		}

		connection = q->first;
		q->len -= 1;

		if (q->len != 0) {
			q->first = q->first->next;
		}
		else {
			q->first = NULL;
		}

		pthread_mutex_unlock(&(q->mutex));

		request_handle(connection->fd);
		close_or_die(connection->fd);
		q->connection_count -= 1;

		free(connection);
	}
}
