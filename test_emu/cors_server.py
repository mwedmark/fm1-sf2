import http.server, socket, ssl, sys, os
class H(http.server.SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header('Access-Control-Allow-Origin', '*')
        self.send_header('Access-Control-Allow-Private-Network', 'true')
        self.send_header('Cache-Control', 'no-store')   # a new firmware build or bank is fetched, never a cached one
        super().end_headers()
    def do_OPTIONS(self):
        self.send_response(204); self.end_headers()
    # stats.html posts each figures message it reads from the FM-1 here: one JSON object, appended as a line to dist/stats_log.jsonl
    # (stats_log.jsonl where there is no dist/), so they can be read on this computer. Nothing else is written; 4 KB at most a post.
    def do_POST(self):
        import json
        n = int(self.headers.get('Content-Length') or 0)
        if self.path != '/stats-log' or not 0 < n <= 4096:
            self.send_response(404 if self.path != '/stats-log' else 413); self.end_headers(); return
        try:
            line = json.dumps(json.loads(self.rfile.read(n)), separators=(',', ':'))
        except ValueError:
            self.send_response(400); self.end_headers(); return
        with open(os.path.join('dist', 'stats_log.jsonl') if os.path.isdir('dist') else 'stats_log.jsonl', 'a') as f:
            f.write(line + '\n')
        self.send_response(204); self.end_headers()
os.chdir(sys.argv[2])
# argv[3]: the address to listen on, 127.0.0.1 unless given (start_emu.ps1 -Lan: '::', every address, IPv4 too)
host = sys.argv[3] if len(sys.argv) > 3 else '127.0.0.1'
class Server(http.server.ThreadingHTTPServer):
    address_family = socket.AF_INET6 if ':' in host else socket.AF_INET
    def server_bind(self):
        if ':' in host: self.socket.setsockopt(socket.IPPROTO_IPV6, socket.IPV6_V6ONLY, 0)
        super().server_bind()
server = Server((host, int(sys.argv[1])), H)
# argv[4]: a PEM file with certificate and key: then HTTPS (Chrome runs AudioWorklet from a secure origin, so no flag is needed)
if len(sys.argv) > 4:
    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    ctx.load_cert_chain(sys.argv[4])
    server.socket = ctx.wrap_socket(server.socket, server_side=True)
server.serve_forever()
