#include "browser_download.h"

#include "browser_http.h"
#include "browser_url.h"
#include "net.h"
#include "nxfs.h"
#include "timer.h"
#include "vfs.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DOWNLOAD_TIMEOUT_TICKS 6000U
#define DOWNLOAD_IDLE_TICKS 600U
#define DOWNLOAD_RX_CHUNK 512U
#define DOWNLOAD_REQUEST_CAPACITY 1200U

static uint8_t download_data[NEXUS_NXFS_MAX_FILE_BYTES];

static bool append_text(char *buffer, size_t capacity, size_t *used,
                        const char *text) {
    if (buffer == NULL || used == NULL || text == NULL) return false;
    while (*text != '\0') {
        if (*used + 1U >= capacity) return false;
        buffer[(*used)++] = *text++;
        buffer[*used] = '\0';
    }
    return true;
}

static bool append_u16(char *buffer, size_t capacity, size_t *used,
                       uint16_t value) {
    char reverse[5];
    size_t count = 0U;
    do {
        reverse[count++] = (char)('0' + value % 10U);
        value = (uint16_t)(value / 10U);
    } while (value != 0U && count < sizeof(reverse));
    while (count != 0U) {
        const char digit[2] = {reverse[--count], '\0'};
        if (!append_text(buffer, capacity, used, digit)) return false;
    }
    return true;
}

static bool find_header_end(const uint8_t *data, size_t length) {
    if (data == NULL || length < 4U) return false;
    return data[length - 4U] == '\r' && data[length - 3U] == '\n' &&
           data[length - 2U] == '\r' && data[length - 1U] == '\n';
}

