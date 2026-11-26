#include "app_repository.h"
#include "sha256.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void hex_encode(const uint8_t *bytes, size_t length, char *out) {
    static const char digits[] = "0123456789abcdef";
    for (size_t i = 0U; i < length; ++i) {
        out[i * 2U] = digits[bytes[i] >> 4];
        out[i * 2U + 1U] = digits[bytes[i] & 15U];
    }
    out[length * 2U] = '\0';
}

static bool test_verify(const char *key, const uint8_t *message, size_t length,
                        const uint8_t signature[64], void *context) {
    (void)context;
    if (key == NULL || strcmp(key, "repo-test") != 0 ||
        message == NULL || signature == NULL || length == 0U) return false;
    uint8_t digest[32];
    nexus_sha256(message, length, digest);
    return memcmp(digest, signature, 32U) == 0 &&
           memcmp(digest, signature + 32U, 32U) == 0;
}

static size_t make_repo(char *output, size_t capacity,
                        const char *app_lines, uint32_t count) {
    char prefix[12000];
    const int prefix_length = snprintf(prefix, sizeof(prefix),
        "NEXUS-APP-REPOSITORY/1\nGENERATION=9\nCOUNT=%u\nKEY=repo-test\n%s",
        count, app_lines);
    assert(prefix_length > 0 && (size_t)prefix_length < sizeof(prefix));
    uint8_t signature[64];
    nexus_sha256(prefix, (size_t)prefix_length, signature);
    memcpy(signature + 32U, signature, 32U);
    char signature_hex[129];
    hex_encode(signature, sizeof(signature), signature_hex);
    const int result = snprintf(output, capacity, "%sSIGNATURE=%s\n",
                                prefix, signature_hex);
    assert(result > prefix_length && (size_t)result < capacity);
    return (size_t)result;
}

static const char *const valid_apps =
    "APP=CORE|1.2.0|8388608|https://repo.example/core.nup|"
    "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef|"
    "Nexus Core Runtime|Nexus|SYSTEM|Shared base runtime for Nexus apps||"
    "0|0|-\n"
    "APP=EDITOR|2.1.0|9437184|https://repo.example/editor.nup|"
    "abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789|"
    "Nexus Editor|Nexus Team|DEVELOPMENT|A lightweight code editor|"
    "https://repo.example/editor.png|47|125|CORE@1.1.0\n"
    "APP=PAINT|1.0.0|2097152|https://repo.example/paint.nup|"
    "00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff|"
    "Nexus Paint|Nexus Team|GRAPHICS|Draw and edit images||0|0|CORE@1.0.0\n";

struct installed_fixture {
    const char *ids[4];
    const char *versions[4];
    size_t count;
};

static enum nexus_app_installed_lookup installed_lookup(
    const char *id, char *version, size_t capacity, void *context) {
    const struct installed_fixture *fixture =
        (const struct installed_fixture *)context;
    for (size_t i = 0U; i < fixture->count; ++i) {
        if (strcmp(id, fixture->ids[i]) == 0) {
            const size_t length = strlen(fixture->versions[i]);
            if (length >= capacity) return NEXUS_APP_INSTALLED_LOOKUP_ERROR;
            memcpy(version, fixture->versions[i], length + 1U);
            return NEXUS_APP_INSTALLED;
        }
    }
    return NEXUS_APP_NOT_INSTALLED;
}

static void test_signed_repository_metadata_and_search(void) {
    char bytes[NEXUS_APP_REPOSITORY_MAX_BYTES];
    const size_t length = make_repo(bytes, sizeof(bytes), valid_apps, 3U);
    struct nexus_app_repository repository;
    assert(nexus_app_repository_parse_verify(bytes, length, test_verify,
                                               NULL, &repository));
    assert(repository.generation == 9U && repository.app_count == 3U);
    assert(strcmp(repository.apps[1].name, "Nexus Editor") == 0);
    assert(strcmp(repository.apps[1].category, "DEVELOPMENT") == 0);
    assert(repository.apps[1].rating_tenths == 47U);
    assert(repository.apps[1].rating_count == 125U);
    assert(strcmp(repository.apps[1].screenshot_url,
                  "https://repo.example/editor.png") == 0);
    assert(repository.apps[1].package_size == 9437184U);
    assert(repository.apps[1].dependency_count == 1U);
    assert(strcmp(repository.apps[1].dependencies[0].id, "CORE") == 0);

    uint8_t indices[4] = {0};
    assert(nexus_app_repository_search(&repository, "editor", NULL,
                                       indices, 4U) == 1U);
    assert(indices[0] == 1U);
    assert(nexus_app_repository_search(&repository, "nexus", "GRAPHICS",
                                       indices, 4U) == 1U);
    assert(indices[0] == 2U);
    assert(nexus_app_repository_search(&repository, "", NULL,
                                       indices, 2U) == 2U);
    assert(nexus_app_repository_find(&repository, "PAINT") == 2);
    assert(nexus_app_repository_find(&repository, "MISSING") == -1);
}

