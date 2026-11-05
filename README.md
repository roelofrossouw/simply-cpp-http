# simply-cpp-http

`sc-http` is the HTTP client of the simply-cpp suite: `sc::rest` over libcurl, and `sc::ollama`,
which talks to an [Ollama](https://ollama.com) server with it. Both were in sc-core until sc-core
2.0.0; their names and headers (`rest.h`, `ollama.h`) didn't change.

## Install

```bash
curl -fsSL https://apt.roelof.co.za/setup.sh | bash
sudo apt install simply-cpp-http-dev
```

## Usage

```cmake
find_package(sc-http CONFIG REQUIRED)
target_link_libraries(myapp PRIVATE sc::sc-http)   # or sc::sc-http-shared
```

```cpp
#include <rest.h>

sc::rest api{"https://example.com/api/items"};
api.bearer(token);
const auto body = api.timeout(5).post(R"({"name": "x"})");   // JSON unless header() says otherwise
if (api.status() != 200) std::cerr << api.status() << " " << api.error() << '\n';

sc::rest search{"https://example.com/find"};
search.query("q", "a b").query({{"page", "2"}});              // ?q=a%20b&page=2
const auto page = search.follow_redirects().get();            // search.effective_url(), total_time()

sc::rest soap{"https://example.com/service"};
soap.header("Content-Type", "text/xml; charset=utf-8");
soap.header("SOAPAction", "\"urn:Ping\"");
soap.client_certificate("/etc/app/client.pem", {}, passphrase); // certificate and key in one PEM
const auto envelope = soap.quiet().post(xml);

sc::rest upload{"https://example.com/upload"};
upload.post_form({{"name", "Jo"}, {"tag", "a"}});              // application/x-www-form-urlencoded
upload.post_multipart({{.name = "note", .value = "hi"},
                       {.name = "file", .file = "/tmp/report.pdf", .filename = "report.pdf"}});
```

- **Methods:** `get()`, `head()`, `post(body)`, `put(body)`, `patch(body)`, `del()`,
  `post_form(fields)`, `post_multipart(parts)`. Each returns the response body, whatever the
  status, byte for byte; `status()` is 0 when no response came, and `error()` says why
  (`error_code()`: curl's number, 7 couldn't connect, 28 timed out, 56 the connection broke while
  receiving; 0 with a response; since 1.1.0). Without a
  response, a body request returns `{"error": "..."}`, and the error goes to `std::cerr` unless
  `quiet()`.
- **Query parameters:** `query(name, value)` or `query(fields)`, URL-encoded and added to the URL
  (after its own query, before a `#`). `url_encode()` and `form_encode()` are there too.
- **Headers:** `header(name, value)` replaces one of the same name, ignoring case; an empty value
  sends none, not even curl's own (POST's `application/x-www-form-urlencoded`). Requests send
  `Content-Type: application/json` unless told otherwise; `post_form()` sends its own for that
  request, and a multipart body's, with its boundary, is curl's. `bearer(token)` sets
  `Authorization`. `response_headers()` gives the last response's by lower-cased name.
- **Limits:** `timeout(seconds)` for the whole request (30; 0 none), `connect_timeout(seconds)` for
  connecting (10), `idle_timeout(seconds)` (since 1.1.0; none by default) when no data arrives for
  that long: a slow but steady answer goes on, a silent server doesn't (with `timeout(0)`, PHP's
  read timeout).
- **Compression** (since 1.1.0): requests ask for what curl can undo (gzip, deflate, ...) and get
  the content undone; `response_headers()` still says `content-encoding`. A request's own
  `Accept-Encoding` header replaces the ask.
- **Redirects:** not followed unless `follow_redirects(true, max = 5)`; then `effective_url()` is
  where they led. A POST answered with 301, 302 or 303 is followed with a GET, as browsers do.
- **Only http:// and https://**, for the URL and any redirect: a `file://` URL gets no response.
- **TLS:** the certificate and its host name are checked against the system's authorities.
  `trust(ca_file)` trusts only those signed by the given PEM (a self-signed server's own
  certificate pins it); `verify_host(false)` for a server reached by an address its certificate
  doesn't name; `pinned_public_key("sha256//...")`. `client_certificate(cert, key, passphrase)`:
  an empty key means the key is in the certificate's PEM too.
- `rest::fetch(url, base_url)` reads a local file of that path when there is one, else GETs it,
  and keeps the answer for the rest of the process.

`sc::ollama` (`ollama.h`) lists a server's models and asks one, with images when the model takes
them; see the header.

## Building and testing

```bash
cmake -S . -B build && cmake --build build -j
ctest --test-dir build -LE network   # rest, against a stand-in server; ollama wants a real one
```

`test-rest` starts `tests/resource/http_server.py` (python3 and openssl): HTTP, HTTPS with a
self-signed certificate, and HTTPS that wants a client certificate. It covers the methods,
query parameters, JSON, XML, form and multipart bodies, headers, binary bodies, status codes,
redirects, timeouts, trusted and refused certificates, host names, and client certificates with an
encrypted key.

## License

Apache-2.0; see LICENSE and NOTICE. libcurl is under the curl License.
