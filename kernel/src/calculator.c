#include "calculator.h"

#include <stdint.h>

struct calculator_parser {
    const char *input;
    size_t length;
    size_t position;
    enum nexus_calculator_error error;
};

static bool parser_fail(struct calculator_parser *parser,
                        enum nexus_calculator_error error) {
    if (parser->error == NEXUS_CALCULATOR_ERROR_NONE)
        parser->error = error;
    return false;
}

static void skip_space(struct calculator_parser *parser) {
    while (parser->position < parser->length) {
        const char ch = parser->input[parser->position];
        if (ch != ' ' && ch != '\t' && ch != '\r' && ch != '\n') break;
        ++parser->position;
    }
}

static bool parse_expression(struct calculator_parser *parser,
                             int64_t *result);

static bool checked_add(int64_t left, int64_t right, int64_t *result) {
    if ((right > 0 && left > INT64_MAX - right) ||
        (right < 0 && left < INT64_MIN - right)) return false;
    *result = left + right;
    return true;
}

static bool checked_subtract(int64_t left, int64_t right,
                             int64_t *result) {
    if ((right < 0 && left > INT64_MAX + right) ||
        (right > 0 && left < INT64_MIN + right)) return false;
    *result = left - right;
    return true;
}

static bool checked_multiply(int64_t left, int64_t right, int64_t *result) {
    if (left == 0 || right == 0) {
        *result = 0;
        return true;
    }
    if ((left == -1 && right == INT64_MIN) ||
        (right == -1 && left == INT64_MIN)) return false;
    if (left > 0) {
        if (right > 0) {
            if (left > INT64_MAX / right) return false;
        } else if (right < INT64_MIN / left) {
            return false;
        }
    } else if (right > 0) {
        if (left < INT64_MIN / right) return false;
    } else if (left < INT64_MAX / right) {
        return false;
    }
    *result = left * right;
    return true;
}

static bool parse_primary(struct calculator_parser *parser, int64_t *result) {
    skip_space(parser);
    if (parser->position >= parser->length)
        return parser_fail(parser, NEXUS_CALCULATOR_ERROR_SYNTAX);

    const char current = parser->input[parser->position];
    if (current == '(') {
        ++parser->position;
        if (!parse_expression(parser, result)) return false;
        skip_space(parser);
        if (parser->position >= parser->length ||
            parser->input[parser->position] != ')')
            return parser_fail(parser, NEXUS_CALCULATOR_ERROR_SYNTAX);
        ++parser->position;
        return true;
    }
    if (current < '0' || current > '9')
        return parser_fail(parser, NEXUS_CALCULATOR_ERROR_SYNTAX);

    int64_t value = 0;
    while (parser->position < parser->length) {
        const char ch = parser->input[parser->position];
        if (ch < '0' || ch > '9') break;
        const int64_t digit = (int64_t)(ch - '0');
        if (value > (INT64_MAX - digit) / 10)
            return parser_fail(parser, NEXUS_CALCULATOR_ERROR_OVERFLOW);
        value = value * 10 + digit;
        ++parser->position;
    }
    *result = value;
    return true;
}

static bool parse_unary(struct calculator_parser *parser, int64_t *result) {
    skip_space(parser);
    if (parser->position < parser->length) {
        const char op = parser->input[parser->position];
        if (op == '+' || op == '-') {
            ++parser->position;
            if (!parse_unary(parser, result)) return false;
            if (op == '-') {
                if (*result == INT64_MIN)
                    return parser_fail(parser,
                                       NEXUS_CALCULATOR_ERROR_OVERFLOW);
                *result = -*result;
            }
            return true;
        }
    }
    return parse_primary(parser, result);
}

static bool parse_term(struct calculator_parser *parser, int64_t *result) {
    if (!parse_unary(parser, result)) return false;
    for (;;) {
        skip_space(parser);
        if (parser->position >= parser->length) return true;
        const char op = parser->input[parser->position];
        if (op != '*' && op != '/' && op != '%') return true;
        ++parser->position;
        int64_t right = 0;
        if (!parse_unary(parser, &right)) return false;

        int64_t next = 0;
        if (op == '*') {
            if (!checked_multiply(*result, right, &next))
                return parser_fail(parser, NEXUS_CALCULATOR_ERROR_OVERFLOW);
        } else {
            if (right == 0)
                return parser_fail(parser,
                                   NEXUS_CALCULATOR_ERROR_DIVIDE_BY_ZERO);
            if (*result == INT64_MIN && right == -1) {
                if (op == '/')
                    return parser_fail(parser,
                                       NEXUS_CALCULATOR_ERROR_OVERFLOW);
                next = 0;
            } else {
                next = op == '/' ? *result / right : *result % right;
            }
        }
        *result = next;
    }
}

static bool parse_expression(struct calculator_parser *parser,
                             int64_t *result) {
    if (!parse_term(parser, result)) return false;
    for (;;) {
        skip_space(parser);
        if (parser->position >= parser->length) return true;
        const char op = parser->input[parser->position];
        if (op != '+' && op != '-') return true;
        ++parser->position;
        int64_t right = 0;
        if (!parse_term(parser, &right)) return false;

        int64_t next = 0;
        const bool valid = op == '+'
            ? checked_add(*result, right, &next)
            : checked_subtract(*result, right, &next);
        if (!valid)
            return parser_fail(parser, NEXUS_CALCULATOR_ERROR_OVERFLOW);
        *result = next;
    }
}

bool nexus_calculator_eval(const char *expression, size_t length,
                           int64_t *result,
                           enum nexus_calculator_error *error) {
    if (error != NULL) *error = NEXUS_CALCULATOR_ERROR_NONE;
    if (expression == NULL || result == NULL) {
        if (error != NULL)
            *error = NEXUS_CALCULATOR_ERROR_INVALID_ARGUMENT;
        return false;
    }
    if (length == 0U) {
        if (error != NULL) *error = NEXUS_CALCULATOR_ERROR_EMPTY;
        return false;
    }

    struct calculator_parser parser = {
        .input = expression,
        .length = length,
        .position = 0U,
        .error = NEXUS_CALCULATOR_ERROR_NONE
    };
    int64_t value = 0;
    if (!parse_expression(&parser, &value)) {
        if (error != NULL) *error = parser.error;
        return false;
    }
    skip_space(&parser);
    if (parser.position != parser.length) {
        if (error != NULL) *error = NEXUS_CALCULATOR_ERROR_SYNTAX;
        return false;
    }
    *result = value;
    return true;
}
