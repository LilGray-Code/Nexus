#ifndef NEXUS_CALCULATOR_H
#define NEXUS_CALCULATOR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum nexus_calculator_error {
    NEXUS_CALCULATOR_ERROR_NONE = 0,
    NEXUS_CALCULATOR_ERROR_EMPTY,
    NEXUS_CALCULATOR_ERROR_SYNTAX,
    NEXUS_CALCULATOR_ERROR_DIVIDE_BY_ZERO,
    NEXUS_CALCULATOR_ERROR_OVERFLOW,
    NEXUS_CALCULATOR_ERROR_INVALID_ARGUMENT
};

bool nexus_calculator_eval(const char *expression, size_t length,
                           int64_t *result,
                           enum nexus_calculator_error *error);

#endif
