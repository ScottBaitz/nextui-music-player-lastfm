#define _GNU_SOURCE
#include "radio_net.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>

#include "defines.h"
#include "api.h"

// mbedTLS for HTTPS support
#include "mbedtls/net_sockets.h"
#include "mbedtls/ssl.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/error.h"

// SSL context for fetch operations (heap allocated to save stack space)
typedef struct {
    mbedtls_net_context net;
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config conf;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    bool initialized;
} FetchSSLContext;

// Parse URL into host, port, path, and detect HTTPS
int radio_net_parse_url(const char* url, char* host, int host_size,
                        int* port, char* path, int path_size, bool* is_https) {
    if (!url || !host || !path || host_size < 1 || path_size < 1) {
        return -1;
    }

    *is_https = false;
    *port = 80;
    host[0] = '\0';
    path[0] = '\0';

    // Skip protocol
    const char* start = url;
    if (strncmp(url, "https://", 8) == 0) {
        start = url + 8;
        *is_https = true;
        *port = 443;
    } else if (strncmp(url, "http://", 7) == 0) {
        start = url + 7;
    }

    // Find path
    const char* path_start = strchr(start, '/');
    if (path_start) {
        snprintf(path, path_size, "%s", path_start);
    } else {
        strcpy(path, "/");
        path_start = start + strlen(start);
    }

    // Find port
    const char* port_start = strchr(start, ':');
    if (port_start && port_start < path_start) {
        *port = atoi(port_start + 1);
        int host_len = port_start - start;
        if (host_len >= host_size) host_len = host_size - 1;
        memcpy(host, start, host_len);
        host[host_len] = '\0';
    } else {
        int host_len = path_start - start;
        if (host_len >= host_size) host_len = host_size - 1;
        memcpy(host, start, host_len);
        host[host_len] = '\0';
    }

    return 0;
}

