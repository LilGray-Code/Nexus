#include "calculator.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

static void expect_value(const char *expression, int64_t expected) {
    int64_t actual = 0;
    enum nexus_calculator_error error = NEXUS_CALCULATOR_ERROR_INVALID_ARGUMENT;
    assert(nexus_calculator_eval(expression, strlen(expression),
                                 &actual, &error));
    assert(actual == expected);
    assert(error == NEXUS_CALCULATOR_ERROR_NONE);
}

static void expect_error(const char *expression,
                         enum nexus_calculator_error expected) {
    int64_t actual = 0;
    enum nexus_calculator_error error = NEXUS_CALCULATOR_ERROR_NONE;
    assert(!nexus_calculator_eval(expression, strlen(expression),
                                  &actual, &error));
    assert(error == expected);
}

int main(void) {
    expect_value("2 + 3 * 4", 14);
    expect_value("(2 + 3) * 4", 20);
    expect_value(" 18 / 3 - 4 ", 2);
    expect_value("--3 + 5", 8);
    expect_value("20 % 6", 2);
    expect_value("-9 / 2", -4);
    expect_value("2 * (3 + (4 * 5))", 46);
    expect_value("9223372036854775807 * -1",
                 -INT64_C(9223372036854775807));
    expect_error("", NEXUS_CALCULATOR_ERROR_EMPTY);
    expect_error("1 / 0", NEXUS_CALCULATOR_ERROR_DIVIDE_BY_ZERO);
    expect_error("9223372036854775807 + 1",
                 NEXUS_CALCULATOR_ERROR_OVERFLOW);
    expect_error("3037000500 * 3037000500",
                 NEXUS_CALCULATOR_ERROR_OVERFLOW);
    expect_error("9223372036854775808",
                 NEXUS_CALCULATOR_ERROR_OVERFLOW);
    expect_error("1 +", NEXUS_CALCULATOR_ERROR_SYNTAX);
    expect_error("(1 + 2", NEXUS_CALCULATOR_ERROR_SYNTAX);
    expect_error("1 2", NEXUS_CALCULATOR_ERROR_SYNTAX);
    expect_error("1.5", NEXUS_CALCULATOR_ERROR_SYNTAX);
    expect_error("-(-9223372036854775807 - 1)",
                 NEXUS_CALCULATOR_ERROR_OVERFLOW);

    int64_t result = 0;
    enum nexus_calculator_error error = NEXUS_CALCULATOR_ERROR_NONE;
    assert(!nexus_calculator_eval(NULL, 0U, &result, &error));
    assert(error == NEXUS_CALCULATOR_ERROR_INVALID_ARGUMENT);
    assert(!nexus_calculator_eval("1", 1U, NULL, &error));
    assert(error == NEXUS_CALCULATOR_ERROR_INVALID_ARGUMENT);
    return 0;
}
