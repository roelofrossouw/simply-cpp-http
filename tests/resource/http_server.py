"""A stand-in web server for sc-http's tests. Usage: http_server.py SERVER_CERT SERVER_KEY CLIENT_CA
Serves the same routes three ways, on free ports of the system's choice, and says which on stdout
("listening HTTP HTTPS MTLS"): plain HTTP, HTTPS with SERVER_CERT, and HTTPS that also wants a client
certificate signed by (or being) CLIENT_CA.

  /echo           what was asked, as JSON: method, path, query, headers (lower-cased names), body
                  (base64), content_type
  /status/N       status N, the body "status N"
  /redirect/N     302 to /redirect/N-1, and /redirect/0 to /echo
  /to-file        302 to file:///etc/hosts (which a client must refuse to follow)
  /slow           answers after 3 s
  /drip           "drips" one byte every 0.6 s: slow but steady, 3 s in all
  /stall          one byte of "stall", then nothing for 4 s
  /gzip           "compressed hello" gzipped when the request accepts gzip (Content-Encoding: gzip), else plain
  /binary         the bytes 0 to 255, and a header X-Test: yes
"""
import base64, gzip, http.server, json, ssl, sys, threading, time, urllib.parse

server_cert, server_key, client_ca = sys.argv[1:4]


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *args):
        pass

    def send(self, status, body, content_type="text/plain", extra=None):
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        for name, value in (extra or {}).items():
            self.send_header(name, value)
        self.end_headers()
        if self.command != "HEAD":
            self.wfile.write(body)

    def handle_any(self):
        url = urllib.parse.urlsplit(self.path)
        length = int(self.headers.get("Content-Length") or 0)
        body = self.rfile.read(length) if length else b""
        if url.path == "/echo":
            answer = {"method": self.command, "path": url.path, "query": url.query,
                      "headers": {name.lower(): value for name, value in self.headers.items()},
                      "body": base64.b64encode(body).decode(), "content_type": self.headers.get("Content-Type")}
            return self.send(200, json.dumps(answer).encode(), "application/json")
        if url.path.startswith("/status/"):
            code = int(url.path.split("/")[2])
            return self.send(code, ("status %d" % code).encode())
        if url.path.startswith("/redirect/"):
            left = int(url.path.split("/")[2])
            target = "/echo" if left == 0 else "/redirect/%d" % (left - 1)
            return self.send(302, b"", extra={"Location": target})
        if url.path == "/to-file":
            return self.send(302, b"", extra={"Location": "file:///etc/hosts"})
        if url.path == "/slow":
            time.sleep(3)
            return self.send(200, b"slow")
        if url.path in ("/drip", "/stall"):
            # Headers first, then the body a byte at a time: what an idle limit tells apart
            self.send_response(200)
            self.send_header("Content-Type", "text/plain")
            self.send_header("Content-Length", "5")
            self.end_headers()
            self.wfile.flush()
            for i, byte in enumerate(b"drips" if url.path == "/drip" else b"stall"):
                time.sleep(0.6 if url.path == "/drip" else (0 if i == 0 else 4))
                self.wfile.write(bytes([byte]))
                self.wfile.flush()
            return
        if url.path == "/gzip":
            if "gzip" in (self.headers.get("Accept-Encoding") or ""):
                return self.send(200, gzip.compress(b"compressed hello"), extra={"Content-Encoding": "gzip"})
            return self.send(200, b"compressed hello")
        if url.path == "/binary":
            return self.send(200, bytes(range(256)), "application/octet-stream", {"X-Test": "yes"})
        self.send(404, b"no such route")

    do_GET = do_POST = do_PUT = do_PATCH = do_DELETE = do_HEAD = handle_any


def serve(context=None):
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    if context:
        server.socket = context.wrap_socket(server.socket, server_side=True)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    return server.server_address[1]


tls = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
tls.load_cert_chain(server_cert, server_key)
mutual = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
mutual.load_cert_chain(server_cert, server_key)
mutual.verify_mode = ssl.CERT_REQUIRED
mutual.load_verify_locations(client_ca)
# macOS's own libcurl (LibreSSL 3.3.6) sends a client certificate over TLS 1.3 in a way OpenSSL
# 4.x refuses ("tlsv1 alert decode error"); over TLS 1.2 they agree.
mutual.maximum_version = ssl.TLSVersion.TLSv1_2

print("listening", serve(), serve(tls), serve(mutual), flush=True)
while True:
    time.sleep(3600)
