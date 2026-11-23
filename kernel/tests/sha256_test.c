#include "sha256.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void expect_hex(const uint8_t *bytes,const char *hex,size_t n){
    static const char digits[]="0123456789abcdef";
    char out[129];
    assert(n<=64U);
    for(size_t i=0U;i<n;++i){out[i*2U]=digits[bytes[i]>>4];out[i*2U+1U]=digits[bytes[i]&15U];}
    out[n*2U]='\0';assert(strcmp(out,hex)==0);
}
int main(void){
    uint8_t digest[32];
    nexus_sha256("abc",3U,digest);
    expect_hex(digest,"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",32U);
    uint8_t key[20];memset(key,0x0b,sizeof(key));
    nexus_hmac_sha256(key,sizeof(key),"Hi There",8U,digest);
    expect_hex(digest,"b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7",32U);
    uint8_t ikm[22];memset(ikm,0x0b,sizeof(ikm));
    uint8_t salt[13],info[10],okm[42];
    for(size_t i=0U;i<sizeof(salt);++i)salt[i]=(uint8_t)i;
    for(size_t i=0U;i<sizeof(info);++i)info[i]=(uint8_t)(0xf0U+i);
    assert(nexus_hkdf_sha256(salt,sizeof(salt),ikm,sizeof(ikm),info,sizeof(info),okm,sizeof(okm)));
    expect_hex(okm,"3cb25f25faacd57a90434f64d0362f2a2d2d0a90cf1a5a4c5db02d56ecc4c5bf34007208d5b887185865",42U);
    assert(!nexus_hkdf_sha256(NULL,0U,NULL,0U,NULL,0U,okm,8161U));
    puts("NEXUS SHA-256/HMAC/HKDF TESTS PASSED");
    return 0;
}
