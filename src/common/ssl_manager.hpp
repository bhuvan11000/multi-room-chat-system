#include <openssl/ssl.h>
#include <openssl/err.h>
#include <boost/asio/ssl.hpp>
#include <iostream>
#include <string>

namespace chat {
class SSLManager {
public:
    enum Mode { SERVER, CLIENT };

    SSLManager(Mode mode) {
        const SSL_METHOD* method;
        if (mode == SERVER) {
            method = TLS_server_method();
        } else {
            method = TLS_client_method();
        }

        ctx = SSL_CTX_new(method);
        if (!ctx) {
            handle_error("Unable to create SSL context");
        }
    }

    void load_certificates(const std::string& cert_path, const std::string& key_path) {
        if (SSL_CTX_use_certificate_file(ctx, cert_path.c_str(), SSL_FILETYPE_PEM) <= 0) {
            handle_error("Failed to load certificate");
        }

        if (SSL_CTX_use_PrivateKey_file(ctx, key_path.c_str(), SSL_FILETYPE_PEM) <= 0) {
            handle_error("Failed to load private key");
        }
    }

    SSL* create_ssl_session(int socket_fd) {
        SSL* ssl = SSL_new(ctx);
        SSL_set_fd(ssl, socket_fd);
        return ssl;
    }

    ~SSLManager() {
        if (ctx) {
            SSL_CTX_free(ctx);
        }
    }

    static boost::asio::ssl::context create_boost_context(Mode mode,
                                                        const std::string& cert = "", 
                                                        const std::string& key = "") {
        boost::asio::ssl::context ctx(mode == SERVER ? 
            boost::asio::ssl::context::tlsv12_server : 
            boost::asio::ssl::context::tlsv12_client);

        if (mode == SERVER && !cert.empty() && !key.empty()) {
            ctx.use_certificate_chain_file(cert);
            ctx.use_private_key_file(key, boost::asio::ssl::context::pem);
        }
        
        return ctx;
    }

private:
    SSL_CTX* ctx;
    void handle_error(const char* msg) {
        perror(msg);
        ERR_print_errors_fp(stderr);
        exit(EXIT_FAILURE);
    }
};
}
