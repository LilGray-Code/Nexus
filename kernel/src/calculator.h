#ifndef NEXUS_CALCULATOR_H
#define NEXUS_CALCULATOR_H
#include <stdint.h>
#define NEXUS_CALCULATOR_EXPRESSION_MAX 96U
enum nexus_calculator_error {
    NEXUS_CALCULATOR_OK = 0, NEXUS_CALCULATOR_EMPTY,
    NEXUS_CALCULATOR_SYNTAX, NEXUS_CALCULATOR_DIVIDE_BY_ZERO,
    NEXUS_CALCULATOR_OVERFLOW, NEXUS_CALCULATOR_TOO_LONG,
    NEXUS_CALCULATOR_TOO_DEEP
};
enum nexus_calculator_error nexus_calculator_evaluate(
    const char *expression, int64_t *result);
const char *nexus_calculator_error_name(enum nexus_calculator_error error);
#endif
