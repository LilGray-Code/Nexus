#include "win_driver_inf.h"

static char lower_ascii(char c) {
    return c >= 'A' && c <= 'Z' ? (char)(c - 'A' + 'a') : c;
}

static bool equal_ci(const char *a, const char *b, size_t n) {
    for (size_t i = 0U; i < n; ++i)
        if (lower_ascii(a[i]) != lower_ascii(b[i])) return false;
    return true;
}

static void trim(const char **p, size_t *n) {
    while (*n && ((*p)[0] == ' ' || (*p)[0] == '\t')) { ++*p; --*n; }
    while (*n && ((*p)[*n - 1U] == ' ' || (*p)[*n - 1U] == '\t' ||
                  (*p)[*n - 1U] == '\r')) --*n;
    if (*n >= 2U && (*p)[0] == '"' && (*p)[*n - 1U] == '"') {
        ++*p; *n -= 2U;
    }
}

static bool copy_field(char *out, const char *p, size_t n) {
    if (!out || !p || n == 0U || n >= NEXUS_WIN_INF_FIELD_MAX) return false;
    for (size_t i = 0U; i < n; ++i) out[i] = p[i];
    out[n] = '\0';
    return true;
}

static bool csv_field(const char *p, size_t n, size_t wanted,
                      const char **field, size_t *field_length) {
    size_t index = 0U, start = 0U;
    bool quoted = false;
    for (size_t i = 0U; i <= n; ++i) {
        const bool at_end = i == n;
        if (!at_end && p[i] == '"') quoted = !quoted;
        if (at_end || (p[i] == ',' && !quoted)) {
            if (index == wanted) {
                *field = p + start;
                *field_length = i - start;
                trim(field, field_length);
                return *field_length != 0U;
            }
            ++index; start = i + 1U;
        }
    }
    return false;
}

static uint16_t hex_value(const char *p, size_t n) {
    if (n == 0U || n > 4U) return 0xFFFFU;
    uint16_t value = 0U;
    for (size_t i = 0U; i < n; ++i) {
        const char c = lower_ascii(p[i]);
        uint8_t digit;
        if (c >= '0' && c <= '9') digit = (uint8_t)(c - '0');
        else if (c >= 'a' && c <= 'f') digit = (uint8_t)(c - 'a' + 10);
        else return 0xFFFFU;
        value = (uint16_t)((value << 4) | digit);
    }
    return value;
}

static bool find_hex(const char *p, size_t n, const char *marker,
                     size_t marker_length, size_t digits, uint16_t *value) {
    const size_t m = marker_length;
    for (size_t i = 0U; i + m + digits <= n; ++i) {
        if ((i == 0U || p[i - 1U] == '&' || p[i - 1U] == '\\') &&
            equal_ci(p + i, marker, m)) {
            *value = hex_value(p + i + m, digits);
            return *value != 0xFFFFU;
        }
    }
    return false;
}

static bool find_subsystem(const char *p, size_t n,
                           uint16_t *sub_device, uint16_t *sub_vendor) {
    const char marker[] = "SUBSYS_";
    for (size_t i = 0U; i + 15U <= n; ++i) {
        if ((i == 0U || p[i - 1U] == '&' || p[i - 1U] == '\\') &&
            equal_ci(p + i, marker, 7U)) {
            *sub_device = hex_value(p + i + 7U, 4U);
            *sub_vendor = hex_value(p + i + 11U, 4U);
            return *sub_device != 0xFFFFU && *sub_vendor != 0xFFFFU;
        }
    }
    return false;
}

static bool match_id(const char *id, size_t n,
                     const struct nexus_win_device_id *d,
                     uint16_t *specificity) {
    uint16_t vendor, product;
    if (d->bus == NEXUS_WIN_DEVICE_PCI) {
        if (n < 8U || !equal_ci(id, "PCI\\VEN_", 8U) ||
            !find_hex(id, n, "VEN_", 4U, 4U, &vendor) ||
            !find_hex(id, n, "DEV_", 4U, 4U, &product) ||
            vendor != d->vendor_id || product != d->device_id) return false;
    } else if (d->bus == NEXUS_WIN_DEVICE_USB) {
        if (n < 8U || !equal_ci(id, "USB\\VID_", 8U) ||
            !find_hex(id, n, "VID_", 4U, 4U, &vendor) ||
            !find_hex(id, n, "PID_", 4U, 4U, &product) ||
            vendor != d->vendor_id || product != d->device_id) return false;
    } else return false;

    *specificity = 1U;
    if (d->bus == NEXUS_WIN_DEVICE_PCI) {
        uint16_t sub_device, sub_vendor;
        if (find_subsystem(id, n, &sub_device, &sub_vendor)) {
            if (sub_device != d->subsystem_device_id ||
                sub_vendor != d->subsystem_vendor_id) return false;
            *specificity = 2U;
        }
        uint16_t revision;
        if (find_hex(id, n, "REV_", 4U, 2U, &revision)) {
            if ((uint8_t)revision != d->revision_id) return false;
            *specificity = 3U;
        }
    }
    return true;
}


