uwsgi-sse-offload
=================

uWSGI offload bridge between redis pubsub and server sent events (sse)

This is a fork of the uwsgi-realtime (https://github.com/unbit/uwsgi-realtime) project, exposing only sse features.

The bridge waits on a redis pubsub channel and whenever it receives a message it forwards it to the connected sse clients.

It is an offload engine so you can manage thousand of concurrent requests without bothering your workers/threads/async-cores
