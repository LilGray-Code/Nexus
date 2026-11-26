#include "download_https.h"

#include "ca_bundle.h"
#include "http.h"
#include "net.h"
#include "net_https.h"
#include "timer.h"

#include <stddef.h>
#include <stdint.h>

static struct nexus_https_connection *connection(
    struct nexus_download_https_context *context) {
    return context == NULL ? NULL : &context->connection;
}

static bool append_text(char *out, size_t capacity, size_t *used,
                        const char *text) {
    if (out == NULL || used == NULL || text == NULL || capacity == 0U)
        return false;
    for (size_t i = 0U; text[i] != '\0'; ++i) {
        if (*used + 1U >= capacity) return false;
        out[(*used)++] = text[i];
    }
    out[*used] = '\0';
    return true;
}

bool nexus_download_https_parse_url(
    const char *url,
    char hostname[NEXUS_DOWNLOAD_HTTPS_HOST_MAX + 1U],
    char request_path[NEXUS_DOWNLOAD_HTTPS_PATH_MAX + 1U]) {
    static const char scheme[] = "https://";
    if (url == NULL || hostname == NULL || request_path == NULL) return false;
    for (size_t i = 0U; i < sizeof(scheme) - 1U; ++i)
        if (url[i] != scheme[i]) return false;

    const size_t host_start = sizeof(scheme) - 1U;
    size_t host_end = host_start;
    while (url[host_end] != '\0' && url[host_end] != '/' &&
           url[host_end] != '?' && url[host_end] != '#') {
        const char c = url[host_end];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') || c == '-' || c == '.')) return false;
        ++host_end;
        if (host_end - host_start > NEXUS_DOWNLOAD_HTTPS_HOST_MAX)
            return false;
    }
    const size_t host_length = host_end - host_start;
    if (host_length == 0U || url[host_start] == '.' ||
        url[host_start] == '-' || url[host_end - 1U] == '.' ||
        url[host_end - 1U] == '-') return false;
    for (size_t i = host_start; i < host_end; ++i) {
        if (url[i] == '.' &&
            (i == host_start || url[i - 1U] == '.' || url[i - 1U] == '-'))
            return false;
        if (url[i] == '-' &&
            (i == host_start || url[i - 1U] == '.' || url[i - 1U] == '-'))
            return false;
    }
    for (size_t i = 0U; i < host_length; ++i)
        hostname[i] = url[host_start + i];
    hostname[host_length] = '\0';

    size_t path_length = 0U;
    if (url[host_end] == '\0') {
        request_path[path_length++] = '/';
    } else {
        if (url[host_end] == '#') return false;
        if (url[host_end] == '?') request_path[path_length++] = '/';
        for (size_t i = host_end; url[i] != '\0'; ++i) {
            const unsigned char c = (unsigned char)url[i];
            if (c <= 0x20U || c >= 0x7FU || c == '\\' || c == '#' ||
                path_length >= NEXUS_DOWNLOAD_HTTPS_PATH_MAX) return false;
            request_path[path_length++] = (char)c;
        }
    }
    if (path_length == 0U ||
        path_length > NEXUS_DOWNLOAD_HTTPS_PATH_MAX) return false;
    request_path[path_length] = '\0';
    return true;
}

bool nexus_download_https_context_init(
    struct nexus_download_https_context *context, uint32_t expected_size) {
    if (context == NULL || expected_size == 0U ||
        expected_size > NEXUS_DOWNLOAD_MANAGER_MAX_BYTES) return false;
    *context = (struct nexus_download_https_context){0};
    context->expected_size = expected_size;
    return true;
}

static bool send_all(struct nexus_https_connection *https,
                     const uint8_t *request, size_t length) {
    const uint64_t started = timer_ticks();
    size_t sent = 0U;
    while (sent < length &&
           timer_ticks() - started < NEXUS_DOWNLOAD_HTTPS_TIMEOUT_TICKS) {
        const int result = nexus_https_write(
            https, request + sent, length - sent);
        if (result > 0) {
            if ((size_t)result > length - sent) return false;
            sent += (size_t)result;
            continue;
        }
        if (result != NEXUS_HTTPS_WANT_READ &&
            result != NEXUS_HTTPS_WANT_WRITE) return false;
        net_poll();
        __asm__ volatile ("pause" ::: "memory");
    }
    return sent == length;
}

static bool read_headers(struct nexus_download_https_context *context,
                         struct nexus_http_response *response) {
    size_t used = 0U;
    size_t header_length = 0U;
    const uint64_t started = timer_ticks();
    struct nexus_https_connection *https = connection(context);

    while (used < sizeof(context->pending) &&
           timer_ticks() - started < NEXUS_DOWNLOAD_HTTPS_TIMEOUT_TICKS) {
        const int result = nexus_https_read(
            https, context->pending + used, sizeof(context->pending) - used);
        if (result > 0) {
            if ((size_t)result > sizeof(context->pending) - used)
                return false;
            used += (size_t)result;
            if (nexus_http_decode_headers(context->pending, used,
                                          &header_length, response)) {
                if (header_length > used) return false;
                context->pending_length = used - header_length;
                if (context->pending_length > context->expected_size)
                    return false;
                /* Compact any body bytes that arrived with the HTTP headers. */
                for (size_t i = 0U; i < context->pending_length; ++i)
                    context->pending[i] = context->pending[header_length + i];
                context->pending_offset = 0U;
                return true;
            }
            /* Fail early if the header terminator has arrived but is invalid. */
            for (size_t i = 0U; i + 3U < used; ++i) {
                if (context->pending[i] == '\r' &&
                    context->pending[i + 1U] == '\n' &&
                    context->pending[i + 2U] == '\r' &&
                    context->pending[i + 3U] == '\n') return false;
            }
            continue;
        }
        if (result == 0) return false;
        if (result != NEXUS_HTTPS_WANT_READ &&
            result != NEXUS_HTTPS_WANT_WRITE) return false;
        net_poll();
        __asm__ volatile ("pause" ::: "memory");
    }
    return false;
}