static bool is_model_section(const char *name) {
    static const char models[] = "models";
    if (name == NULL) return false;
    for (size_t i = 0U; name[i] != '\0'; ++i) {
        size_t j = 0U;
        while (models[j] != '\0' && name[i + j] != '\0' &&
               lower_ascii(name[i + j]) == models[j]) ++j;
        if (models[j] == '\0') return true;
        if (name[i] == '.' && name[i + 1U] != '\0' &&
            name[i + 2U] != '\0' && lower_ascii(name[i + 1U]) == 'n' &&
            lower_ascii(name[i + 2U]) == 't') return true;
    }
    return false;
}

bool nexus_win_inf_find_match(const char *text, size_t length,
                              const struct nexus_win_device_id *device,
                              struct nexus_win_inf_match *match) {
    if (!text || !device || !match || length == 0U) return false;
    *match = (struct nexus_win_inf_match){0};
    char line[NEXUS_WIN_INF_LINE_MAX];
    char section[NEXUS_WIN_INF_FIELD_MAX] = {0};
    const bool utf16le = length >= 2U &&
        (uint8_t)text[0] == 0xFFU && (uint8_t)text[1] == 0xFEU;
    if (utf16le && ((length - 2U) & 1U) != 0U) return false;
    size_t offset = utf16le ? 2U :
        (length >= 3U && (uint8_t)text[0] == 0xEFU &&
         (uint8_t)text[1] == 0xBBU && (uint8_t)text[2] == 0xBFU ? 3U : 0U);
    size_t line_number = 0U;
    uint16_t best = 0U;

    while (offset < length) {
        size_t n = 0U;
        while (offset < length) {
            char ch;
            if (utf16le) {
                if (length - offset < 2U) return false;
                const uint16_t unit = (uint16_t)(uint8_t)text[offset] |
                    (uint16_t)((uint16_t)(uint8_t)text[offset + 1U] << 8);
                offset += 2U;
                ch = unit <= 0x7FU ? (char)unit : '?';
            } else {
                ch = text[offset++];
            }
            if (ch == '\n') break;
            if (ch == '\0' || (uint8_t)ch < 0x20U && ch != '\t' && ch != '\r')
                return false;
            if (n + 1U >= sizeof(line)) return false;
            line[n++] = ch;
        }
        ++line_number;
        line[n] = '\0';
        const char *p = line;
        size_t used = n;
        trim(&p, &used);
        if (used == 0U || p[0] == ';') continue;

        if (p[0] == '[' && used >= 3U && p[used - 1U] == ']') {
            const char *name = p + 1U;
            size_t name_length = used - 2U;
            if (!copy_field(section, name, name_length)) section[0] = '\0';
            continue;
        }
        if (!is_model_section(section)) continue;

        size_t equals = 0U;
        while (equals < used && p[equals] != '=') ++equals;
        if (equals == used) continue;
        const char *description = p;
        size_t description_length = equals;
        trim(&description, &description_length);
        const char *install = NULL, *id = NULL;
        size_t install_length = 0U, id_length = 0U;
        if (!csv_field(p + equals + 1U, used - equals - 1U, 0U,
                       &install, &install_length)) continue;
        for (size_t field = 1U; field < 16U; ++field) {
            if (!csv_field(p + equals + 1U, used - equals - 1U, field,
                           &id, &id_length)) break;
            uint16_t specificity = 0U;
            if (match_id(id, id_length, device, &specificity) &&
                specificity > best) {
                struct nexus_win_inf_match candidate = {0};
                if (!copy_field(candidate.model_description, description,
                                description_length) ||
                    !copy_field(candidate.install_section, install,
                                install_length) ||
                    !copy_field(candidate.hardware_id, id, id_length))
                    continue;
                candidate.specificity = specificity;
                candidate.line_number = line_number;
                *match = candidate;
                best = specificity;
            }
        }
    }
    return best != 0U;
}
