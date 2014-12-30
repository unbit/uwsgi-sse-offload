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

The plugin is 2.0 friendly (it requires uWSGI >=2.0.8):

```sh
uwsgi --build-plugin https://github.com/unbit/uwsgi-sse-offload
```

will generate sse_offload_plugin.so in the current directory

you can eventually build a monolithic binary with sse-offload plugin in one-shot:

```sh
curl http://uwsgi.it/install | UWSGI_EMBED_PLUGINS="sse_offload=https://github.com/unbit/uwsgi-sse-offload" bash -s psgi /tmp/uwsgi
```

this will result in a binary in /tmp/uwsgi with psgi and sse-offload support

in the same way (for a python setup):

```sh
UWSGI_EMBED_PLUGINS="sse_offload=https://github.com/unbit/uwsgi-sse-offload" pip install uwsgi
```

Usage (via internal routing)
============================

Let's start with a simple perl clock. A perl script will publish in the 'clock' redis channel the current unix time (seconds since the epoch):

```perl
use Redis;

my $redis = Redis->new;

while(1) {
        sleep(1);
        $redis->publish('clock', time);
}
```

save it as clock.pl

now we want an html page that "subscribe" to the /whattimeisit url via sse (server sent events) and set the content of the 'clock' div to the received data (yes, they are the timestamps sent to redis)

```html
<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8" />
</head>
<body>
  <script>
    var source = new EventSource('/whattimeisit');
    source.onmessage = function(e) {
      document.getElementById('clock').innerHTML = e.data;
    };
  </script>

<div id="clock" />
</body>
</html>
```

(save it as clock.html)

finally we start uWSGI with a mule running clock.pl and a rule for mapping requests to /whattimeisit to the sse engine:

```ini
[uwsgi]
; eventually use absolue path for the plugin if it is not in the current directory
plugin = sse_offload
; bind on http port 9090
http-socket = :9090
; run clock.pl as a mule
mule = clock.pl
; map requests to / to the html file
static-map = /=clock.html
; route requests to ^/whattimeisit to the sse engine subscribed to the 'clock' redis channel
route = ^/whattimeisit sse:clock
; enable 1 offload thread
offload-threads = 1
```

Usage (app-governed)
====================

If you want to hold control over the sse url (for example for managing authentication and authorization) you can pass control of the sse url to your app and (after having done your checks) pass back the control to the offload engine.

There are various ways to accomplish this, the easiest is using uWSGI request vars (this time we use python, so ensure to load the python plugin too if not available in your binary):

```python
import uwsgi
def application(environ, start_response):
    if environ['PATH_INFO'] == '/whattimeisit':
        uwsgi.add_var('X-SSE-OFFLOAD', 'clock')
        return []
    else:
        start_response('200 OK', [('Content-Type', 'text/plain')])
        return ['Hello World']
```

(save it as clock.py)

so when the PATH_INFO is '/whattimeisit', your app set the X-SSE-OFFLOAD variable to the name of the channel to subscribe. Now let's configure uWSGI to honour this variable:

```ini
[uwsgi]
plugin = python
; eventually use absolue path for the plugin if it is not in the current directory
plugin = sse_offload
; bind on http port 9090
http-socket = :9090
; run clock.pl as a mule
mule = clock.pl
; map requests to / to the html file
static-map = /=clock.html

; load the wsgi app
wsgi-file = clock.py

; tell the routing engine to check for X-SSE-OFFLOAD variable
final-route-if-not = empty:${X-SSE-OFFLOAD} sse:${X-SSE-OFFLOAD}
; enable 1 offload thread
offload-threads = 1
```

the 'final-route-if-not' rule tells the engine to run the 'sse' action if the X-SSE-OFFLOAD variable is not empty, passing its content as the sse action argument.

The sse engine is 'smart' about response headers, so you are free to generate them from your app without damaging the stream:

```python
import uwsgi
def application(environ, start_response):
    if environ['PATH_INFO'] == '/whattimeisit':
        uwsgi.add_var('X-SSE-OFFLOAD', 'clock')
        start_response('200 OK', [('Content-Type', 'event/stream'), ('Cache-Control', 'no-cache'), ('Foo', 'Bar')])
        return []
    else:
        start_response('200 OK', [('Content-Type', 'text/plain')])
        return ['Hello World']
```

or with Django:

```python
def sse_view(request, foobar):
    response = HttpResponse('', content_type='event/stream')
    response['Cache-Control'] = 'no-cache'
    uwsgi.add_var('X-SSE-OFFLOAD', 'clock')
    return response
```

Action parameters
=================

The 'sse' routing action takes a single parameter (the redis channel) or a keyval list:

```ini
route = ^/foobar sse:server=127.0.0.1:4040,subscribe=foobar
```

this will connect to the redis server 127.0.0.1:4040 subscribing to the channel 'foobar'

The folowing keys are available:

* `server` (the redis server address, unix sockets are supported too)
* `subscribe` (the channel to subscribe to)
* `buffer_size` (the buffer size for the response, default 4k, tune it only if you need to stream big messages for which having a bigger buffer could result in better performance)


The 'raw' mode
==============

The 'sse' action, takes every message from the redis channel and 'convert' it to sse format:

```
foobar
```

become

```
data: foobar\n\n
```

the 'converter' take rid of multiline messages too:

```
foobar\n
foobar2
```

is converted to

```
data: foobar\n
data: foobar2\n\n
```

If you want to disable this convertion and directly stream out the content of the redis message as-is, use the sseraw action:

```ini
route = ^/foobar sseraw:server=127.0.0.1:4040,subscribe=foobar
```

Tips&Tricks
===========
