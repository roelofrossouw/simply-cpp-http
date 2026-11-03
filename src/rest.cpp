#include <rest.h>
#include <sc.h>
#include <curl/curl.h>

#include <filesystem>
#include <iostream>

namespace sc {
    // One response header line, "Name: value", into the map by its lowercased name. A status line
    // (a new response, after a redirect or a 100 Continue) starts the map again.
    static size_t header_data(char *contents, size_t size, size_t nmemb, void *userp) {
        auto &headers = *static_cast<std::map<std::string, std::string> *>(userp);
        std::string line(contents, size * nmemb);
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
        if (line.starts_with("HTTP/")) {
            headers.clear();
        } else if (const auto colon = line.find(':'); colon != std::string::npos) {
            auto value = line.substr(colon + 1);
            value.erase(0, value.find_first_not_of(" \t"));
            headers[lowercase(std::string_view(line).substr(0, colon))] = value;
        }
        return size * nmemb;
    }

    static size_t write_data(void *contents, size_t size, size_t nmemb, void *userp) {
        static_cast<std::string *>(userp)->append(static_cast<char *>(contents), size * nmemb);
        return size * nmemb;
    }

    std::map<std::string, std::string> rest::fetch_cache;

    rest::rest(const std::string &url) : url_(url) {
        header("Content-Type", "application/json");
    }

    namespace detail {
        class curl {
        public:
            curl() : curl_(curl_easy_init()) {
            }

            curl(const curl &) = delete;
            curl &operator=(const curl &) = delete;

            ~curl() {
                if (mime_) curl_mime_free(mime_);
                if (headers_) curl_slist_free_all(headers_);
                if (curl_) curl_easy_cleanup(curl_);
            }

            bool run() {
                if (!curl_) {
                    last_error_ = "No connection available";
                    return false;
                }
                char details[CURL_ERROR_SIZE] = {};
                option(CURLOPT_ERRORBUFFER, details);
                const CURLcode result = curl_easy_perform(curl_);
                last_error_ = *details ? details : curl_easy_strerror(result);
                option(CURLOPT_ERRORBUFFER, static_cast<char *>(nullptr));
                return result == CURLE_OK;
            }

            [[nodiscard]] const std::string &last_error() const { return last_error_; }

            // 0 when there was no response.
            [[nodiscard]] long status() const {
                long code = 0;
                if (curl_) curl_easy_getinfo(curl_, CURLINFO_RESPONSE_CODE, &code);
                return code;
            }

            [[nodiscard]] std::string effective_url() const {
                char *url = nullptr;
                if (curl_) curl_easy_getinfo(curl_, CURLINFO_EFFECTIVE_URL, &url);
                return url ? url : "";
            }

            [[nodiscard]] double total_time() const {
                double seconds = 0;
                if (curl_) curl_easy_getinfo(curl_, CURLINFO_TOTAL_TIME, &seconds);
                return seconds;
            }

            template<typename T>
            void option(const CURLoption option, const T &value) const {
                if (curl_) curl_easy_setopt(curl_, option, value);
            }

            void header(const std::string &line) { headers_ = curl_slist_append(headers_, line.c_str()); }

            void use_headers() const {
                if (headers_) option(CURLOPT_HTTPHEADER, headers_);
            }

            // A multipart body; false with last_error set when a file part can't be read.
            bool multipart(const std::vector<rest::part> &parts) {
                mime_ = curl_mime_init(curl_);
                for (const auto &each: parts) {
                    curl_mimepart *part = curl_mime_addpart(mime_);
                    curl_mime_name(part, each.name.c_str());
                    if (!each.file.empty()) {
                        if (!std::filesystem::is_regular_file(each.file)) {
                            last_error_ = "Can't read " + each.file + " for the part " + each.name;
                            return false;
                        }
                        if (curl_mime_filedata(part, each.file.c_str()) != CURLE_OK) {
                            last_error_ = "Can't read " + each.file + " for the part " + each.name;
                            return false;
                        }
                        if (!each.filename.empty()) curl_mime_filename(part, each.filename.c_str());
                        curl_mime_type(part, each.content_type.empty() ? "application/octet-stream" : each.content_type.c_str());
                    } else {
                        curl_mime_data(part, each.value.data(), each.value.size());
                        if (!each.content_type.empty()) curl_mime_type(part, each.content_type.c_str());
                    }
                }
                option(CURLOPT_MIMEPOST, mime_);
                return true;
            }

