#ifndef REST_H
#define REST_H

#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sc {
    /// HTTP to one URL over libcurl: GET, HEAD, POST, PUT, PATCH and DELETE, with query parameters,
    /// headers, a bearer token, JSON, form and multipart bodies, timeouts, client certificates and
    /// redirects. Only http:// and https:// URLs (redirects too); TLS certificates and host names
    /// are checked unless told otherwise. Requests send "Content-Type: application/json" unless
    /// header() says otherwise (an empty value sends none).
    ///
    ///     sc::rest api{"https://example.com/api/items"};
    ///     api.bearer(token);
    ///     const auto body = api.timeout(5).post(R"({"name": "x"})");
    ///     if (api.status() != 200) ...
    ///
    ///     sc::rest search{"https://example.com/find"};
    ///     search.query("q", "a b").query("page", "2");               // ?q=a%20b&page=2
    ///     const auto page = search.follow_redirects().get();
    ///     sc::rest upload{"https://example.com/upload"};
    ///     upload.post_multipart({{.name = "note", .value = "hi"}, {.name = "file", .file = "/tmp/a.pdf"}});
    class rest {
    public:
        explicit rest(const std::string &url);

        /// name=value pairs, in order (a name may repeat).
        using fields = std::vector<std::pair<std::string, std::string>>;

        /// One part of a multipart/form-data body: a value, or a file's contents (file: its path)
        /// sent as filename (the file's own name when empty), each with its own Content-Type when
        /// given (a file's otherwise application/octet-stream).
        struct part {
            std::string name;
            std::string value;
            std::string file;
            std::string filename;
            std::string content_type;
        };

        /// A query parameter, URL-encoded and added to the URL (after its own, with ? or &).
        rest &query(const std::string &name, const std::string &value);
        rest &query(const fields &parameters);

        /// The response body, whatever the status (see status()). When no response came, the
        /// error goes to std::cerr (unless quiet()) and the body is empty.
        std::string get();
        /// A HEAD: status() and response_headers(), no body.
        std::string head();

        /// Posts body (any bytes, zeros included) and returns the response body, whatever the
        /// status. When no response came, the error goes to std::cerr (unless quiet()) and the
        /// body is {"error": "<curl's message>"}.
        std::string post(const std::string &jsonData = R"({})");
        /// A PUT or PATCH of body, as post() sends it; the response body, whatever the status.
        std::string put(const std::string &jsonData = R"({})");
        std::string patch(const std::string &body = R"({})");
        /// A DELETE; the response body, whatever the status, as with get().
        std::string del();
        /// A POST of fields as application/x-www-form-urlencoded (form_encode()).
        std::string post_form(const fields &form);
        /// A POST of parts as multipart/form-data (its Content-Type, with the boundary, is curl's).
        /// A file that can't be read gives no response: error() says why.
        std::string post_multipart(const std::vector<part> &parts);

        /// Sets a request header, replacing one of the same name (ignoring case); an empty value
        /// sends none (nor curl's own, such as POST's Content-Type).
        void header(const std::string &key, const std::string &value);

        /// "Authorization: Bearer <token>".
        void bearer(const std::string &token);

        /// The HTTP status of the last request (200, 404, ...), 0 when it got no response.
        [[nodiscard]] long status() const { return status_; }
        /// The last response's headers by lowercased name ("content-type", "location"); empty when
        /// none came. After redirects, the last response's.
        [[nodiscard]] const std::map<std::string, std::string> &response_headers() const { return response_headers_; }
        /// The URL the last response came from: after redirects, where they led; with the query.
        [[nodiscard]] const std::string &effective_url() const { return effective_url_; }
        /// How long the last request took, in seconds, redirects included.
        [[nodiscard]] double total_time() const { return total_time_; }

        /// The whole request's limit in seconds (default 30; 0: none); connect_timeout() only
        /// connecting's (default 10).
        rest &timeout(long timeout);
        rest &connect_timeout(long timeout);
        /// Gives up when no data arrives for this many seconds (less than a byte a second over that
        /// time), however long the whole request may take: a slow but steady answer goes on, a
        /// silent server doesn't. 0 (the default): no such limit. With timeout(0), as PHP's
        /// file_get_contents' read timeout.
        rest &idle_timeout(long seconds);
        /// A TLS client certificate (PEM) and its key, for a server that asks for one (LXD). An
        /// empty key_file: the key is in certificate_file too (as PHP's local_cert). passphrase:
        /// the key's, when it's encrypted.
        rest &client_certificate(const std::string &certificate_file, const std::string &key_file = {}, const std::string &passphrase = {});
        /// Trusts the server's certificate only when it's signed by (or is) one in this PEM file,
        /// rather than the system's authorities: a self-signed server's own certificate pins it.
        rest &trust(const std::string &ca_file);
        /// Whether the certificate must name the host connected to (default true). A server reached
        /// by an address its certificate doesn't list (a cluster member by IP) needs false; the
        /// certificate is still checked against trust().
        rest &verify_host(bool on);
        /// Accepts only a server whose public key is this, curl's "sha256//<base64>" (several
        /// joined by ';'), whatever else is trusted.
        rest &pinned_public_key(const std::string &pins);
        /// Follows redirects (Location), at most max of them, to http:// and https:// URLs only;
        /// off by default. A POST answered by a 301, 302 or 303 is followed with a GET, as browsers do.
        rest &follow_redirects(bool on = true, long max = 5);
        /// Keeps a failed request's error off std::cerr; error() still has it.
        rest &quiet(bool on = true);
        /// Why the last request got no response ("Couldn't connect to server"); empty when one came.
        [[nodiscard]] const std::string &error() const { return error_; }
        /// The same as curl's error number (CURLcode: 7 couldn't connect, 28 timed out, 56 the
        /// connection broke while receiving ...); 0 when a response came. Curl's numbers, kept as they
        /// are whatever does the transfer.
        [[nodiscard]] long error_code() const { return error_code_; }

        /// text with every byte but A-Z a-z 0-9 - . _ ~ as %XX (RFC 3986).
        [[nodiscard]] static std::string url_encode(std::string_view text);
        /// name=value&... with both URL-encoded, as application/x-www-form-urlencoded sends them.
        [[nodiscard]] static std::string form_encode(const fields &form);

        /// The content at url, kept for the rest of the process: a local file of that path (or of it
        /// without its first character, or without base_url) if there is one, else a GET. Empty for a
        /// url that is empty or over 100 characters. Not thread-safe: the cache is shared and unguarded.
        static std::string fetch(const std::string &url, const std::string &base_url = "");

    private:
        struct header_entry {
            std::string name, value;
        };

        std::string url_;
        std::string query_;
        long connect_timeout_secs_ = 10;
        long timeout_secs_ = 30;
        long idle_secs_ = 0;
        long status_ = 0;
        std::string error_;
        long error_code_ = 0;
        std::string effective_url_;
        double total_time_ = 0;
        std::string certificate_file_, key_file_, passphrase_, ca_file_, pins_;
        bool verify_host_ = true;
        bool quiet_ = false;
        bool follow_ = false;
        long max_redirects_ = 5;
        std::map<std::string, header_entry> headers_; // by lowercased name
        std::string response;
        std::map<std::string, std::string> response_headers_;

        // method ("GET", "HEAD", "POST", ...) with body when there is one, or the parts of a
        // multipart body; the response body.
        std::string perform(const std::string &method, const std::string *body, const std::vector<part> *parts = nullptr);
    };
}

#endif //REST_H
