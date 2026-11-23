#include "calculator.h"
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#define CALC_MAX_DEPTH 16U
struct parser { const char *text; size_t pos; enum nexus_calculator_error error; };

static void spaces(struct parser *p) {
    while (p->text[p->pos]==' ' || p->text[p->pos]=='\t' ||
           p->text[p->pos]=='\n' || p->text[p->pos]=='\r') ++p->pos;
}
static bool add_checked(int64_t a,int64_t b,int64_t *o) {
    if ((b>0 && a>INT64_MAX-b)||(b<0 && a<INT64_MIN-b)) return false;
    *o=a+b; return true;
}
static bool sub_checked(int64_t a,int64_t b,int64_t *o) {
    if ((b<0 && a>INT64_MAX+b)||(b>0 && a<INT64_MIN+b)) return false;
    *o=a-b; return true;
}
static bool mul_checked(int64_t a,int64_t b,int64_t *o) {
    if (a==0 || b==0) { *o=0; return true; }
    if ((a==-1 && b==INT64_MIN)||(b==-1 && a==INT64_MIN)) return false;
    if (a>0) {
        if ((b>0 && a>INT64_MAX/b)||(b<0 && b<INT64_MIN/a)) return false;
    } else {
        if ((b>0 && a<INT64_MIN/b)||(b<0 && a<INT64_MAX/b)) return false;
    }
    *o=a*b; return true;
}
static bool expression(struct parser *p,unsigned depth,int64_t *v);
static bool magnitude(struct parser *p,uint64_t limit,uint64_t *v) {
    spaces(p); size_t start=p->pos; uint64_t n=0U;
    while (p->text[p->pos]>='0' && p->text[p->pos]<='9') {
        uint32_t digit=(uint32_t)(p->text[p->pos]-'0');
        if (digit>limit || n>(limit-digit)/10U) {
            p->error=NEXUS_CALCULATOR_OVERFLOW; return false;
        }
        n=n*10U+digit; ++p->pos;
    }
    if (p->pos==start) { p->error=NEXUS_CALCULATOR_SYNTAX; return false; }
    *v=n; return true;
}
static bool primary(struct parser *p,unsigned depth,int64_t *v) {
    spaces(p);
    if (p->text[p->pos]=='(') {
        if (depth>=CALC_MAX_DEPTH) { p->error=NEXUS_CALCULATOR_TOO_DEEP; return false; }
        ++p->pos;
        if (!expression(p,depth+1U,v)) return false;
        spaces(p);
        if (p->text[p->pos]!=')') { p->error=NEXUS_CALCULATOR_SYNTAX; return false; }
        ++p->pos; return true;
    }
    uint64_t n=0U;
    if (!magnitude(p,(uint64_t)INT64_MAX,&n)) return false;
    *v=(int64_t)n; return true;
}
static bool unary(struct parser *p,unsigned depth,int64_t *v) {
    spaces(p); char op=p->text[p->pos];
    if (op!='+' && op!='-') return primary(p,depth,v);
    if (depth>=CALC_MAX_DEPTH) { p->error=NEXUS_CALCULATOR_TOO_DEEP; return false; }
    ++p->pos; spaces(p);
    if (op=='-' && p->text[p->pos]>='0' && p->text[p->pos]<='9') {
        uint64_t n=0U, minmag=(uint64_t)INT64_MAX+1U;
        if (!magnitude(p,minmag,&n)) return false;
        *v=n==minmag ? INT64_MIN : -(int64_t)n; return true;
    }
    if (!unary(p,depth+1U,v)) return false;
    if (op=='-') {
        if (*v==INT64_MIN) { p->error=NEXUS_CALCULATOR_OVERFLOW; return false; }
        *v=-*v;
    }
    return true;
}
static bool term(struct parser *p,unsigned depth,int64_t *v) {
    if (!unary(p,depth,v)) return false;
    for (;;) {
        spaces(p); char op=p->text[p->pos];
        if (op!='*' && op!='/' && op!='%') return true;
        ++p->pos; int64_t rhs=0;
        if (!unary(p,depth,&rhs)) return false;
        if (op=='*') {
            if (!mul_checked(*v,rhs,v)) { p->error=NEXUS_CALCULATOR_OVERFLOW; return false; }
        } else {
            if (rhs==0) { p->error=NEXUS_CALCULATOR_DIVIDE_BY_ZERO; return false; }
            if (*v==INT64_MIN && rhs==-1) { p->error=NEXUS_CALCULATOR_OVERFLOW; return false; }
            *v=op=='/' ? *v/rhs : *v%rhs;
        }
    }
}
static bool expression(struct parser *p,unsigned depth,int64_t *v) {
    if (!term(p,depth,v)) return false;
    for (;;) {
        spaces(p); char op=p->text[p->pos];
        if (op!='+' && op!='-') return true;
        ++p->pos; int64_t rhs=0;
        if (!term(p,depth,&rhs)) return false;
        bool ok=op=='+' ? add_checked(*v,rhs,v) : sub_checked(*v,rhs,v);
        if (!ok) { p->error=NEXUS_CALCULATOR_OVERFLOW; return false; }
    }
}
enum nexus_calculator_error nexus_calculator_evaluate(const char *s,int64_t *out) {
    if (!s || !out) return NEXUS_CALCULATOR_SYNTAX;
    size_t n=0U;
    while (s[n]!='\0' && n<=NEXUS_CALCULATOR_EXPRESSION_MAX) ++n;
    if (n==0U) return NEXUS_CALCULATOR_EMPTY;
    if (n>NEXUS_CALCULATOR_EXPRESSION_MAX) return NEXUS_CALCULATOR_TOO_LONG;
    struct parser p={.text=s,.pos=0U,.error=NEXUS_CALCULATOR_OK};
    int64_t result=0;
    if (!expression(&p,0U,&result))
        return p.error==NEXUS_CALCULATOR_OK ? NEXUS_CALCULATOR_SYNTAX : p.error;
    spaces(&p);
    if (p.text[p.pos]!='\0') return NEXUS_CALCULATOR_SYNTAX;
    *out=result; return NEXUS_CALCULATOR_OK;
}
const char *nexus_calculator_error_name(enum nexus_calculator_error e) {
    switch(e) {
        case NEXUS_CALCULATOR_OK:return "OK";
        case NEXUS_CALCULATOR_EMPTY:return "EMPTY EXPRESSION";
        case NEXUS_CALCULATOR_SYNTAX:return "INVALID EXPRESSION";
        case NEXUS_CALCULATOR_DIVIDE_BY_ZERO:return "DIVISION BY ZERO";
        case NEXUS_CALCULATOR_OVERFLOW:return "INTEGER OVERFLOW";
        case NEXUS_CALCULATOR_TOO_LONG:return "EXPRESSION TOO LONG";
        case NEXUS_CALCULATOR_TOO_DEEP:return "NESTING LIMIT EXCEEDED";
        default:return "UNKNOWN CALCULATOR ERROR";
    }
}