static void test_dependency_plan_and_update(void) {
    char bytes[NEXUS_APP_REPOSITORY_MAX_BYTES];
    const size_t length = make_repo(bytes, sizeof(bytes), valid_apps, 3U);
    struct nexus_app_repository repository;
    assert(nexus_app_repository_parse_verify(bytes, length, test_verify,
                                               NULL, &repository));
    struct installed_fixture installed = {
        .ids = {"CORE"}, .versions = {"1.0.0"}, .count = 1U
    };
    struct nexus_app_install_plan plan;
    assert(nexus_app_repository_plan_install(&repository, "EDITOR",
        installed_lookup, &installed, &plan));
    assert(plan.step_count == 2U);
    assert(plan.steps[0].repository_index == 0U);
    assert(plan.steps[0].is_update);
    assert(plan.steps[1].repository_index == 1U);
    assert(!plan.steps[1].is_update);

    installed.versions[0] = "1.1.0";
    assert(nexus_app_repository_plan_install(&repository, "EDITOR",
        installed_lookup, &installed, &plan));
    assert(plan.step_count == 1U && plan.steps[0].repository_index == 1U);

    installed.ids[1] = "EDITOR";
    installed.versions[1] = "2.1.0";
    installed.count = 2U;
    assert(nexus_app_repository_plan_install(&repository, "EDITOR",
        installed_lookup, &installed, &plan));
    assert(plan.step_count == 0U);
}

static void test_version_ordering(void) {
    int order = 0;
    assert(nexus_app_version_compare("1.10.0", "1.9.9", &order));
    assert(order > 0);
    assert(nexus_app_version_compare("2.0.0", "2.0.0", &order));
    assert(order == 0);
    assert(!nexus_app_version_compare("1.0", "1.0.0", &order));
}

static void test_invalid_repository_is_never_exposed(void) {
    char bytes[NEXUS_APP_REPOSITORY_MAX_BYTES];
    size_t length = make_repo(bytes, sizeof(bytes), valid_apps, 3U);
    struct nexus_app_repository repository;
    char *url = strstr(bytes, "https://repo.example/editor.nup");
    assert(url != NULL);
    url[0] = 'x'; /* invalidates the signed catalog bytes */
    assert(!nexus_app_repository_parse_verify(bytes, length, test_verify,
                                               NULL, &repository));

    static const char missing_dependency[] =
        "APP=EDITOR|1.0.0|100|https://repo.example/editor.nup|"
        "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef|"
        "Editor|Nexus|DEVELOPMENT|Editor package||0|0|MISSING@1.0.0\n";
    length = make_repo(bytes, sizeof(bytes), missing_dependency, 1U);
    assert(!nexus_app_repository_parse_verify(bytes, length, test_verify,
                                               NULL, &repository));

    static const char cyclic_apps[] =
        "APP=CORE|1.0.0|100|https://repo.example/core.nup|"
        "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef|"
        "Core|Nexus|SYSTEM|Core runtime||0|0|EDITOR@1.0.0\n"
        "APP=EDITOR|1.0.0|100|https://repo.example/editor.nup|"
        "abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789|"
        "Editor|Nexus|DEVELOPMENT|Editor package||0|0|CORE@1.0.0\n";
    length = make_repo(bytes, sizeof(bytes), cyclic_apps, 2U);
    assert(!nexus_app_repository_parse_verify(bytes, length, test_verify,
                                               NULL, &repository));
}

int main(void) {
    test_signed_repository_metadata_and_search();
    test_dependency_plan_and_update();
    test_version_ordering();
    test_invalid_repository_is_never_exposed();
    puts("NEXUS APP REPOSITORY AND DEPENDENCY TESTS PASSED");
    return 0;
}