// Fetch content from URL into buffer
// Returns bytes read, or -1 on error
int radio_net_fetch(const char* url, uint8_t* buffer, int buffer_size,
                    char* content_type, int ct_size) {
    if (!url || !buffer || buffer_size <= 0) {
        LOG_error("[RadioNet] Invalid parameters\n");
        return -1;
    }

    // Use heap for URL components to reduce stack usage
    char* host = (char*)malloc(256);
    char* path = (char*)malloc(512);
    if (!host || !path) {
        LOG_error("[RadioNet] Failed to allocate host/path buffers\n");
        free(host);
        free(path);
        return -1;
    }

    int port;
    bool is_https;

    if (radio_net_parse_url(url, host, 256, &port, path, 512, &is_https) != 0) {
        LOG_error("[RadioNet] Failed to parse URL: %s\n", url);
        free(host);
        free(path);
        return -1;
    }

    int sock_fd = -1;
    FetchSSLContext* ssl_ctx = NULL;
    char* header_buf = NULL;

    if (is_https) {
        // Allocate SSL context on heap to avoid stack overflow
        ssl_ctx = (FetchSSLContext*)calloc(1, sizeof(FetchSSLContext));
        if (!ssl_ctx) {
            LOG_error("[RadioNet] Failed to allocate SSL context\n");
            free(host);
            free(path);
            return -1;
        }

        const char* pers = "radio_net_fetch";
        mbedtls_net_init(&ssl_ctx->net);
        mbedtls_ssl_init(&ssl_ctx->ssl);
        mbedtls_ssl_config_init(&ssl_ctx->conf);
        mbedtls_entropy_init(&ssl_ctx->entropy);
        mbedtls_ctr_drbg_init(&ssl_ctx->ctr_drbg);

        if (mbedtls_ctr_drbg_seed(&ssl_ctx->ctr_drbg, mbedtls_entropy_func, &ssl_ctx->entropy,
                                   (const unsigned char*)pers, strlen(pers)) != 0) {
            goto cleanup;
        }

        if (mbedtls_ssl_config_defaults(&ssl_ctx->conf, MBEDTLS_SSL_IS_CLIENT,
                                         MBEDTLS_SSL_TRANSPORT_STREAM,
                                         MBEDTLS_SSL_PRESET_DEFAULT) != 0) {
            goto cleanup;
        }

        mbedtls_ssl_conf_authmode(&ssl_ctx->conf, MBEDTLS_SSL_VERIFY_NONE);
        mbedtls_ssl_conf_rng(&ssl_ctx->conf, mbedtls_ctr_drbg_random, &ssl_ctx->ctr_drbg);

        if (mbedtls_ssl_setup(&ssl_ctx->ssl, &ssl_ctx->conf) != 0) {
            goto cleanup;
        }

        mbedtls_ssl_set_hostname(&ssl_ctx->ssl, host);

        char port_str[16];
        snprintf(port_str, sizeof(port_str), "%d", port);

        if (mbedtls_net_connect(&ssl_ctx->net, host, port_str, MBEDTLS_NET_PROTO_TCP) != 0) {
            goto cleanup;
        }

        mbedtls_ssl_set_bio(&ssl_ctx->ssl, &ssl_ctx->net, mbedtls_net_send, mbedtls_net_recv, NULL);

        int ret;
        while ((ret = mbedtls_ssl_handshake(&ssl_ctx->ssl)) != 0) {
            if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
                goto cleanup;
            }
        }

        ssl_ctx->initialized = true;
        sock_fd = ssl_ctx->net.fd;
    } else {
        // Use getaddrinfo instead of gethostbyname (thread-safe)
        struct addrinfo hints, *result = NULL;
        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;

        char port_str[16];
        snprintf(port_str, sizeof(port_str), "%d", port);

        int gai_ret = getaddrinfo(host, port_str, &hints, &result);
        if (gai_ret != 0 || !result) {
            LOG_error("[RadioNet] getaddrinfo failed for host: %s (error: %d)\n", host, gai_ret);
            if (result) freeaddrinfo(result);
            free(host);
            free(path);
            return -1;
        }

        sock_fd = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
        if (sock_fd < 0) {
            LOG_error("[RadioNet] socket() failed\n");
            freeaddrinfo(result);
            free(host);
            free(path);
            return -1;
        }

        struct timeval tv = {10, 0};
        setsockopt(sock_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(sock_fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

        if (connect(sock_fd, result->ai_addr, result->ai_addrlen) < 0) {
            close(sock_fd);
            freeaddrinfo(result);
            free(host);
            free(path);
            return -1;
        }
        freeaddrinfo(result);
    }

    // Send HTTP request (use HTTP/1.1 with proper headers for CDN compatibility)
    char request[512];
    snprintf(request, sizeof(request),
        "GET %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "User-Agent: Mozilla/5.0 (Linux) AppleWebKit/537.36\r\n"
        "Accept: */*\r\n"
        "Accept-Encoding: identity\r\n"
        "Connection: close\r\n"
        "\r\n",
        path, host);

    int sent;
    if (is_https) {
        sent = mbedtls_ssl_write(&ssl_ctx->ssl, (unsigned char*)request, strlen(request));
    } else {
        sent = send(sock_fd, request, strlen(request), 0);
    }

    if (sent < 0) {
        goto cleanup;
    }

    // Read response - allocate header buffer on heap to reduce stack pressure
    #define HEADER_BUF_SIZE 2048
    header_buf = (char*)malloc(HEADER_BUF_SIZE);
    if (!header_buf) {
        LOG_error("[RadioNet] Failed to allocate header buffer\n");
        goto cleanup;
    }
    int header_pos = 0;
    bool headers_done = false;

    // Read headers
    while (header_pos < HEADER_BUF_SIZE - 1) {
        char c;
        int r;
        if (is_https) {
            r = mbedtls_ssl_read(&ssl_ctx->ssl, (unsigned char*)&c, 1);
        } else {
            r = recv(sock_fd, &c, 1, 0);
        }
        if (r != 1) break;

        header_buf[header_pos++] = c;
        if (header_pos >= 4 &&
            header_buf[header_pos-4] == '\r' && header_buf[header_pos-3] == '\n' &&
            header_buf[header_pos-2] == '\r' && header_buf[header_pos-1] == '\n') {
            headers_done = true;
            break;
        }
    }
    header_buf[header_pos] = '\0';

    if (!headers_done) {
        goto cleanup;
    }

    // Find end of first line (status line) for redirect detection
    char* first_line_end = strstr(header_buf, "\r\n");

    // Check for redirect - only check the status line (first line), not entire headers
    bool is_redirect = false;
    if (first_line_end) {
        char* status_start = strstr(header_buf, "HTTP/");
        if (status_start && status_start < first_line_end) {
            is_redirect = (strstr(status_start, " 301 ") && strstr(status_start, " 301 ") < first_line_end) ||
                         (strstr(status_start, " 302 ") && strstr(status_start, " 302 ") < first_line_end) ||
                         (strstr(status_start, " 303 ") && strstr(status_start, " 303 ") < first_line_end) ||
                         (strstr(status_start, " 307 ") && strstr(status_start, " 307 ") < first_line_end) ||
                         (strstr(status_start, " 308 ") && strstr(status_start, " 308 ") < first_line_end);
        }
    }
    if (is_redirect) {
        char* loc = strcasestr(header_buf, "\nLocation:");
        if (loc) {
            loc += 10;
            while (*loc == ' ') loc++;
            char* end = loc;
            while (*end && *end != '\r' && *end != '\n') end++;

            // Copy redirect URL before cleanup
            char redirect_url[1024];
            int rlen = end - loc;
            if (rlen >= (int)sizeof(redirect_url)) rlen = sizeof(redirect_url) - 1;
            strncpy(redirect_url, loc, rlen);
            redirect_url[rlen] = '\0';

            // Cleanup current connection and buffers before recursive call
            if (ssl_ctx) {
                mbedtls_ssl_close_notify(&ssl_ctx->ssl);
                mbedtls_net_free(&ssl_ctx->net);
                mbedtls_ssl_free(&ssl_ctx->ssl);
                mbedtls_ssl_config_free(&ssl_ctx->conf);
                mbedtls_ctr_drbg_free(&ssl_ctx->ctr_drbg);
                mbedtls_entropy_free(&ssl_ctx->entropy);
                free(ssl_ctx);
            } else {
                close(sock_fd);
            }
            free(header_buf);
            free(host);
            free(path);

            // Follow redirect
            return radio_net_fetch(redirect_url, buffer, buffer_size, content_type, ct_size);
        }
        goto cleanup;
    }

    // Extract content type if requested
    if (content_type && ct_size > 0) {
        content_type[0] = '\0';
        char* ct = strcasestr(header_buf, "\nContent-Type:");
        if (ct) {
            ct += 14;
            while (*ct == ' ') ct++;
            char* end = ct;
            while (*end && *end != '\r' && *end != '\n' && *end != ';') end++;
            int len = end - ct;
            if (len < ct_size) {
                strncpy(content_type, ct, len);
                content_type[len] = '\0';
            }
        }
    }

    // Read body
    int total_read = 0;
    while (total_read < buffer_size - 1) {
        int r;
        if (is_https) {
            r = mbedtls_ssl_read(&ssl_ctx->ssl, buffer + total_read, buffer_size - total_read - 1);
            if (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
        } else {
            r = recv(sock_fd, buffer + total_read, buffer_size - total_read - 1, 0);
        }
        if (r <= 0) break;
        total_read += r;
    }

    // Cleanup
    if (ssl_ctx) {
        mbedtls_ssl_close_notify(&ssl_ctx->ssl);
        mbedtls_net_free(&ssl_ctx->net);
        mbedtls_ssl_free(&ssl_ctx->ssl);
        mbedtls_ssl_config_free(&ssl_ctx->conf);
        mbedtls_ctr_drbg_free(&ssl_ctx->ctr_drbg);
        mbedtls_entropy_free(&ssl_ctx->entropy);
        free(ssl_ctx);
    } else {
        close(sock_fd);
    }

    free(header_buf);
    free(host);
    free(path);
    return total_read;

cleanup:
    if (ssl_ctx) {
        if (ssl_ctx->initialized) {
            mbedtls_ssl_close_notify(&ssl_ctx->ssl);
        }
        mbedtls_net_free(&ssl_ctx->net);
        mbedtls_ssl_free(&ssl_ctx->ssl);
        mbedtls_ssl_config_free(&ssl_ctx->conf);
        mbedtls_ctr_drbg_free(&ssl_ctx->ctr_drbg);
        mbedtls_entropy_free(&ssl_ctx->entropy);
        free(ssl_ctx);
    } else if (sock_fd >= 0) {
        close(sock_fd);
    }
    free(header_buf);
    free(host);
    free(path);
    return -1;
}
