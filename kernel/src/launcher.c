#include "launcher.h"

static char ascii_lower(char ch) {
    if(ch>='A' && ch<='Z') return (char)(ch-'A'+'a');
    return ch;
}

static int contains_ascii_case_insensitive(const char *text,
                                           const char *query) {
    if(text==NULL || query==NULL) return 0;
    if(query[0]=='\0') return 1;
    for(size_t start=0U;text[start]!='\0';++start) {
        size_t offset=0U;
        while(query[offset]!='\0' && text[start+offset]!='\0' &&
              ascii_lower(text[start+offset])==ascii_lower(query[offset]))
            ++offset;
        if(query[offset]=='\0') return 1;
    }
    return 0;
}

size_t nexus_launcher_filter(const char *query,
                             const char *const *app_names,
                             size_t app_count,
                             size_t *results,
                             size_t capacity) {
    if(app_names==NULL || results==NULL || capacity==0U) return 0U;
    if(query==NULL) query="";
    size_t count=0U;
    for(size_t i=0U;i<app_count && count<capacity;++i) {
        if(contains_ascii_case_insensitive(app_names[i],query))
            results[count++]=i;
    }
    return count;
}
