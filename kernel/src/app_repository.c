#include "app_repository.h"

#include <stddef.h>
#include <stdint.h>

#define APP_REPOSITORY_MAGIC "NEXUS-APP-REPOSITORY/1"

static bool text_copy(char *out, size_t capacity, const uint8_t *bytes,
                      size_t length, bool allow_empty) {
    if (out == NULL || bytes == NULL || capacity == 0U ||
        (!allow_empty && length == 0U) || length >= capacity) return false;
    for (size_t i = 0U; i < length; ++i) {
        if (bytes[i] < 0x20U || bytes[i] >= 0x7FU || bytes[i] == '|')
            return false;
        out[i] = (char)bytes[i];
    }
    out[length] = '\0';
    return true;
}

static bool line_next(const uint8_t *bytes, size_t length, size_t *offset,
                      size_t *start, size_t *line_length) {
    if (bytes == NULL || offset == NULL || start == NULL ||
        line_length == NULL || *offset >= length) return false;
    const size_t begin = *offset;
    size_t end = begin;
    while (end < length && bytes[end] != '\n') {
        if (bytes[end] == '\r' || bytes[end] == 0U ||
            end - begin >= NEXUS_APP_REPOSITORY_MAX_LINE) return false;
        ++end;
    }
    if (end >= length) return false;
    *start = begin;
    *line_length = end - begin;
    *offset = end + 1U;
    return true;
}

static bool line_value(const uint8_t *bytes, size_t start, size_t length,
                       const char *prefix, char *output, size_t capacity) {
    size_t prefix_length = 0U;
    while (prefix[prefix_length] != '\0') ++prefix_length;
    if (length <= prefix_length) return false;
    for (size_t i = 0U; i < prefix_length; ++i)
        if (bytes[start + i] != (uint8_t)prefix[i]) return false;
    return text_copy(output, capacity, bytes + start + prefix_length,
                     length - prefix_length, false);
}

static bool parse_u32(const char *text, uint32_t maximum, uint32_t *value) {
    if (text == NULL || value == NULL || text[0] == '\0' ||
        (text[0] == '0' && text[1] != '\0')) return false;
    uint32_t parsed = 0U;
    for (size_t i = 0U; text[i] != '\0'; ++i) {
        if (text[i] < '0' || text[i] > '9') return false;
        const uint32_t digit = (uint32_t)(text[i] - '0');
        if (digit > maximum || parsed > (maximum - digit) / 10U)
            return false;
        parsed = parsed * 10U + digit;
    }
    *value = parsed;
    return true;
}

static bool version_valid(const char *version) {
    if (version == NULL) return false;
    size_t length = 0U, component_start = 0U;
    unsigned components = 0U;
    uint32_t component = 0U;
    while (version[length] != '\0') {
        const char c = version[length];
        if (c == '.') {
            if (length == component_start || component > 255U) return false;
            ++components;
            component_start = length + 1U;
            component = 0U;
        } else {
            if (c < '0' || c > '9') return false;
            if (length == component_start && c == '0' &&
                version[length + 1U] != '.' && version[length + 1U] != '\0')
                return false;
            component = component * 10U + (uint32_t)(c - '0');
            if (component > 255U) return false;
        }
        if (++length > NEXUS_PACKAGE_VERSION_MAX) return false;
    }
    return length > component_start && components == 2U;
}

static bool id_valid(const char *id) {
    if (id == NULL || id[0] < 'A' || id[0] > 'Z') return false;
    size_t length = 0U;
    char previous = '\0';
    while (id[length] != '\0') {
        const char c = id[length];
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              c == '-') || (c == '-' && previous == '-')) return false;
        previous = c;
        if (++length > NEXUS_PACKAGE_ID_MAX) return false;
    }
    return length != 0U && previous != '-';
}

static bool key_valid(const char *key) {
    if (key == NULL || key[0] == '\0') return false;
    size_t length = 0U;
    while (key[length] != '\0') {
        const char c = key[length];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') || c == '-' || c == '_')) return false;
        if (++length > NEXUS_PACKAGE_KEY_ID_MAX) return false;
    }
    return true;
}

