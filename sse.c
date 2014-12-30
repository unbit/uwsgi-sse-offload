#include "sse.h"

extern struct uwsgi_server uwsgi;

struct uwsgi_offload_engine *sse_redis_offload_engine;

static void sse_offload_destroy_config_do(struct sse_offload_config *soc) {
	if (soc->server) free(soc->server);
	if (soc->subscribe) free(soc->subscribe);
	if (soc->buffer_size_str) free(soc->buffer_size_str);
	free(soc);
}

static void sse_offload_destroy_config(struct uwsgi_offload_request *uor) {
	sse_offload_destroy_config_do((struct sse_offload_config *) uor->data);
}

static int sse_redis_offload(struct wsgi_request *wsgi_req, struct sse_offload_config *soc) {
        struct uwsgi_offload_request uor;
	uwsgi_offload_setup(sse_redis_offload_engine, &uor, wsgi_req, 1);
	if (!soc->server) {
                soc->server = uwsgi_str("127.0.0.1:6379");
        }
	if (soc->buffer_size_str) {
		soc->buffer_size = atoi(soc->buffer_size_str);
	}
	if (!soc->buffer_size) {
                soc->buffer_size = 4096;
        }
	uor.data = soc;
	uor.name = soc->server;
	uor.ubuf = uwsgi_buffer_new(uwsgi.page_size);
	if (uwsgi_buffer_append(uor.ubuf, "*2\r\n$9\r\nSUBSCRIBE\r\n$", 20)) goto error;
        if (uwsgi_buffer_num64(uor.ubuf, strlen(soc->subscribe))) goto error;
        if (uwsgi_buffer_append(uor.ubuf, "\r\n", 2)) goto error;
        if (uwsgi_buffer_append(uor.ubuf, soc->subscribe, strlen(soc->subscribe))) goto error;
        if (uwsgi_buffer_append(uor.ubuf, "\r\n", 2)) goto error;

	uor.free = sse_offload_destroy_config;
        return uwsgi_offload_run(wsgi_req, &uor, NULL);
error:
	uwsgi_buffer_destroy(uor.ubuf);
        return -1;
}

static int sse_router_func(struct wsgi_request *wsgi_req, struct uwsgi_route *ur) {
        if (!wsgi_req->socket->can_offload) {
                uwsgi_log("[sse_offload] unable to use \"sse\" router without offloading\n");
                return UWSGI_ROUTE_BREAK;
        }

        char **subject = (char **) (((char *)(wsgi_req))+ur->subject);
        uint16_t *subject_len = (uint16_t *)  (((char *)(wsgi_req))+ur->subject_len);

        struct uwsgi_buffer *ub = uwsgi_routing_translate(wsgi_req, ur, *subject, *subject_len, ur->data, ur->data_len);
        if (!ub) return UWSGI_ROUTE_BREAK;

        struct sse_offload_config *soc = uwsgi_calloc(sizeof(struct sse_offload_config));
        if (strchr(ub->buf, '=')) {
                if (uwsgi_kvlist_parse(ub->buf, ub->pos, ',', '=',
                        "server", &soc->server,
                        "subscribe", &soc->subscribe,
                        "buffer_size", &soc->buffer_size_str,
                        NULL)) {
                        uwsgi_log("[sse_offload] unable to parse sse action\n");
                        sse_offload_destroy_config_do(soc);
                        uwsgi_buffer_destroy(ub);
                        return UWSGI_ROUTE_BREAK;
                }
        }
        else {
                soc->subscribe = uwsgi_str(ub->buf);
        }

	if (!soc->subscribe) {
		uwsgi_log("[sse_offload] unable to use \"sse\" engine without a pubsub channel\n");
		sse_offload_destroy_config_do(soc);
                uwsgi_buffer_destroy(ub);
                return UWSGI_ROUTE_BREAK;
	}

        if (!wsgi_req->headers_sent) {
                if (!wsgi_req->headers_size) {
                        if (uwsgi_response_prepare_headers(wsgi_req, "200 OK", 6)) goto end;
                        if (uwsgi_response_add_content_type(wsgi_req, "text/event-stream", 17)) goto end;
                        if (uwsgi_response_add_header(wsgi_req, "Cache-Control", 13, "no-cache", 8)) goto end;
                }
                if (uwsgi_response_write_headers_do(wsgi_req) < 0) goto end;
        }


        if (!sse_redis_offload(wsgi_req, soc)) {
                wsgi_req->via = UWSGI_VIA_OFFLOAD;
                wsgi_req->status = 202;
                soc = NULL;
        }

end:
        if (soc) sse_offload_destroy_config_do(soc);
        uwsgi_buffer_destroy(ub);
        return UWSGI_ROUTE_BREAK;
}

static int sse_router(struct uwsgi_route *ur, char *args) {
        ur->func = sse_router_func;
        ur->data = args;
        ur->data_len = strlen(args);
        return 0;
}

static int sseraw_router(struct uwsgi_route *ur, char *args) {
	ur->custom = 1;
        return sse_router(ur, args);
}

static int sse_redis_offload_prepare(struct wsgi_request *wsgi_req, struct uwsgi_offload_request *uor) {
        if (!uor->name) {
                return -1;
        }

        uor->fd = uwsgi_connect(uor->name, 0, 1);
        if (uor->fd < 0) {
                uwsgi_error("sse_redis_offload_prepare()/connect()");
                return -1;
        }

        return 0;
}


static void sse_offload_register() {
	sse_redis_offload_engine = uwsgi_offload_register_engine("sse-redis", sse_redis_offload_prepare, sse_redis_offload_do);
	uwsgi_register_router("sse", sse_router);
        uwsgi_register_router("sseraw", sseraw_router);
}

struct uwsgi_plugin sse_offload_plugin = {
	.name = "sse_offload",
	.on_load = sse_offload_register,
};
