#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <liburing.h>
#include <arpa/inet.h>
#include <unistd.h>
#include "networkio.h"
#include "shutdown.h"
#include "worker.h"

#define PORT 8888
#define QUEUE_DEPTH 16
#define BUFFER_CNT QUEUE_DEPTH * 2
#define BUFFER_SZ 1024

static struct msghdr msghdr;
static char recv_buffers[BUFFER_CNT * BUFFER_SZ];

static int submit_recvmsg_sqe(struct io_uring *ring, int socket)
{
	struct io_uring_sqe *sqe;
	sqe = io_uring_get_sqe(ring);
	if (!sqe) {
		fprintf(stderr, "submit_recvmsg_sqe(): "
				"submission queue full!\n");
		return -1;
	}
	io_uring_prep_recvmsg_multishot(sqe, socket, &msghdr, 0);
	sqe->flags |= IOSQE_BUFFER_SELECT;
	sqe->buf_group = 0;
	io_uring_submit(ring);
	return 0;
}

static int handle_recvmsg_cqe(
	struct io_uring *ring,
	struct io_uring_buf_ring *buf_ring,
	struct io_uring_cqe *cqe)
{
	if (!(cqe->flags & IORING_CQE_F_BUFFER)) {
		fprintf(stderr, "handle_recvmsg_cqe(): "
				"cqe missing IORING_CQE_F_BUFFER flag\n");
		return -1;
	}

	int rv = 0;
	int bidx = cqe->flags >> 16;
	struct io_uring_recvmsg_out *out;
	char *buf = recv_buffers + (bidx * BUFFER_SZ);
	out = io_uring_recvmsg_validate(buf, cqe->res, &msghdr);
	if (!out) {
		fprintf(stderr, "handle_recvmsg_cqe(): "
				"message failed validation\n");
		rv = -1;
		goto recycle;
	}
	if (out->namelen > msghdr.msg_namelen) {
		fprintf(stderr, "handle_recvmsg_cqe(): "
				"truncated name\n");
		rv = -1;
		goto recycle;
	}
	if (out->flags & MSG_TRUNC)
	{
		fprintf(stderr, "handle_recvmsg_cqe(): "
				"truncated message\n");
		rv = -1;
		goto recycle;
	}

	// copy the buffer and send to worker pool
	struct workdata *data = malloc(sizeof(*data));
	if (!data) {
		perror("handle_recvmsg_cqe(): malloc()");
		rv = -1;
		goto recycle;
	}

	char *payload = io_uring_recvmsg_payload(out, &msghdr);
	data->addr = *((struct sockaddr_in*)io_uring_recvmsg_name(out));
	data->len = io_uring_recvmsg_payload_length(out, cqe->res, &msghdr);
	memcpy(&data->payload, payload, data->len);

	workqueue_push(&workqueue, data);
	printf("pushed to work queue\n");

recycle:
	io_uring_buf_ring_add(buf_ring, buf, BUFFER_SZ, bidx, BUFFER_CNT-1,0);
	io_uring_buf_ring_advance(buf_ring, 1);
	return rv;
}

void *recvmsg_loop(void *arg)
{
	int res;

	// block sigint on the main thread (for some reason? IBM told me to)
	sigset_t signal_set;
	sigemptyset(&signal_set);
	sigfillset(&signal_set);
	pthread_sigmask(SIG_BLOCK, &signal_set, NULL);

	// get a socket
	int s = socket(PF_INET, SOCK_DGRAM, 0);
	if (s < 0) {
		perror("socket()");
		goto shutdown;
	}

	// bind the socket
	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(PORT);
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	res = bind(s, (struct sockaddr *)&addr, sizeof(addr));
	if (res < 0) {
		perror("bind()");
		goto close_socket;
	}

	// initialize our io_uring

	struct io_uring ring;
	res = io_uring_queue_init(QUEUE_DEPTH, &ring, 0);
	if (res < 0) {
		perror("io_uring_queue_init()");
		goto close_socket;
	}

	// initialize our buffer ring
	struct io_uring_buf_ring *buf_ring;
	res = posix_memalign(
		(void **)&buf_ring, 
		sysconf(_SC_PAGESIZE), 
		BUFFER_CNT * sizeof(*buf_ring));
	if (res < 0) {
		perror("posix_memalign()");
		goto exit_uring_queue;
	}
	// register our buffer ring with our io ring
	struct io_uring_buf_reg buffer_reg;
	buffer_reg.ring_addr = (unsigned long)buf_ring;
	buffer_reg.ring_entries = BUFFER_CNT;
	buffer_reg.bgid = 0;
	res = io_uring_register_buf_ring(&ring, &buffer_reg, 0);
	if (res < 0) {
		errno = -res;
		perror("io_uring_register_buf_ring()");
		goto dealloc_buf_ring;
	}

	// add buffers to our buffer ring 
	io_uring_buf_ring_init(buf_ring);
	int mask = io_uring_buf_ring_mask(BUFFER_CNT);
	for (int i = 0; i < BUFFER_CNT; i++) {
		void *buffer = recv_buffers + (i * BUFFER_SZ);
		io_uring_buf_ring_add(buf_ring, buffer, BUFFER_SZ, i, mask, i);
	}
	io_uring_buf_ring_advance(buf_ring, BUFFER_CNT);

	// add recvmsg multishot sqe (and "initialize" our msghdr)
	msghdr.msg_namelen = sizeof(struct sockaddr_in);
	res = submit_recvmsg_sqe(&ring, s);
	if (res < 0) {
		fprintf(stderr, "idk how its full during init but whatever\n");
		goto dealloc_buf_ring;
	}

	// pump recvmsg loop
	struct io_uring_cqe *cqe;
	struct __kernel_timespec ts = { .tv_sec = 1, .tv_nsec = 0 };

	int shutdown_copy = 0;

	while(!should_shutdown()) {
		res = io_uring_wait_cqe_timeout(&ring, &cqe, &ts);
		if (res == -ETIME) {
			continue;
		}
		if (res < 0) {
			// the call to dequeue an cqe failed
			errno = -res;
			perror("io_uring_wait_cqe()");
			break;
		}
		if (cqe->res < 0) {
			// our request failed
			// TODO: don't shutdown here
			errno = -cqe->res;
			perror("recvmsg()");
			break;
		}

		res = handle_recvmsg_cqe(&ring, buf_ring, cqe);
		if (res < 0) {
			// TODO: don't shutdown here
			printf("handle_recvmsg_cqe() returned: %i\n", res);
			break;
		}
		io_uring_cqe_seen(&ring, cqe);

		if (!(cqe->flags & IORING_CQE_F_MORE)) {
			// for some reason, the multishot stopped
			submit_recvmsg_sqe(&ring, s);
		}
	}

dealloc_buf_ring:
	free(buf_ring);

exit_uring_queue:
	io_uring_queue_exit(&ring);

close_socket:
	close(s);

shutdown:
	graceful_shutdown();

	return NULL;
}