static bool url_valid(const char *url, bool allow_empty) {
    static const char scheme[] = "https://";
    if (url == NULL || (allow_empty && url[0] == '\0')) return allow_empty;
    for (size_t i = 0U; i < sizeof(scheme) - 1U; ++i)
        if (url[i] != scheme[i]) return false;
    size_t host_start = sizeof(scheme) - 1U, host_end = host_start;
    while (url[host_end] != '\0' && url[host_end] != '/' &&
           url[host_end] != '?' && url[host_end] != '#') {
        const char c = url[host_end];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') || c == '-' || c == '.')) return false;
        ++host_end;
        if (host_end - host_start > 100U) return false;
    }
    if (host_end == host_start || url[host_start] == '.' ||
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
    size_t length = host_end;
    while (url[length] != '\0') {
        const unsigned char c = (unsigned char)url[length++];
        if (c <= 0x20U || c >= 0x7FU || c == '\\' || c == '|') return false;
        if (length > NEXUS_APP_REPOSITORY_MAX_URL) return false;
    }
    return true;
}

static int hex_value(uint8_t c) {
    if (c >= '0' && c <= '9') return (int)(c - '0');
    if (c >= 'a' && c <= 'f') return (int)(c - 'a') + 10;
    if (c >= 'A' && c <= 'F') return (int)(c - 'A') + 10;
    return -1;
}

static bool hex_decode(const uint8_t *bytes, size_t length,
                       uint8_t *output, size_t output_length) {
    if (bytes == NULL || output == NULL || length != output_length * 2U)
        return false;
    for (size_t i = 0U; i < output_length; ++i) {
        const int hi = hex_value(bytes[i * 2U]);
        const int lo = hex_value(bytes[i * 2U + 1U]);
        if (hi < 0 || lo < 0) return false;
        output[i] = (uint8_t)((hi << 4) | lo);
    }
    return true;
}

static bool field_equals(const uint8_t *bytes, size_t start, size_t length,
                         const char *value) {
    size_t n = 0U;
    while (value[n] != '\0') ++n;
    if (n != length) return false;
    for (size_t i = 0U; i < n; ++i)
        if (bytes[start + i] != (uint8_t)value[i]) return false;
    return true;
}

static bool app_parse(const uint8_t *bytes, size_t start, size_t length,
                      struct nexus_app_repository_entry *app) {
    static const char prefix[] = "APP=";
    if (bytes == NULL || app == NULL || length <= sizeof(prefix) - 1U)
        return false;
    if (!field_equals(bytes, start, sizeof(prefix) - 1U, prefix)) return false;
    const size_t value_start = start + sizeof(prefix) - 1U;
    size_t separators[12], count = 0U;
    for (size_t i = value_start; i < start + length; ++i) {
        if (bytes[i] == '|') {
            if (count >= 12U) return false;
            separators[count++] = i;
        }
    }
    if (count != 12U) return false;
    size_t field_start[13], field_length[13];
    field_start[0] = value_start;
    for (size_t i = 0U; i < 12U; ++i) {
        field_length[i] = separators[i] - field_start[i];
        field_start[i + 1U] = separators[i] + 1U;
    }
    field_length[12] = start + length - field_start[12];

    char number[16];
    if (!text_copy(app->id, sizeof(app->id), bytes + field_start[0],
                   field_length[0], false) || !id_valid(app->id) ||
        !text_copy(app->version, sizeof(app->version),
                   bytes + field_start[1], field_length[1], false) ||
        !version_valid(app->version) ||
        !text_copy(number, sizeof(number), bytes + field_start[2],
                   field_length[2], false) ||
        !parse_u32(number, 256U * 1024U * 1024U, &app->package_size) ||
        app->package_size == 0U ||
        !text_copy(app->package_url, sizeof(app->package_url),
                   bytes + field_start[3], field_length[3], false) ||
        !url_valid(app->package_url, false) ||
        !hex_decode(bytes + field_start[4], field_length[4],
                    app->package_sha256, sizeof(app->package_sha256)) ||
        !text_copy(app->name, sizeof(app->name), bytes + field_start[5],
                   field_length[5], false) ||
        !text_copy(app->publisher, sizeof(app->publisher),
                   bytes + field_start[6], field_length[6], false) ||
        !text_copy(app->category, sizeof(app->category),
                   bytes + field_start[7], field_length[7], false) ||
        !text_copy(app->description, sizeof(app->description),
                   bytes + field_start[8], field_length[8], false) ||
        !text_copy(app->screenshot_url, sizeof(app->screenshot_url),
                   bytes + field_start[9], field_length[9], true) ||
        !url_valid(app->screenshot_url, true) ||
        !text_copy(number, sizeof(number), bytes + field_start[10],
                   field_length[10], false)) return false;

    uint32_t rating = 0U;
    if (!parse_u32(number, 50U, &rating)) return false;
    app->rating_tenths = (uint8_t)rating;
    if (!text_copy(number, sizeof(number), bytes + field_start[11],
                   field_length[11], false) ||
        !parse_u32(number, UINT32_MAX, &app->rating_count)) return false;

    if (field_equals(bytes, field_start[12], field_length[12], "-"))
        return true;
    size_t cursor = field_start[12], end = cursor + field_length[12];
    while (cursor < end) {
        if (app->dependency_count >= NEXUS_APP_REPOSITORY_MAX_DEPENDENCIES)
            return false;
        size_t token_end = cursor;
        while (token_end < end && bytes[token_end] != ';') ++token_end;
        size_t separator = cursor;
        while (separator < token_end && bytes[separator] != '@') ++separator;
        struct nexus_app_dependency *dep =
            &app->dependencies[app->dependency_count];
        if (separator == cursor || separator == token_end ||
            !text_copy(dep->id, sizeof(dep->id), bytes + cursor,
                       separator - cursor, false) || !id_valid(dep->id) ||
            !text_copy(dep->minimum_version, sizeof(dep->minimum_version),
                       bytes + separator + 1U, token_end - separator - 1U,
                       false) || !version_valid(dep->minimum_version))
            return false;
        for (uint8_t i = 0U; i < app->dependency_count; ++i) {
            const char *prior = app->dependencies[i].id;
            size_t prior_length = 0U;
            while (prior[prior_length] != '\0') ++prior_length;
            size_t current_length = 0U;
            while (dep->id[current_length] != '\0') ++current_length;
            if (prior_length == current_length) {
                size_t same = 0U;
                while (same < prior_length && prior[same] == dep->id[same])
                    ++same;
                if (same == prior_length) return false;
            }
        }
        ++app->dependency_count;
        cursor = token_end + 1U;
    }
    return app->dependency_count != 0U;
}

bool nexus_app_version_compare(const char *left, const char *right,
                               int *comparison) {
    if (comparison == NULL || !version_valid(left) || !version_valid(right))
        return false;
    size_t li = 0U, ri = 0U;
    for (unsigned part = 0U; part < 3U; ++part) {
        uint32_t lv = 0U, rv = 0U;
        while (left[li] >= '0' && left[li] <= '9') {
            lv = lv * 10U + (uint32_t)(left[li++] - '0');
        }
        while (right[ri] >= '0' && right[ri] <= '9') {
            rv = rv * 10U + (uint32_t)(right[ri++] - '0');
        }
        if (lv < rv) { *comparison = -1; return true; }
        if (lv > rv) { *comparison = 1; return true; }
        if (part != 2U) { ++li; ++ri; }
    }
    *comparison = 0;
    return true;
}

static int id_compare(const char *left, const char *right) {
    size_t i = 0U;
    while (left[i] != '\0' && right[i] != '\0') {
        if (left[i] < right[i]) return -1;
        if (left[i] > right[i]) return 1;
        ++i;
    }
    if (left[i] == right[i]) return 0;
    return left[i] == '\0' ? -1 : 1;
}

int nexus_app_repository_find(const struct nexus_app_repository *repository,
                              const char *app_id) {
    if (repository == NULL || app_id == NULL) return -1;
    size_t low = 0U, high = repository->app_count;
    while (low < high) {
        const size_t middle = low + (high - low) / 2U;
        const int comparison = id_compare(repository->apps[middle].id, app_id);
        if (comparison == 0) return (int)middle;
        if (comparison < 0) low = middle + 1U;
        else high = middle;
    }
    return -1;
}

static bool repo_validate_dependencies(
    const struct nexus_app_repository *repository);

bool nexus_app_repository_parse_verify(
    const void *catalog_bytes, size_t catalog_length,
    nexus_package_signature_verifier verifier, void *verifier_context,
    struct nexus_app_repository *repository) {
    if (catalog_bytes == NULL || repository == NULL || verifier == NULL ||
        catalog_length == 0U ||
        catalog_length > NEXUS_APP_REPOSITORY_MAX_BYTES) return false;
    const uint8_t *bytes = (const uint8_t *)catalog_bytes;
    struct nexus_app_repository parsed = {0};
    size_t offset = 0U, start = 0U, length = 0U;
    char number[16];

    if (!line_next(bytes, catalog_length, &offset, &start, &length) ||
        !field_equals(bytes, start, length, APP_REPOSITORY_MAGIC) ||
        !line_next(bytes, catalog_length, &offset, &start, &length) ||
        !line_value(bytes, start, length, "GENERATION=", number,
                    sizeof(number)) ||
        !parse_u32(number, UINT32_MAX, &parsed.generation) ||
        !line_next(bytes, catalog_length, &offset, &start, &length) ||
        !line_value(bytes, start, length, "COUNT=", number,
                    sizeof(number)) ||
        !parse_u32(number, NEXUS_APP_REPOSITORY_MAX_APPS,
                   &parsed.app_count) || parsed.app_count == 0U ||
        !line_next(bytes, catalog_length, &offset, &start, &length) ||
        !line_value(bytes, start, length, "KEY=", parsed.key_id,
                    sizeof(parsed.key_id)) || !key_valid(parsed.key_id))
        return false;

    for (uint32_t i = 0U; i < parsed.app_count; ++i) {
        if (!line_next(bytes, catalog_length, &offset, &start, &length) ||
            !app_parse(bytes, start, length, &parsed.apps[i])) return false;
        if (i != 0U &&
            id_compare(parsed.apps[i - 1U].id, parsed.apps[i].id) >= 0)
            return false;
    }

    const size_t signed_length = offset;
    if (!line_next(bytes, catalog_length, &offset, &start, &length) ||
        length != sizeof("SIGNATURE=") - 1U +
                  NEXUS_PACKAGE_SIGNATURE_BYTES * 2U ||
        !field_equals(bytes, start, sizeof("SIGNATURE=") - 1U,
                      "SIGNATURE=") || offset != catalog_length)
        return false;
    uint8_t signature[NEXUS_PACKAGE_SIGNATURE_BYTES];
    if (!hex_decode(bytes + start + sizeof("SIGNATURE=") - 1U,
                    length - (sizeof("SIGNATURE=") - 1U),
                    signature, sizeof(signature)) ||
        !verifier(parsed.key_id, bytes, signed_length, signature,
                  verifier_context) ||
        !repo_validate_dependencies(&parsed)) return false;

    *repository = parsed;
    return true;
}

static bool repo_validate_visit(const struct nexus_app_repository *repository,
                                uint8_t index, uint8_t state[
                                    NEXUS_APP_REPOSITORY_MAX_APPS]) {
    if (state[index] == 1U) return false;
    if (state[index] == 2U) return true;
    state[index] = 1U;
    const struct nexus_app_repository_entry *app = &repository->apps[index];
    for (uint8_t i = 0U; i < app->dependency_count; ++i) {
        const int dependency = nexus_app_repository_find(
            repository, app->dependencies[i].id);
        if (dependency < 0) return false;
        int version_order = 0;
        if (!nexus_app_version_compare(
                repository->apps[dependency].version,
                app->dependencies[i].minimum_version, &version_order) ||
            version_order < 0 ||
            !repo_validate_visit(repository, (uint8_t)dependency, state))
            return false;
    }
    state[index] = 2U;
    return true;
}

static bool repo_validate_dependencies(
    const struct nexus_app_repository *repository) {
    uint8_t state[NEXUS_APP_REPOSITORY_MAX_APPS] = {0};
    for (uint8_t i = 0U; i < repository->app_count; ++i)
        if (!repo_validate_visit(repository, i, state)) return false;
    return true;
}

static char ascii_lower(char c) {
    return c >= 'A' && c <= 'Z' ? (char)(c - 'A' + 'a') : c;
}

static bool contains_ci(const char *text, const char *query) {
    if (query == NULL || query[0] == '\0') return true;
    if (text == NULL) return false;
    for (size_t start = 0U; text[start] != '\0'; ++start) {
        size_t i = 0U;
        while (query[i] != '\0' && text[start + i] != '\0' &&
               ascii_lower(text[start + i]) == ascii_lower(query[i])) ++i;
        if (query[i] == '\0') return true;
    }
    return false;
}

static bool equal_ci(const char *left, const char *right) {
    if (left == NULL || right == NULL) return false;
    size_t i = 0U;
    while (left[i] != '\0' && right[i] != '\0') {
        if (ascii_lower(left[i]) != ascii_lower(right[i])) return false;
        ++i;
    }
    return left[i] == right[i];
}

size_t nexus_app_repository_search(
    const struct nexus_app_repository *repository,
    const char *query, const char *category,
    uint8_t *result_indices, size_t result_capacity) {
    if (repository == NULL ||
        (result_capacity != 0U && result_indices == NULL)) return 0U;
    size_t written = 0U;
    for (uint32_t i = 0U; i < repository->app_count; ++i) {
        const struct nexus_app_repository_entry *app = &repository->apps[i];
        if (category != NULL && category[0] != '\0' &&
            !equal_ci(app->category, category)) continue;
        if (!contains_ci(app->id, query) &&
            !contains_ci(app->name, query) &&
            !contains_ci(app->publisher, query) &&
            !contains_ci(app->description, query)) continue;
        if (written < result_capacity) result_indices[written++] = (uint8_t)i;
    }
    return written;
}

struct plan_context {
    const struct nexus_app_repository *repository;
    nexus_app_installed_version_fn installed_version;
    void *installed_context;
    struct nexus_app_install_plan *plan;
    uint8_t visiting[NEXUS_APP_REPOSITORY_MAX_APPS];
};

static bool plan_contains(const struct nexus_app_install_plan *plan,
                          uint8_t index) {
    for (uint8_t i = 0U; i < plan->step_count; ++i)
        if (plan->steps[i].repository_index == index) return true;
    return false;
}

static bool plan_visit(struct plan_context *context, uint8_t index,
                       const char *required_version) {
    if (context->visiting[index] != 0U) return false;
    const struct nexus_app_repository_entry *app =
        &context->repository->apps[index];
    char installed[ NEXUS_PACKAGE_VERSION_MAX + 1U ] = {0};
    const enum nexus_app_installed_lookup lookup =
        context->installed_version(app->id, installed, sizeof(installed),
                                   context->installed_context);
    if (lookup == NEXUS_APP_INSTALLED_LOOKUP_ERROR) return false;
    bool needs_install = lookup == NEXUS_APP_NOT_INSTALLED;
    if (!needs_install) {
        int order = 0;
        if (!nexus_app_version_compare(installed, required_version, &order))
            return false;
        needs_install = order < 0;
    }
    if (!needs_install) return true;
    if (needs_install && plan_contains(context->plan, index)) return true;

    context->visiting[index] = 1U;
    for (uint8_t i = 0U; i < app->dependency_count; ++i) {
        const struct nexus_app_dependency *dependency = &app->dependencies[i];
        const int dependency_index = nexus_app_repository_find(
            context->repository, dependency->id);
        if (dependency_index < 0 ||
            !plan_visit(context, (uint8_t)dependency_index,
                        dependency->minimum_version)) {
            context->visiting[index] = 0U;
            return false;
        }
    }
    context->visiting[index] = 0U;

    if (needs_install && !plan_contains(context->plan, index)) {
        if (context->plan->step_count >= NEXUS_APP_REPOSITORY_MAX_APPS)
            return false;
        struct nexus_app_install_step *step =
            &context->plan->steps[context->plan->step_count++];
        step->repository_index = index;
        step->is_update = lookup == NEXUS_APP_INSTALLED;
    }
    return true;
}

bool nexus_app_repository_plan_install(
    const struct nexus_app_repository *repository, const char *app_id,
    nexus_app_installed_version_fn installed_version, void *context,
    struct nexus_app_install_plan *plan) {
    if (repository == NULL || app_id == NULL || installed_version == NULL ||
        plan == NULL || repository->app_count == 0U ||
        repository->app_count > NEXUS_APP_REPOSITORY_MAX_APPS) return false;
    const int root = nexus_app_repository_find(repository, app_id);
    if (root < 0) return false;
    *plan = (struct nexus_app_install_plan){0};
    struct plan_context planner = {
        .repository = repository,
        .installed_version = installed_version,
        .installed_context = context,
        .plan = plan
    };
    return plan_visit(&planner, (uint8_t)root,
                      repository->apps[root].version);
}
