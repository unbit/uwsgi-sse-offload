import uwsgi
def application(environ, start_response):
    if environ['PATH_INFO'] == '/whattimeisit':
        uwsgi.add_var('X-SSE-OFFLOAD', 'clock')
        return []
    elif environ['PATH_INFO'] == '/whattimeisit2':
        uwsgi.add_var('X-SSE-OFFLOAD', 'clock')
        start_response('200 OK', [('Content-Type', 'event/stream'), ('Cache-Control', 'no-cache'), ('Foo', 'Bar')])
        return []
    else:
        start_response('200 OK', [('Content-Type', 'text/plain')])
        return ['Hello World']
