// sc::rest against a stand-in web server (resource/http_server.py: HTTP, HTTPS, and HTTPS that wants
// a client certificate), and fetch() on local files.
#include <rest.h>
#include <sc.h>

#include "sc_test.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <random>
#include <string>
#include <thread>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {
    std::string decoded(const json &echo) {
        static const std::string letters = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        const auto text = echo.at("body").get<std::string>();
        std::string out;
        int bits = 0, value = 0;
        for (const char c: text) {
            if (c == '=') break;
            value = value << 6 | static_cast<int>(letters.find(c));
            bits += 6;
            if (bits >= 8) {
                bits -= 8;
                out += static_cast<char>(value >> bits & 0xff);
            }
        }
        return out;
    }
}

int main() {
    const auto dir = fs::temp_directory_path() / ("sc-http-test-" + std::to_string(std::random_device{}()));
    fs::create_directories(dir);
    const auto at = [&](const std::string &name) { return (dir / name).string(); };

    SECTION("URL and form encoding");
    {
        CHECK_EQ(sc::rest::url_encode("a b&c=d/é~._-"), std::string{"a%20b%26c%3Dd%2F%C3%A9~._-"});
        CHECK_EQ(sc::rest::form_encode({{"name", "Jo Soap"}, {"tag", "a+b"}, {"tag", ""}}), std::string{"name=Jo%20Soap&tag=a%2Bb&tag="});
        CHECK_EQ(sc::rest::form_encode({}), std::string{});
    }

    SECTION("fetch reads a local file before trying the network, and keeps it");
    {
        const auto path = at("payload.txt");
        sc::file_put_contents(path, "hello from disk");
        CHECK_EQ(sc::rest::fetch(path), std::string{"hello from disk"});
        sc::file_put_contents(path, "changed");
        CHECK_EQ(sc::rest::fetch(path), std::string{"hello from disk"});
        const std::string base = "https://example.com/";
        // Relative, so the URL stays under fetch()'s 100 characters.
        const std::string based = "sc-http-based.txt";
        sc::file_put_contents(based, "resolved locally");
        CHECK_EQ(sc::rest::fetch(base + based, base), std::string{"resolved locally"});
        fs::remove(based);
        CHECK_EQ(sc::rest::fetch("https://example.com/" + std::string(100, 'a')), std::string{});
        CHECK_EQ(sc::rest::fetch(""), std::string{});
    }

    SECTION("No response: reported, not thrown; only http and https");
    {
        sc::rest nowhere{"http://127.0.0.1:1/x"};
        nowhere.quiet().connect_timeout(2);
        CHECK_EQ(nowhere.get(), std::string{});
        CHECK_EQ(nowhere.status(), 0L);
        CHECK(!nowhere.error().empty());
        CHECK(nowhere.post("{}").find("\"error\"") != std::string::npos);
        // Nothing but http and https: no file read, no ftp, gopher or dict.
        for (const std::string url: {"file:///etc/hosts", "file:///etc/hostname", "ftp://127.0.0.1/x", "gopher://127.0.0.1/x", "dict://127.0.0.1/x"}) {
            sc::rest other{url};
            CHECK_EQ(other.quiet().get(), std::string{});
            CHECK_EQ(other.status(), 0L);
            CHECK_MSG(other.error().find("rotocol") != std::string::npos, url + ": " + other.error());
        }
    }

    if (std::system("openssl version > /dev/null 2>&1") != 0 || std::system("python3 --version > /dev/null 2>&1") != 0) {
        std::cout << "   (no openssl or python3: the server tests didn't run)" << std::endl;
        fs::remove_all(dir);
        TEST_SUMMARY();
    }

    // A server certificate for 127.0.0.1; a client's, its key encrypted (passphrase "secret"), and
    // the two in one PEM; a stranger the server doesn't know. Curves named and extensions spelt out
    // for macOS's LibreSSL (see sc-lxd's tests).
    const auto certificate = [&](const std::string &name, const std::string &key_option) {
        return std::system(("openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:P-256 -pkeyopt ec_param_enc:named_curve " + key_option +
                            " -days 1 -subj /CN=sc-http-" + name +
                            " -addext basicConstraints=critical,CA:TRUE -addext subjectKeyIdentifier=hash -addext authorityKeyIdentifier=keyid:always"
                            " -addext keyUsage=critical,digitalSignature,keyCertSign -addext subjectAltName=IP:127.0.0.1"
                            " -keyout " + at(name + ".key") + " -out " + at(name + ".crt") + " > /dev/null 2>&1").c_str());
    };
    CHECK_EQ(certificate("server", "-nodes"), 0);
    CHECK_EQ(certificate("client", "-passout pass:secret"), 0);
    CHECK_EQ(certificate("stranger", "-nodes"), 0);
    CHECK_EQ(std::system(("cat " + at("client.crt") + " " + at("client.key") + " > " + at("client.pem")).c_str()), 0);

    CHECK_EQ(std::system(("python3 " SC_HTTP_TEST_RESOURCE "/http_server.py " + at("server.crt") + " " + at("server.key") + " " + at("client.crt") +
                          " > " + at("server.log") + " 2>&1 & echo $! > " + at("server.pid")).c_str()),
             0);
    int http_port = 0, https_port = 0, mtls_port = 0;
    for (int tries = 0; tries < 100 && http_port == 0; ++tries) {
        std::ifstream log{at("server.log")};
        std::string word;
        if (log >> word && word == "listening") log >> http_port >> https_port >> mtls_port;
        if (http_port == 0) std::this_thread::sleep_for(std::chrono::milliseconds{100});
    }
    CHECK(http_port != 0);
    const auto http = "http://127.0.0.1:" + std::to_string(http_port);
    const auto https = "https://127.0.0.1:" + std::to_string(https_port);
    const auto mtls = "https://127.0.0.1:" + std::to_string(mtls_port);

    SECTION("Methods, bodies and their Content-Type");
    {
        for (const std::string method: {"GET", "POST", "PUT", "PATCH", "DELETE"}) {
            sc::rest request{http + "/echo"};
            const auto body = method == "GET" ? request.get() : method == "POST" ? request.post(R"({"a":1})") : method == "PUT" ? request.put(R"({"a":1})")
                                                            : method == "PATCH" ? request.patch(R"({"a":1})") : request.del();
            const auto echo = json::parse(body);
            CHECK_EQ(echo.at("method").get<std::string>(), method);
            if (method != "GET" && method != "DELETE") {
                CHECK_EQ(decoded(echo), std::string{R"({"a":1})"});
                CHECK_EQ(echo.at("content_type").get<std::string>(), std::string{"application/json"});
            }
        }
        sc::rest head{http + "/binary"};
        CHECK_EQ(head.head(), std::string{});
        CHECK_EQ(head.status(), 200L);
        CHECK_EQ(head.response_headers().at("x-test"), std::string{"yes"});

        sc::rest xml{http + "/echo"};
        xml.header("content-type", "text/xml; charset=utf-8"); // replaces the default, whatever its case
        xml.header("SOAPAction", "\"urn:Ping\"");
        const auto soap = json::parse(xml.post("<Envelope/>"));
        CHECK_EQ(soap.at("content_type").get<std::string>(), std::string{"text/xml; charset=utf-8"});
        CHECK_EQ(soap.at("headers").at("soapaction").get<std::string>(), std::string{"\"urn:Ping\""});
        CHECK_EQ(decoded(soap), std::string{"<Envelope/>"});

        sc::rest bare{http + "/echo"};
        bare.header("Content-Type", ""); // none at all, not even curl's own
        const auto none = json::parse(bare.post("raw"));
        CHECK(none.at("content_type").is_null());

        sc::rest zeros{http + "/echo"};
        const std::string binary{"a\0b\0", 4};
        CHECK_EQ(decoded(json::parse(zeros.post(binary))), binary);
    }

    SECTION("Query parameters, form and multipart bodies");
    {
        sc::rest search{http + "/echo?fixed=1"};
        search.query("q", "a b&c").query({{"page", "2"}});
        CHECK_EQ(json::parse(search.get()).at("query").get<std::string>(), std::string{"fixed=1&q=a%20b%26c&page=2"});

        sc::rest form{http + "/echo"};
        const auto posted = json::parse(form.post_form({{"name", "Jo Soap"}, {"x", "1&2"}}));
        CHECK_EQ(posted.at("content_type").get<std::string>(), std::string{"application/x-www-form-urlencoded"});
        CHECK_EQ(decoded(posted), std::string{"name=Jo%20Soap&x=1%262"});
        // The form's Content-Type was for that request only.
        CHECK_EQ(json::parse(form.post("{}")).at("content_type").get<std::string>(), std::string{"application/json"});

        sc::file_put_contents(at("upload.txt"), "file contents");
        sc::rest upload{http + "/echo"};
        const auto multipart = json::parse(upload.post_multipart({{.name = "note", .value = "hi there"},
                                                                  {.name = "doc", .file = at("upload.txt"), .filename = "report.txt", .content_type = "text/plain"}}));
        const auto type = multipart.at("content_type").get<std::string>();
        CHECK(type.starts_with("multipart/form-data; boundary="));
        const auto sent = decoded(multipart);
        CHECK(sent.find("name=\"note\"") != std::string::npos && sent.find("hi there") != std::string::npos);
        CHECK(sent.find("filename=\"report.txt\"") != std::string::npos && sent.find("file contents") != std::string::npos);

        sc::rest missing{http + "/echo"};
        (void) missing.quiet().post_multipart({{.name = "doc", .file = at("no-such-file")}});
        CHECK_EQ(missing.status(), 0L);
        CHECK(missing.error().find("no-such-file") != std::string::npos);
    }

    SECTION("Status, headers, binary bodies, effective URL and time");
    {
        sc::rest gone{http + "/status/404"};
        CHECK_EQ(gone.get(), std::string{"status 404"});
        CHECK_EQ(gone.status(), 404L);
        CHECK(gone.error().empty());
        sc::rest binary{http + "/binary"};
        const auto bytes = binary.get();
        CHECK_EQ(bytes.size(), std::size_t{256});
        CHECK_EQ(static_cast<unsigned char>(bytes[255]), 255);
        CHECK_EQ(binary.response_headers().at("content-type"), std::string{"application/octet-stream"});
        CHECK(binary.total_time() > 0);
        CHECK_EQ(binary.effective_url(), http + "/binary");
    }

    SECTION("Redirects: not followed unless asked, at most max");
    {
        sc::rest plain{http + "/redirect/2"};
        (void) plain.get();
        CHECK_EQ(plain.status(), 302L);
        sc::rest followed{http + "/redirect/2"};
        CHECK_EQ(json::parse(followed.follow_redirects().get()).at("path").get<std::string>(), std::string{"/echo"});
        CHECK_EQ(followed.status(), 200L);
        CHECK_EQ(followed.effective_url(), http + "/echo");
        // A redirect to file:// is refused, not followed.
        sc::rest to_file{http + "/to-file"};
        const auto refused = to_file.quiet().follow_redirects().get();
        CHECK_EQ(to_file.status(), 0L);
        CHECK(refused.find("localhost") == std::string::npos); // /etc/hosts wasn't read
        CHECK_MSG(to_file.error().find("rotocol") != std::string::npos, to_file.error());
        sc::rest too_many{http + "/redirect/4"};
        (void) too_many.quiet().follow_redirects(true, 2).get();
        CHECK_EQ(too_many.status(), 0L);
        CHECK(!too_many.error().empty());
    }

    SECTION("Timeouts");
    {
        sc::rest slow{http + "/slow"};
        const auto started = std::chrono::steady_clock::now();
        (void) slow.quiet().timeout(1).get();
        CHECK_EQ(slow.status(), 0L);
        CHECK(std::chrono::steady_clock::now() - started < std::chrono::seconds{3});
    }

    SECTION("TLS: a self-signed server refused unless trusted; host names; client certificates");
    {
        sc::rest unknown{https + "/echo"};
        (void) unknown.quiet().get();
        CHECK_EQ(unknown.status(), 0L); // the system's authorities don't know it
        CHECK(!unknown.error().empty());
        sc::rest trusted{https + "/echo"};
        CHECK_EQ(json::parse(trusted.trust(at("server.crt")).get()).at("method").get<std::string>(), std::string{"GET"});
        sc::rest by_name{"https://localhost:" + std::to_string(https_port) + "/echo"};
        (void) by_name.quiet().trust(at("server.crt")).get();
        CHECK_EQ(by_name.status(), 0L); // its certificate names 127.0.0.1, not localhost
        sc::rest unchecked{"https://localhost:" + std::to_string(https_port) + "/echo"};
        (void) unchecked.quiet().trust(at("server.crt")).verify_host(false).get();
        CHECK_EQ(unchecked.status(), 200L);

        sc::rest anonymous{mtls + "/echo"};
        (void) anonymous.quiet().trust(at("server.crt")).get();
        CHECK_EQ(anonymous.status(), 0L); // no client certificate
        sc::rest client{mtls + "/echo"};
        (void) client.quiet().trust(at("server.crt")).client_certificate(at("client.crt"), at("client.key"), "secret").get();
        CHECK_EQ(client.status(), 200L);
        sc::rest one_pem{mtls + "/echo"};
        (void) one_pem.quiet().trust(at("server.crt")).client_certificate(at("client.pem"), {}, "secret").get();
        CHECK_EQ(one_pem.status(), 200L);
        sc::rest wrong_passphrase{mtls + "/echo"};
        (void) wrong_passphrase.quiet().trust(at("server.crt")).client_certificate(at("client.pem"), {}, "wrong").get();
        CHECK_EQ(wrong_passphrase.status(), 0L);
        sc::rest stranger{mtls + "/echo"};
        (void) stranger.quiet().trust(at("server.crt")).client_certificate(at("stranger.crt"), at("stranger.key")).get();
        CHECK_EQ(stranger.status(), 0L);
    }

    (void) std::system(("kill $(cat " + at("server.pid") + ") > /dev/null 2>&1").c_str());
    fs::remove_all(dir);
    TEST_SUMMARY();
}