static bool download_make_filename(const struct nexus_browser_url *url,
                                   const char *suggested,
                                   char output[NEXUS_VFS_NAME_MAX]) {
    if (url == NULL || output == NULL) return false;
    const char *candidate = suggested;
    if (candidate == NULL || candidate[0] == '\0') {
        candidate = url->path;
        const char *last_slash = candidate;
        for (const char *p = candidate; *p != '\0' && *p != '?'; ++p)
            if (*p == '/') last_slash = p + 1;
        candidate = last_slash;
    }

    size_t used = 0U;
    while (*candidate != '\0' && *candidate != '?' &&
           *candidate != '#' && used < NEXUS_VFS_NAME_MAX - 1U) {
        const unsigned char c = (unsigned char)*candidate++;
        const bool safe =
            (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_';
        output[used++] = safe ? (char)c : '_';
    }
    while (used > 0U && output[used - 1U] == '.') --used;
    if (used == 0U || (used == 1U && output[0] == '.') ||
        (used == 2U && output[0] == '.' && output[1] == '.')) {
        static const char fallback[] = "DOWNLOAD.BIN";
        used = sizeof(fallback) - 1U;
        for (size_t i = 0U; i < used; ++i) output[i] = fallback[i];
    }
    output[used] = '\0';
    return true;
}

static bool verify_saved_file(const char *name, size_t expected_length) {
    struct nexus_vfs_file file;
    if (!vfs_open_readonly(name, &file)) return false;
    uint32_t file_size = 0U;
    bool valid = vfs_file_size(&file, &file_size) &&
                 file_size == expected_length;
    uint8_t verify_buffer[NEXUS_VFS_READ_CHUNK];
    size_t offset = 0U;
    while (valid && offset < expected_length) {
        size_t amount = expected_length - offset;
        if (amount > sizeof(verify_buffer)) amount = sizeof(verify_buffer);
        size_t received = 0U;
        valid = vfs_read(&file, verify_buffer, amount, &received) &&
                received == amount;
        for (size_t i = 0U; valid && i < received; ++i) {
            if (verify_buffer[i] != download_data[offset + i]) valid = false;
        }
        offset += received;
    }
    vfs_close(&file);
    return valid && offset == expected_length;
}

enum nexus_browser_download_result
nexus_browser_download_http(const char *address, char *saved_name,
                            size_t saved_name_capacity) {
    if (address == NULL || saved_name == NULL ||
        saved_name_capacity < NEXUS_VFS_NAME_MAX) {
        return NEXUS_BROWSER_DOWNLOAD_INVALID_URL;
    }
    saved_name[0] = '\0';

    struct nexus_browser_url url;
    if (!nexus_browser_url_parse(address, &url))
        return NEXUS_BROWSER_DOWNLOAD_INVALID_URL;
    if (url.scheme != NEXUS_BROWSER_SCHEME_HTTP)
        return NEXUS_BROWSER_DOWNLOAD_HTTPS_BLOCKED;
    if (!vfs_nxfs_writable())
        return NEXUS_BROWSER_DOWNLOAD_NOT_PERSISTENT;

    uint8_t remote_ip[4];
    if (!net_dns_resolve(url.host, remote_ip, 600U))
        return NEXUS_BROWSER_DOWNLOAD_DNS_FAILED;
    if (!net_tcp_connect(remote_ip, url.port, 1000U))
        return NEXUS_BROWSER_DOWNLOAD_CONNECT_FAILED;

    char request[DOWNLOAD_REQUEST_CAPACITY] = {0};
    size_t request_length = 0U;
    bool request_ok =
        append_text(request, sizeof(request), &request_length, "GET ") &&
        append_text(request, sizeof(request), &request_length, url.path) &&
        append_text(request, sizeof(request), &request_length,
                    " HTTP/1.0\r\nHost: ") &&
        append_text(request, sizeof(request), &request_length, url.host);
    if (request_ok && url.explicit_port) {
        request_ok = append_text(request, sizeof(request), &request_length, ":") &&
                     append_u16(request, sizeof(request), &request_length,
                                url.port);
    }
    request_ok = request_ok &&
        append_text(request, sizeof(request), &request_length,
                    "\r\nConnection: close\r\n"
                    "Accept: */*\r\nAccept-Encoding: identity\r\n"
                    "User-Agent: NexusBrowser/0.2\r\n\r\n");
    if (!request_ok ||
        !net_tcp_send(request, (uint16_t)request_length)) {
        net_tcp_close();
        return NEXUS_BROWSER_DOWNLOAD_REQUEST_FAILED;
    }

    uint8_t headers[NEXUS_BROWSER_HTTP_HEADER_MAX + 4U];
    uint8_t incoming[DOWNLOAD_RX_CHUNK];
    size_t header_length = 0U;
    size_t body_length = 0U;
    bool header_complete = false;
    bool header_overflow = false;
    const uint64_t start = timer_ticks();
    uint64_t last_data = start;

    while (!header_complete && !header_overflow &&
           timer_ticks() - start < DOWNLOAD_TIMEOUT_TICKS) {
        net_poll();
        const size_t received = net_tcp_receive(incoming, sizeof(incoming));
        if (received != 0U) {
            last_data = timer_ticks();
            for (size_t i = 0U; i < received; ++i) {
                if (header_complete) {
                    if (body_length < sizeof(download_data))
                        download_data[body_length++] = incoming[i];
                    else
                        header_overflow = true;
                } else {
                    if (header_length >= sizeof(headers)) {
                        header_overflow = true;
                        break;
                    }
                    headers[header_length++] = incoming[i];
                    if (find_header_end(headers, header_length))
                        header_complete = true;
                }
            }
        } else if (header_length != 0U &&
                   timer_ticks() - last_data >= DOWNLOAD_IDLE_TICKS) {
            break;
        }
        __asm__ volatile ("pause");
    }

    if (!header_complete || header_overflow) {
        net_tcp_close();
        return NEXUS_BROWSER_DOWNLOAD_HEADER_FAILED;
    }

    struct nexus_browser_http_response parsed;
    if (!nexus_browser_http_parse(headers, header_length, &parsed)) {
        net_tcp_close();
        return NEXUS_BROWSER_DOWNLOAD_HEADER_FAILED;
    }
    if (parsed.status_code >= 300U && parsed.status_code < 400U) {
        net_tcp_close();
        return NEXUS_BROWSER_DOWNLOAD_REDIRECT;
    }
    if (parsed.status_code < 200U || parsed.status_code >= 300U) {
        net_tcp_close();
        return NEXUS_BROWSER_DOWNLOAD_HEADER_FAILED;
    }
    if (parsed.chunked_transfer) {
        net_tcp_close();
        return NEXUS_BROWSER_DOWNLOAD_CHUNKED_UNSUPPORTED;
    }
    if (!parsed.has_content_length) {
        net_tcp_close();
        return NEXUS_BROWSER_DOWNLOAD_LENGTH_REQUIRED;
    }
    if (parsed.content_length > NEXUS_NXFS_MAX_FILE_BYTES) {
        net_tcp_close();
        return NEXUS_BROWSER_DOWNLOAD_TOO_LARGE;
    }
    const size_t expected_length = parsed.content_length;
    if (body_length > expected_length) body_length = expected_length;

    const uint64_t body_start = timer_ticks();
    last_data = body_start;
    while (body_length < expected_length &&
           timer_ticks() - body_start < DOWNLOAD_TIMEOUT_TICKS &&
           timer_ticks() - last_data < DOWNLOAD_IDLE_TICKS) {
        net_poll();
        const size_t received = net_tcp_receive(incoming, sizeof(incoming));
        if (received != 0U) {
            last_data = timer_ticks();
            size_t amount = received;
            if (amount > expected_length - body_length)
                amount = expected_length - body_length;
            for (size_t i = 0U; i < amount; ++i)
                download_data[body_length + i] = incoming[i];
            body_length += amount;
        }
        __asm__ volatile ("pause");
    }
    net_tcp_close();
    if (body_length != expected_length)
        return NEXUS_BROWSER_DOWNLOAD_INCOMPLETE;

    if (!download_make_filename(&url, parsed.filename, saved_name))
        return NEXUS_BROWSER_DOWNLOAD_INVALID_URL;
    struct nexus_vfs_file existing;
    if (vfs_open_readonly(saved_name, &existing)) {
        vfs_close(&existing);
        saved_name[0] = '\0';
        return NEXUS_BROWSER_DOWNLOAD_FILE_EXISTS;
    }
    if (!vfs_write_file(saved_name, download_data, expected_length)) {
        saved_name[0] = '\0';
        return NEXUS_BROWSER_DOWNLOAD_SAVE_FAILED;
    }
    if (!verify_saved_file(saved_name, expected_length)) {
        (void)vfs_remove_file(saved_name);
        saved_name[0] = '\0';
        return NEXUS_BROWSER_DOWNLOAD_VERIFY_FAILED;
    }
    return NEXUS_BROWSER_DOWNLOAD_OK;
}