static bool https_source_open(const char *url, void **session, void *opaque) {
    struct nexus_download_https_context *context =
        (struct nexus_download_https_context *)opaque;
    if (context == NULL || session == NULL || context->expected_size == 0U)
        return false;

    if (context->connected) {
        nexus_https_close(connection(context));
        context->connected = false;
    }
    context->pending_offset = 0U;
    context->pending_length = 0U;
    context->body_remaining = 0U;
    if (!nexus_download_https_parse_url(
            url, context->hostname, context->request_path)) return false;

    size_t ca_length = 0U;
    const unsigned char *ca = nexus_builtin_ca_bundle_get(&ca_length);
    if (ca == NULL || ca_length < 2U || ca[ca_length - 1U] != 0U)
        return false;

    struct nexus_https_connection *https = connection(context);
    const int connected = nexus_https_connect(
        https, context->hostname, ca, ca_length,
        NEXUS_DOWNLOAD_HTTPS_TIMEOUT_TICKS);
    if (connected != 0 || !nexus_https_is_verified(https)) {
        nexus_https_close(https);
        return false;
    }
    context->connected = true;

    char request[512] = {0};
    size_t request_length = 0U;
    if (!append_text(request, sizeof(request), &request_length, "GET ") ||
        !append_text(request, sizeof(request), &request_length,
                     context->request_path) ||
        !append_text(request, sizeof(request), &request_length,
                     " HTTP/1.1\r\nHost: ") ||
        !append_text(request, sizeof(request), &request_length,
                     context->hostname) ||
        !append_text(request, sizeof(request), &request_length,
                     "\r\nUser-Agent: NexusPackageManager/1\r\n"
                     "Accept: application/octet-stream\r\n"
                     "Accept-Encoding: identity\r\n"
                     "Connection: close\r\n\r\n") ||
        !send_all(https, (const uint8_t *)request, request_length)) {
        nexus_https_close(https);
        context->connected = false;
        return false;
    }

    struct nexus_http_response response = {0};
    if (!read_headers(context, &response) || response.status_code != 200U ||
        !response.has_content_length || response.chunked ||
        response.content_length != context->expected_size) {
        nexus_https_close(https);
        context->connected = false;
        return false;
    }
    context->body_remaining = context->expected_size;
    *session = context;
    return true;
}

static int https_source_read(void *session, uint8_t *buffer, size_t capacity,
                             void *opaque) {
    struct nexus_download_https_context *context =
        (struct nexus_download_https_context *)session;
    (void)opaque;
    if (context == NULL || buffer == NULL || capacity == 0U ||
        !context->connected) return -1;
    if (context->body_remaining == 0U) return 0;

    if (context->pending_offset < context->pending_length) {
        size_t amount = context->pending_length - context->pending_offset;
        if (amount > capacity) amount = capacity;
        if (amount > context->body_remaining) return -1;
        for (size_t i = 0U; i < amount; ++i)
            buffer[i] = context->pending[context->pending_offset + i];
        context->pending_offset += amount;
        context->body_remaining -= (uint32_t)amount;
        return (int)amount;
    }

    const uint64_t started = timer_ticks();
    while (timer_ticks() - started < NEXUS_DOWNLOAD_HTTPS_TIMEOUT_TICKS) {
        size_t request = capacity;
        if (request > context->body_remaining)
            request = context->body_remaining;
        const int result = nexus_https_read(connection(context), buffer,
                                            request);
        if (result > 0) {
            if ((size_t)result > request ||
                (uint32_t)result > context->body_remaining) return -1;
            context->body_remaining -= (uint32_t)result;
            return result;
        }
        if (result == 0) return -1; /* Unexpected EOF before Content-Length. */
        if (result != NEXUS_HTTPS_WANT_READ &&
            result != NEXUS_HTTPS_WANT_WRITE) return -1;
        net_poll();
        __asm__ volatile ("pause" ::: "memory");
    }
    return -1;
}

static void https_source_close(void *session, void *opaque) {
    struct nexus_download_https_context *context =
        (struct nexus_download_https_context *)session;
    (void)opaque;
    if (context == NULL) return;
    if (context->connected) nexus_https_close(connection(context));
    context->connected = false;
    context->body_remaining = 0U;
    context->pending_offset = 0U;
    context->pending_length = 0U;
}

static const struct nexus_download_source_ops https_source = {
    .open = https_source_open,
    .read = https_source_read,
    .close = https_source_close
};

const struct nexus_download_source_ops *
nexus_download_https_source_ops(void) {
    return &https_source;
}
