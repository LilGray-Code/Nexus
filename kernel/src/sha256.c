#include "sha256.h"

static const uint32_t round_constants[64] = {
    0x428A2F98U,0x71374491U,0xB5C0FBCFU,0xE9B5DBA5U,
    0x3956C25BU,0x59F111F1U,0x923F82A4U,0xAB1C5ED5U,
    0xD807AA98U,0x12835B01U,0x243185BEU,0x550C7DC3U,
    0x72BE5D74U,0x80DEB1FEU,0x9BDC06A7U,0xC19BF174U,
    0xE49B69C1U,0xEFBE4786U,0x0FC19DC6U,0x240CA1CCU,
    0x2DE92C6FU,0x4A7484AAU,0x5CB0A9DCU,0x76F988DAU,
    0x983E5152U,0xA831C66DU,0xB00327C8U,0xBF597FC7U,
    0xC6E00BF3U,0xD5A79147U,0x06CA6351U,0x14292967U,
    0x27B70A85U,0x2E1B2138U,0x4D2C6DFCU,0x53380D13U,
    0x650A7354U,0x766A0ABBU,0x81C2C92EU,0x92722C85U,
    0xA2BFE8A1U,0xA81A664BU,0xC24B8B70U,0xC76C51A3U,
    0xD192E819U,0xD6990624U,0xF40E3585U,0x106AA070U,
    0x19A4C116U,0x1E376C08U,0x2748774CU,0x34B0BCB5U,
    0x391C0CB3U,0x4ED8AA4AU,0x5B9CCA4FU,0x682E6FF3U,
    0x748F82EEU,0x78A5636FU,0x84C87814U,0x8CC70208U,
    0x90BEFFFAU,0xA4506CEBU,0xBEF9A3F7U,0xC67178F2U
};

static uint32_t rotate_right(uint32_t value, unsigned amount) {
    return (value >> amount) | (value << (32U - amount));
}

static uint32_t read_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static void write_be32(uint8_t *p, uint32_t value) {
    p[0]=(uint8_t)(value>>24); p[1]=(uint8_t)(value>>16);
    p[2]=(uint8_t)(value>>8); p[3]=(uint8_t)value;
}

static void transform(struct nexus_sha256_context *context,
                      const uint8_t block[64]) {
    uint32_t w[64];
    for (size_t i=0U;i<16U;++i) w[i]=read_be32(block+i*4U);
    for (size_t i=16U;i<64U;++i) {
        const uint32_t x=w[i-15U], y=w[i-2U];
        const uint32_t s0=rotate_right(x,7U)^rotate_right(x,18U)^(x>>3U);
        const uint32_t s1=rotate_right(y,17U)^rotate_right(y,19U)^(y>>10U);
        w[i]=w[i-16U]+s0+w[i-7U]+s1;
    }
    uint32_t a=context->state[0],b=context->state[1];
    uint32_t c=context->state[2],d=context->state[3];
    uint32_t e=context->state[4],f=context->state[5];
    uint32_t g=context->state[6],h=context->state[7];
    for (size_t i=0U;i<64U;++i) {
        const uint32_t s1=rotate_right(e,6U)^rotate_right(e,11U)^rotate_right(e,25U);
        const uint32_t choice=(e&f)^(~e&g);
        const uint32_t t1=h+s1+choice+round_constants[i]+w[i];
        const uint32_t s0=rotate_right(a,2U)^rotate_right(a,13U)^rotate_right(a,22U);
        const uint32_t majority=(a&b)^(a&c)^(b&c);
        const uint32_t t2=s0+majority;
        h=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;
    }
    context->state[0]+=a;context->state[1]+=b;
    context->state[2]+=c;context->state[3]+=d;
    context->state[4]+=e;context->state[5]+=f;
    context->state[6]+=g;context->state[7]+=h;
}

void nexus_sha256_init(struct nexus_sha256_context *c) {
    if(c==NULL)return;
    static const uint32_t initial[8]={
        0x6A09E667U,0xBB67AE85U,0x3C6EF372U,0xA54FF53AU,
        0x510E527FU,0x9B05688CU,0x1F83D9ABU,0x5BE0CD19U
    };
    for(size_t i=0U;i<8U;++i)c->state[i]=initial[i];
    c->bit_count=0U;c->block_used=0U;
}

void nexus_sha256_update(struct nexus_sha256_context *c,
                         const void *data,size_t length) {
    if(c==NULL||(length!=0U&&data==NULL))return;
    const uint8_t *p=(const uint8_t *)data;
    c->bit_count+=(uint64_t)length*8U;
    while(length!=0U){
        size_t amount=64U-c->block_used;
        if(amount>length)amount=length;
        for(size_t i=0U;i<amount;++i)c->block[c->block_used+i]=p[i];
        c->block_used+=amount;p+=amount;length-=amount;
        if(c->block_used==64U){transform(c,c->block);c->block_used=0U;}
    }
}

