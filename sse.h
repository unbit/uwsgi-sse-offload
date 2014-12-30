#include <uwsgi.h>

#define uwsgi_offload_retry if (uwsgi_is_again()) return 0;

struct sse_offload_config {
	char *server;
	char *subscribe;
	char *buffer_size_str;
	size_t buffer_size;
};

int sse_redis_offload_do(struct uwsgi_thread *, struct uwsgi_offload_request *, int);