        private:
            CURL *curl_{nullptr};
            curl_slist *headers_{nullptr};
            curl_mime *mime_{nullptr};
            std::string last_error_{};
        };
    }

    std::string rest::url_encode(const std::string_view text) {
        static constexpr char hex[] = "0123456789ABCDEF";
        std::string out;
        out.reserve(text.size());
        for (const unsigned char c: text) {
            if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '.' || c == '_' || c == '~') {
                out += static_cast<char>(c);
            } else {
                out += '%';
                out += hex[c >> 4];
                out += hex[c & 15];
            }
        }
        return out;
    }

    std::string rest::form_encode(const fields &form) {
        std::string out;
        for (const auto &[name, value]: form) out += (out.empty() ? "" : "&") + url_encode(name) + "=" + url_encode(value);
        return out;
    }

    rest &rest::query(const std::string &name, const std::string &value) {
        query_ += (query_.empty() ? "" : "&") + url_encode(name) + "=" + url_encode(value);
        return *this;
    }

    rest &rest::query(const fields &parameters) {
        for (const auto &[name, value]: parameters) query(name, value);
        return *this;
    }

    std::string rest::perform(const std::string &method, const std::string *body, const std::vector<part> *parts) {
        detail::curl conn;
        std::string url = url_;
        if (!query_.empty()) {
            const auto hash = url.find('#');
            const auto before = hash == std::string::npos ? url.size() : hash;
            const bool has_query = url.find('?') < before;
            url.insert(before, (has_query ? "&" : "?") + query_);
        }
        conn.option(CURLOPT_URL, url.c_str());
        // Only the web: no file://, ftp:// or the like, even through a redirect.
#if LIBCURL_VERSION_NUM >= 0x075500
        conn.option(CURLOPT_PROTOCOLS_STR, "http,https");
        conn.option(CURLOPT_REDIR_PROTOCOLS_STR, "http,https");
#else
        conn.option(CURLOPT_PROTOCOLS, static_cast<long>(CURLPROTO_HTTP | CURLPROTO_HTTPS));
        conn.option(CURLOPT_REDIR_PROTOCOLS, static_cast<long>(CURLPROTO_HTTP | CURLPROTO_HTTPS));
#endif
        conn.option(CURLOPT_NOSIGNAL, 1L);
        conn.option(CURLOPT_CONNECTTIMEOUT, connect_timeout_secs_);
        conn.option(CURLOPT_TIMEOUT, timeout_secs_);
        for (const auto &[lower, entry]: headers_) {
            // A multipart body's Content-Type, with its boundary, is curl's own.
            if (parts && lower == "content-type") continue;
            conn.header(entry.value.empty() ? entry.name + ":" : entry.name + ": " + entry.value);
        }
        conn.use_headers();
        conn.option(CURLOPT_WRITEFUNCTION, write_data);
        response.clear();
        conn.option(CURLOPT_WRITEDATA, &response);
        response_headers_.clear();
        conn.option(CURLOPT_HEADERFUNCTION, header_data);
        conn.option(CURLOPT_HEADERDATA, &response_headers_);
        if (!certificate_file_.empty()) {
            conn.option(CURLOPT_SSLCERT, certificate_file_.c_str());
            conn.option(CURLOPT_SSLCERTTYPE, "PEM");
            if (!key_file_.empty()) conn.option(CURLOPT_SSLKEY, key_file_.c_str());
            if (!passphrase_.empty()) conn.option(CURLOPT_KEYPASSWD, passphrase_.c_str());
        }
        if (!ca_file_.empty()) conn.option(CURLOPT_CAINFO, ca_file_.c_str());
        if (!verify_host_) conn.option(CURLOPT_SSL_VERIFYHOST, 0L);
        if (!pins_.empty()) conn.option(CURLOPT_PINNEDPUBLICKEY, pins_.c_str());
        if (follow_) {
            conn.option(CURLOPT_FOLLOWLOCATION, 1L);
            conn.option(CURLOPT_MAXREDIRS, max_redirects_);
        }

        bool ready = true;
        if (parts) {
            ready = conn.multipart(*parts);
        } else if (body) {
            // With the size given, bodies with zero bytes go out whole.
            conn.option(CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(body->size()));
            conn.option(CURLOPT_POSTFIELDS, body->data());
        }
        if (method == "HEAD") conn.option(CURLOPT_NOBODY, 1L);
        else if (method != "GET" && method != "POST") conn.option(CURLOPT_CUSTOMREQUEST, method.c_str());

        const bool ran = ready && conn.run();
        status_ = ran ? conn.status() : 0;
        error_ = ran ? std::string{} : conn.last_error();
        effective_url_ = conn.effective_url();
        total_time_ = conn.total_time();
        if (!ran) {
            if (!quiet_) std::cerr << "Request failed: " << error_ << std::endl;
            response = body || parts ? R"({"error": ")" + error_ + R"("})" : "";
        }
        return response;
    }

    std::string rest::get() { return perform("GET", nullptr); }

    std::string rest::head() { return perform("HEAD", nullptr); }

    std::string rest::post(const std::string &jsonData) { return perform("POST", &jsonData); }

    std::string rest::put(const std::string &jsonData) { return perform("PUT", &jsonData); }

    std::string rest::patch(const std::string &body) { return perform("PATCH", &body); }

    std::string rest::del() { return perform("DELETE", nullptr); }

    std::string rest::post_form(const fields &form) {
        const auto body = form_encode(form);
        const auto kept = headers_;
        header("Content-Type", "application/x-www-form-urlencoded");
        auto answer = perform("POST", &body);
        headers_ = kept;
        return answer;
    }

    std::string rest::post_multipart(const std::vector<part> &parts) { return perform("POST", nullptr, &parts); }

    void rest::header(const std::string &key, const std::string &value) { headers_[lowercase(key)] = {key, value}; }

    void rest::bearer(const std::string &token) { header("Authorization", "Bearer " + token); }

    rest &rest::timeout(const long timeout) {
        timeout_secs_ = timeout;
        return *this;
    }

    rest &rest::connect_timeout(const long timeout) {
        connect_timeout_secs_ = timeout;
        return *this;
    }

    rest &rest::client_certificate(const std::string &certificate_file, const std::string &key_file, const std::string &passphrase) {
        certificate_file_ = certificate_file;
        key_file_ = key_file;
        passphrase_ = passphrase;
        return *this;
    }

    rest &rest::trust(const std::string &ca_file) {
        ca_file_ = ca_file;
        return *this;
    }

    rest &rest::verify_host(const bool on) {
        verify_host_ = on;
        return *this;
    }

    rest &rest::pinned_public_key(const std::string &pins) {
        pins_ = pins;
        return *this;
    }

    rest &rest::follow_redirects(const bool on, const long max) {
        follow_ = on;
        max_redirects_ = max;
        return *this;
    }

    rest &rest::quiet(const bool on) {
        quiet_ = on;
        return *this;
    }

    std::string rest::fetch(const std::string &url, const std::string &base_url) {
        if (url.empty() || url.size() > 100) return "";
        if (const auto cached = fetch_cache.find(url); cached != fetch_cache.end()) return cached->second;

        std::string content;
        if (std::filesystem::exists(url)) content = file_get_contents(url);
        if (content.empty() && std::filesystem::exists(url.substr(1))) content = file_get_contents(url.substr(1));
        if (content.empty() && url.starts_with(base_url)) {
            const auto local_path = url.substr(base_url.size());
            if (std::filesystem::exists(local_path)) content = file_get_contents(local_path);
        }
        if (content.empty()) content = rest(url).get();

        return fetch_cache[url] = content;
    }
}
