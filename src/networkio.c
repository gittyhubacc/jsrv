#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <liburing.h>
#include <arpa/inet.h>
#include <unistd.h>
#include "networkio.h"
#include "shutdown.h"

#define PORT 8888
#define MAX_CONNECTIONS 16
#define CONN_BUF_SZ 256

// connection
struct conn {
	int socket;
	struct sockaddr_in addr;
	char addrstr[INET_ADDRSTRLEN];
	char buffer[CONN_BUF_SZ];
};

// connection manager
struct connman {
	int open_connections;
	struct conn conns[MAX_CONNECTIONS];
};

static struct connman connman;
static struct sockaddr_in addr;
static socklen_t addr_sz;

static void connman_initialize()
{
	connman.open_connections = 0;
	for (int i = 0; i < MAX_CONNECTIONS; i++) {
		connman.conns[i].socket = -1;
	}
}

static int connman_can_accept()
{
	return connman.open_connections < MAX_CONNECTIONS;
}

static struct conn *connman_accept_conn(int socket)
{
	for (int i = 0; i < MAX_CONNECTIONS; i++) {
		if (connman.conns[i].socket < 0) {
			connman.conns[i].socket = socket;
			connman.conns[i].addr = addr;
			connman.open_connections++;
			inet_ntop(
				AF_INET, 
				&connman.conns[i].addr.sin_addr, 
				connman.conns[i].addrstr,
				INET_ADDRSTRLEN
			);
			return &connman.conns[i];
		}
	}
	return NULL;
}

static void connman_close_connection(struct conn *conn)
{
	conn->socket = -1;
	connman.open_connections--;
}


static void submit_recv_sqe(struct io_uring *ring, struct conn *conn)
{
	struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
	io_uring_prep_recv(sqe, conn->socket, conn->buffer, CONN_BUF_SZ, 0);
	io_uring_sqe_set_data(sqe, conn);
	io_uring_submit(ring);
}

static void handle_recv_cqe(struct io_uring *ring, struct io_uring_cqe *cqe)
{
	// for now just print to stdout
	struct conn *conn = (struct conn *)cqe->user_data;
	if (cqe->res == -104) {
		printf("%s(%i): messy close\n", conn->addrstr, conn->socket);
		connman_close_connection(conn);
		return;
	} else if (cqe->res < 0) {
		perror("recv()");
		connman_close_connection(conn);
		return;
	} else if (cqe->res == 0) {
		printf("%s(%i): close\n", conn->addrstr, conn->socket);
		connman_close_connection(conn);
		return;
	} 
	printf(
		"%s(%i): recv %i bytes of data:\n", 
		conn->addrstr, conn->socket, cqe->res
	);
	conn->buffer[cqe->res] = '\0';
	printf("\t%s", conn->buffer);
	submit_recv_sqe(ring, conn);
}

static void submit_accept_sqe(struct io_uring *ring, int socket)
{
	struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
	struct sockaddr *sockaddr = (struct sockaddr *)&addr;
	io_uring_prep_accept(sqe, socket, sockaddr, &addr_sz, 0);
	io_uring_sqe_set_data(sqe, NULL);
	io_uring_submit(ring);
}

static void handle_accept_cqe(
	struct io_uring *ring,
	struct io_uring_cqe *cqe, 
	int socket)
{
	//TODO: submit accept only when we know we have space
	if (cqe->res >= 0 && connman_can_accept()) {
		struct conn *conn = connman_accept_conn(cqe->res);
		printf(
			"accepted connection from %s(%i)\n", 
			conn->addrstr, 
			conn->socket
		);
		submit_recv_sqe(ring, conn);
	}	// TODO: logging
	submit_accept_sqe(ring, socket);
}

void *accept_recv_loop(void *arg)
{
	int s;
	int res;
	struct sockaddr_in addr;
	struct io_uring ring;
	struct io_uring_cqe *cqe;

	addr_sz = sizeof(addr);
	connman_initialize();

	s = socket(PF_INET, SOCK_STREAM, 0);
	if (s < 0) {
		perror("socket()");
		graceful_shutdown();
		return NULL;
	}

	res = 1;
	res = setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &res, sizeof(int));
	if (res < 0) {
		perror("setsockopt()");
		close(s);
		graceful_shutdown();
		return NULL;
	}

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(PORT);
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	res = bind(s, (struct sockaddr *)&addr, sizeof(addr));
	if (res < 0) {
		perror("bind()");
		close(s);
		graceful_shutdown();
		return NULL;
	}

	res = listen(s, MAX_CONNECTIONS);
	if (res < 0) {
		perror("listen()");
		close(s);
		graceful_shutdown();
		return NULL;
	}

	io_uring_queue_init(MAX_CONNECTIONS + 1, &ring, 0);

	submit_accept_sqe(&ring, s);
	while(1) {
		res = io_uring_wait_cqe(&ring, &cqe);
		if (res < 0) {
			perror("io_uring_wait_cqe()");
			close(s);
			graceful_shutdown();
			return NULL;
		}

		// we don't attach user data for accept sqe
		if (cqe->user_data) {
			handle_recv_cqe(&ring, cqe);
		} else {
			handle_accept_cqe(&ring, cqe, s);
		}

		io_uring_cqe_seen(&ring, cqe);
	}

	return NULL;
}

