#include "sse.h"

static ssize_t _redis_num(char *buf, size_t len, int64_t *n) {
	char *ptr = buf;
	int is_negative = 0;
	int64_t num = 0;

	if (*ptr == '-') {
		is_negative = 1;
		ptr++;
		len--;
	}

	for(;;) {
		// more ?
		if (len == 0) return 0;
		if (*ptr == '\r') {
			break;
		}
		if (!isdigit((int) (*ptr))) {
			return -1;
		}
		num = (num*10)+((*ptr) - '0');
		ptr++;
		len--;
	}

	ptr++;
	len--;
	if (len == 0) return 0;
	if (*ptr != '\n') return -1;

	ptr++;

	if (is_negative) {
		num = -num;
	}

	*n = num;
	return ptr - buf;	
}


static ssize_t _redis_string(char *buf, size_t len) {
        char *ptr = buf;

        for(;;) {
                // more ?
                if (len == 0) return 0;
                if (*ptr == '\r') {
                        break;
                }
                ptr++;
                len--;
        }

        ptr++;
        len--;  
        if (len == 0) return 0;
        if (*ptr != '\n') return -1;

	ptr++;

        return ptr - buf;
}

static ssize_t _redis_bulk(char *buf, size_t len, char **str, int64_t *str_len) {
	ssize_t ret = _redis_num(buf, len, str_len);
	if (ret <= 0) return ret;
	buf += ret; len -= ret;
	char *ptr = buf;
	*str = buf;
	int64_t n = *str_len;
	while(n > 0) {
		// more
		if (len == 0) return 0;
                ptr++;
                len--;	
		n--;
	}

	if (len == 0) return 0;
	if (*ptr != '\r') return -1;
        ptr++;
        len--;	
	if (len == 0) return 0;
	if (*ptr != '\n') return -1;

	ptr++;

	return (ptr - buf) + ret;
}

static ssize_t _redis_parse(char *buf, size_t len, char *type, int64_t *n, char **str) {
	if (len == 0) return 0;
	*type = *buf;
	buf++;
	len--;
	if (len == 0) return 0;

	ssize_t ret, array_ret;
	int64_t i;

	switch(*type) {
		// simple string
                case '+':
		// error
                case '-':
			ret = _redis_string(buf, len);
			break;
                // int
                case ':':
			ret = _redis_num(buf, len, n);
			break;
                // bulk strings
                case '$':
			ret = _redis_bulk(buf, len, str, n);
			break;
                // array
                case '*':
			array_ret = _redis_num(buf, len, n);
			if (array_ret <= 0) return array_ret;
			buf += array_ret;
			len -= array_ret;
			ret = array_ret;
                        for(i=0;i<(*n);i++) {
				char array_type;
				int64_t array_n;
				char *array_str;
				array_ret = _redis_parse(buf, len, &array_type, &array_n, &array_str);
				if (array_ret <= 0) return array_ret;
				buf += array_ret;
                        	len -= array_ret;
                        	ret += array_ret;
                        }
			break;
                default:
                        return -1;
	}

	if (ret > 0) ret++;
	return ret;
}

static ssize_t _redis_pubsub(char *buf, size_t len, int64_t *n, char **str) {
	*n = 0;
	if (len == 0) return 0;
	char *ptr = buf;
	if (*ptr != '*') return -1;
	ptr++;
        len--;
        if (len == 0) return 0;

	int64_t array_n = 0;
	ssize_t array_ret = _redis_num(ptr, len, &array_n);
	if (array_ret <= 0) return array_ret;

	ptr += array_ret;
        len -= array_ret;

	if (array_n != 3) return -1;

	if (len == 0) return 0;
        if (*ptr != '$') return -1;
        ptr++; len--;

	char *array_str;
	array_ret = _redis_bulk(ptr, len, &array_str, &array_n);
	if (array_ret <= 0) return array_ret;

        ptr += array_ret;
        len -= array_ret;

	if (array_n <= 0) return -1;

	if (!uwsgi_strncmp(array_str, array_n, "message", 7)) {
		if (len == 0) return 0;
		if (*ptr != '$') return -1;
		ptr++; len--;
		array_ret = _redis_bulk(ptr, len, &array_str, &array_n);
        	if (array_ret <= 0) return array_ret;

		ptr += array_ret;
        	len -= array_ret;

		if (len == 0) return 0;
                if (*ptr != '$') return -1;
                ptr++; len--;
		// directly set output
                array_ret = _redis_bulk(ptr, len, str, n);
                if (array_ret <= 0) return array_ret;

		ptr += array_ret;
	}
	else {
		// ignore
		char type;
		array_ret = _redis_parse(ptr, len, &type, &array_n, &array_str);
		if (array_ret <= 0) return array_ret;
        	ptr += array_ret;
        	len -= array_ret;

		if (len == 0) return 0;

		array_ret = _redis_parse(ptr, len, &type, &array_n, &array_str);
                if (array_ret <= 0) return array_ret;
                ptr += array_ret;
	}

	return ptr - buf;
}

