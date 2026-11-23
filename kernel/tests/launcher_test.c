#include "launcher.h"

#include <assert.h>
#include <stddef.h>
#include <stdio.h>

int main(void) {
    static const char *const apps[] = {
        "Files", "Nexus Browser", "Settings", "Terminal",
        "System Monitor", "Control Center", "Calculator"
    };
    size_t results[7];

    assert(nexus_launcher_filter("",apps,7U,results,7U)==7U);
    for(size_t i=0U;i<7U;++i) assert(results[i]==i);

    assert(nexus_launcher_filter("set",apps,7U,results,7U)==1U);
    assert(results[0]==2U);
    assert(nexus_launcher_filter("NEXUS",apps,7U,results,7U)==1U);
    assert(results[0]==1U);
    assert(nexus_launcher_filter("calc",apps,7U,results,7U)==1U);
    assert(results[0]==6U);
    assert(nexus_launcher_filter("control",apps,7U,results,7U)==1U);
    assert(results[0]==5U);
    assert(nexus_launcher_filter("not installed",apps,7U,results,7U)==0U);

    assert(nexus_launcher_filter("",apps,7U,results,3U)==3U);
    assert(results[0]==0U && results[1]==1U && results[2]==2U);
    assert(nexus_launcher_filter("files",NULL,7U,results,7U)==0U);
    assert(nexus_launcher_filter("files",apps,7U,NULL,7U)==0U);
    assert(nexus_launcher_filter("files",apps,7U,results,0U)==0U);

    puts("Nexus launcher search tests passed");
    return 0;
}
