#include "calculator.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static void value(const char *s,int64_t expected) {
    int64_t out=777;
    assert(nexus_calculator_evaluate(s,&out)==NEXUS_CALCULATOR_OK);
    assert(out==expected);
}
static void error(const char *s,enum nexus_calculator_error expected) {
    int64_t out=777;
    assert(nexus_calculator_evaluate(s,&out)==expected);
    assert(out==777);
}
int main(void) {
    value("2+3*4",14); value("(2+3)*4",20); value("18/3*2",12);
    value("20-6-4",10); value("40/(3+2)",8);
    value("2*(3+(4*5))",46); value(" 7 + 8 * ( 9 - 6 ) ",31);
    value("-7",-7); value("+7",7); value("--7",7); value("-(3+4)",-7);
    value("-9223372036854775808",INT64_MIN); value("(-9)%4",-1);
    value("9%-4",1); value("0-9223372036854775807",-INT64_MAX);
    error("",NEXUS_CALCULATOR_EMPTY); error("()",NEXUS_CALCULATOR_SYNTAX);
    error("1+",NEXUS_CALCULATOR_SYNTAX); error("1 2",NEXUS_CALCULATOR_SYNTAX);
    error("(1+2",NEXUS_CALCULATOR_SYNTAX); error("1+2)",NEXUS_CALCULATOR_SYNTAX);
    error("2.5+1",NEXUS_CALCULATOR_SYNTAX); error("abc",NEXUS_CALCULATOR_SYNTAX);
    error("5/0",NEXUS_CALCULATOR_DIVIDE_BY_ZERO);
    error("5%0",NEXUS_CALCULATOR_DIVIDE_BY_ZERO);
    error("9223372036854775807+1",NEXUS_CALCULATOR_OVERFLOW);
    error("-9223372036854775808-1",NEXUS_CALCULATOR_OVERFLOW);
    error("3037000500*3037000500",NEXUS_CALCULATOR_OVERFLOW);
    error("-9223372036854775808/-1",NEXUS_CALCULATOR_OVERFLOW);
    error("-9223372036854775808%-1",NEXUS_CALCULATOR_OVERFLOW);
    error("99999999999999999999",NEXUS_CALCULATOR_OVERFLOW);
    error("((((((((((((((((((1))))))))))))))))))",NEXUS_CALCULATOR_TOO_DEEP);
    char long_text[NEXUS_CALCULATOR_EXPRESSION_MAX+2U];
    memset(long_text,'1',sizeof(long_text)-1U); long_text[sizeof(long_text)-1U]='\0';
    error(long_text,NEXUS_CALCULATOR_TOO_LONG);
    assert(strcmp(nexus_calculator_error_name(NEXUS_CALCULATOR_DIVIDE_BY_ZERO),
                  "DIVISION BY ZERO")==0);
    puts("Nexus calculator engine tests passed"); return 0;
}