static char *sse_build(char *message, int64_t message_len, uint64_t *final_len) {
        int64_t i;
        struct uwsgi_buffer *ub = uwsgi_buffer_new(message_len);
        char *ptr = message;
        size_t len = 0;
        for(i=0;i<message_len;i++) {
                len++;
                if (message[i] == '\n') {
                        if (uwsgi_buffer_append(ub, "data: ", 6)) goto error;
                        if (uwsgi_buffer_append(ub, ptr, len)) goto error;
                        ptr = message+i+1;
                        len = 0;
                }
        }

        if (uwsgi_buffer_append(ub, "data: ", 6)) goto error;
        if (len > 0) {
                if (uwsgi_buffer_append(ub, ptr, len)) goto error;
        }
        if (uwsgi_buffer_append(ub, "\n\n", 2)) goto error;
        *final_len = ub->pos;
        char *buf = ub->buf;
        ub->buf = NULL;
        uwsgi_buffer_destroy(ub);
        return buf;
error:
        uwsgi_buffer_destroy(ub);
        return NULL;
}

static int sse_redis_subscribe_ubuf(struct uwsgi_thread *ut, struct uwsgi_offload_request *uor, int fd) {
        if (fd == uor->fd) {
                ssize_t rlen = write(uor->fd, uor->ubuf->buf + uor->written, uor->ubuf->pos - uor->written);
                if (rlen > 0) {
                        uor->written += rlen;
                        if (uor->written >= (size_t) uor->ubuf->pos) {
                                // reset buffer
                                uor->ubuf->pos = 0;
                                uor->status = 2;
                                if (event_queue_add_fd_read(ut->queue, uor->s))
                                        return -1;
                                if (event_queue_fd_write_to_read(ut->queue, uor->fd))
                                        return -1;
                        }
                        return 0;
                }
                else if (rlen < 0) {
                        uwsgi_offload_retry
			uwsgi_error("sse_redis_subscribe_ubuf()/write()");
                }
        }
        return -1;
}

static int sse_redis_write_buf(struct uwsgi_thread *ut, struct uwsgi_offload_request *uor) {
        // forward the message to the client
        ssize_t rlen = write(uor->s, uor->buf + uor->pos, uor->to_write);
        if (rlen > 0) {
                uor->to_write -= rlen;
                uor->pos += rlen;
                if (uor->to_write == 0) {
                        if (event_queue_fd_write_to_read(ut->queue, uor->s))
                                return -1;
                        if (event_queue_add_fd_read(ut->queue, uor->fd))
                                return -1;
                        uor->status = 2;
                }
                return 0;
        }
        else if (rlen < 0) {
                uwsgi_offload_retry
		uwsgi_error("sse_redis_write_buf() -> write()");
        }
        return -1;
}

int sse_redis_offload_do(struct uwsgi_thread *ut, struct uwsgi_offload_request *uor, int fd) {

        struct sse_offload_config *soc = (struct sse_offload_config *) uor->data;

	// setup
        if (fd == -1) {
                event_queue_add_fd_write(ut->queue, uor->fd);
                return 0;
        }

        switch(uor->status) {
                // waiting for connection
                case 0:
                        if (fd == uor->fd) {
                                uor->status = 1;
                                // ok try to send the request right now...
                                return sse_redis_offload_do(ut, uor, fd);
                        }
                        return -1;
                // writing the SUBSCRIBE request
                case 1:
                        return sse_redis_subscribe_ubuf(ut, uor, fd);
                // read event from s or fd
                case 2:
                        if (fd == uor->fd) {
                                // ensure ubuf is big enough
                                if (uwsgi_buffer_ensure(uor->ubuf, soc->buffer_size)) return -1;
                                ssize_t rlen = read(uor->fd, uor->ubuf->buf + uor->ubuf->pos, soc->buffer_size);
                                if (rlen > 0) {
                                        uor->ubuf->pos += rlen;
                                        // check if we have a full redis message
                                        int64_t message_len = 0;
                                        char *message;
                                        ssize_t ret = _redis_pubsub(uor->ubuf->buf, uor->ubuf->pos, &message_len, &message);
                                        if (ret > 0) {
                                                if (message_len > 0) {
                                                        if (uor->buf) free(uor->buf);
                                                        uint64_t final_len = 0;
                                                        uor->buf = sse_build(message, message_len, &final_len);
                                                        if (!uor->buf) return -1;
                                                        message_len = final_len;
                                                        uor->to_write = message_len;
                                                        uor->pos = 0;
                                                        if (event_queue_del_fd(ut->queue, uor->fd, event_queue_read())) return -1;\
                                                        if (event_queue_fd_read_to_write(ut->queue, uor->s)) return -1;
                                                        uor->status = 3;
                                                }
                                                if (uwsgi_buffer_decapitate(uor->ubuf, ret)) return -1;
                                                // again
                                                ret = 0;
                                        }
                                        // 0 -> again -1 -> error
                                        return ret;
                                }
                                if (rlen < 0) {
                                        uwsgi_offload_retry
                                        uwsgi_error("sse_redis_offload_do()/read()");
                                }
                        }
                        // an event from the client can only mean disconneciton
                        return -1;
                // write event on s
                case 3:
                        return sse_redis_write_buf(ut, uor);
                default:
                        break;
        }

        return -1;
}