void nexus_sha256_final(struct nexus_sha256_context *c,
                        uint8_t digest[NEXUS_SHA256_SIZE]) {
    if(c==NULL||digest==NULL)return;
    const uint64_t bits=c->bit_count;
    c->block[c->block_used++]=0x80U;
    if(c->block_used>56U){
        while(c->block_used<64U)c->block[c->block_used++]=0U;
        transform(c,c->block);c->block_used=0U;
    }
    while(c->block_used<56U)c->block[c->block_used++]=0U;
    for(size_t i=0U;i<8U;++i)c->block[63U-i]=(uint8_t)(bits>>(i*8U));
    transform(c,c->block);
    for(size_t i=0U;i<8U;++i)write_be32(digest+i*4U,c->state[i]);
    for(size_t i=0U;i<sizeof(*c);++i)((uint8_t *)c)[i]=0U;
}

void nexus_sha256(const void *data,size_t length,
                  uint8_t digest[NEXUS_SHA256_SIZE]) {
    struct nexus_sha256_context c;
    nexus_sha256_init(&c);nexus_sha256_update(&c,data,length);
    nexus_sha256_final(&c,digest);
}

void nexus_hmac_sha256(const void *key,size_t key_length,
                       const void *data,size_t data_length,
                       uint8_t digest[NEXUS_SHA256_SIZE]) {
    if (digest == NULL) return;
    /* This API cannot report errors, so reject invalid spans by returning a
     * deterministic all-zero result instead of silently hashing empty input. */
    if ((key_length != 0U && key == NULL) ||
        (data_length != 0U && data == NULL)) {
        for (size_t i = 0U; i < NEXUS_SHA256_SIZE; ++i) digest[i] = 0U;
        return;
    }
    uint8_t block[64]={0},inner[NEXUS_SHA256_SIZE];
    if(key_length>64U){nexus_sha256(key,key_length,block);key_length=32U;}
    else if(key_length!=0U&&key!=NULL){
        const uint8_t *k=(const uint8_t *)key;
        for(size_t i=0U;i<key_length;++i)block[i]=k[i];
    }
    uint8_t pad[64];
    for(size_t i=0U;i<64U;++i)pad[i]=(uint8_t)(block[i]^0x36U);
    struct nexus_sha256_context c;
    nexus_sha256_init(&c);nexus_sha256_update(&c,pad,64U);
    nexus_sha256_update(&c,data,data_length);nexus_sha256_final(&c,inner);
    for(size_t i=0U;i<64U;++i)pad[i]=(uint8_t)(block[i]^0x5cU);
    nexus_sha256_init(&c);nexus_sha256_update(&c,pad,64U);
    nexus_sha256_update(&c,inner,sizeof(inner));nexus_sha256_final(&c,digest);
    for(size_t i=0U;i<sizeof(block);++i)block[i]=0U;
    for(size_t i=0U;i<sizeof(inner);++i)inner[i]=0U;
    for(size_t i=0U;i<sizeof(pad);++i)pad[i]=0U;
}

bool nexus_hkdf_sha256(const void *salt,size_t salt_length,
                       const void *ikm,size_t ikm_length,
                       const void *info,size_t info_length,
                       void *output,size_t output_length) {
    if((ikm_length!=0U&&ikm==NULL)||(salt_length!=0U&&salt==NULL)||
       (info_length!=0U&&info==NULL)||(output_length!=0U&&output==NULL)||
       output_length>255U*NEXUS_SHA256_SIZE)return false;
    uint8_t zero_salt[NEXUS_SHA256_SIZE]={0},prk[NEXUS_SHA256_SIZE];
    uint8_t previous[NEXUS_SHA256_SIZE],input[32U+1024U+1U];
    const void *actual_salt=salt_length==0U?zero_salt:salt;
    const size_t actual_salt_length=salt_length==0U?sizeof(zero_salt):salt_length;
    nexus_hmac_sha256(actual_salt,actual_salt_length,ikm,ikm_length,prk);
    if(info_length>1024U){for(size_t i=0U;i<32U;++i)prk[i]=0U;return false;}
    size_t produced=0U,previous_length=0U;
    for(uint16_t counter=1U;produced<output_length;++counter){
        size_t n=0U;
        for(size_t i=0U;i<previous_length;++i)input[n++]=previous[i];
        const uint8_t *info_bytes=(const uint8_t *)info;
        for(size_t i=0U;i<info_length;++i)input[n++]=info_bytes[i];
        input[n++]=(uint8_t)counter;
        nexus_hmac_sha256(prk,sizeof(prk),input,n,previous);
        previous_length=sizeof(previous);
        size_t amount=output_length-produced;
        if(amount>sizeof(previous))amount=sizeof(previous);
        uint8_t *out=(uint8_t *)output;
        for(size_t i=0U;i<amount;++i)out[produced+i]=previous[i];
        produced+=amount;
    }
    for(size_t i=0U;i<sizeof(prk);++i)prk[i]=0U;
    for(size_t i=0U;i<sizeof(previous);++i)previous[i]=0U;
    for(size_t i=0U;i<sizeof(input);++i)input[i]=0U;
    return true;
}
