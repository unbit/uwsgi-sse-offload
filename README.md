uwsgi-sse-offload
=================

uWSGI offload bridge between redis pubsub and server sent events (sse)

This is a fork of the uwsgi-realtime (https://github.com/unbit/uwsgi-realtime) project, exposing only sse features.

The bridge waits on a redis pubsub channel and whenever it receives a message it forwards it to the connected sse clients.

It is an offload engine so you can manage thousand of concurrent requests without bothering your workers/threads/async-cores

How it works
============

A client (read: a webbrowser) open an SSE connection to the webserver/proxy forwarding the request to uWSGI.

uWSGI (or your app) recognize (via internal routing or via special response headers) it is an sse session and forward it to the offload engine. The offload engine subscribe to a redis pubsub channel and starts waiting for messages.

Whenever a message is enqueued, the offload engine collects it and forward to the connected client.

Remember: the offload engine is fully non-blocking so you can manage thousand of clients concurrently while your blocking main app continues its job. The maximum number of clients is defined by about half of the file descriptors limit

Installation
============

The plugins is 2.0 friendly:

Usage (via internal routing)
============================

Usage (app-governed)
====================
