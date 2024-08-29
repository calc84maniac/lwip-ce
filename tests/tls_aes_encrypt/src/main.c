#include <ti/screen.h>
#include <ti/getkey.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "tls/includes/aes.h"


#define KEYSIZE (128>>3)    // 256 bits converted to bytes

// TEST 1
uint8_t key1[] = {0xEE,0x89,0x19,0xC3,0x8D,0x53,0x7A,0xD6,0x04,0x19,0x9E,0x77,0x0B,0xE0,0xE0,0x4C};
uint8_t iv1[] = {0x79,0xA6,0xDE,0xDF,0xF0,0xA2,0x7C,0x7F,0xEE,0x0B,0x8E,0xF5,0x12,0x63,0xA4,0x8A};
char *msg1 = "The lazy fox jumped over the dog!";
char *aad1 = "Some random header";
uint8_t tciphertext1[] = {
    0x68,0x7d,0xb1,0x88,0xd1,0x37,0x84,0x42,0xf8,0x84,0x76,0x19,0x31,0x0d,0x7c,0xd1,
    0x9a,0xe4,0x3a,0x78,0x20,0xdb,0x7d,0x54,0x45,0x5a,0x35,0xba,0xe0,0x37,0x01,0x56,0x0d
};
uint8_t ttag1[] = {0x23,0x62,0x9b,0x0d,0xfe,0xd6,0x01,0x8e,0x46,0x32,0x86,0x8c,0x07,0xc3,0xa8,0x3c};


// TEST 2
uint8_t key2[] = {
    0x5a,0x99,0xaf,0x84,0x89,0x99,0xe1,0xa1,0x76,0x99,0x30,0xbc,0x9f,0xea,0xa2,0xbd,
    0xd2,0xec,0x0a,0x03,0xaa,0x45,0xa5,0x49,0x36,0x66,0xe6,0x99,0xa7,0x02,0x01,0x57
};
uint8_t iv2[] ={0xea,0xfb,0xb9,0xac,0xdd,0x83,0xfb,0x66,0xda,0xa3,0xca,0x93,0xc7,0x2e};
const char *msg2 = "Leading the way to the future!";
uint8_t tciphertext2[] = {0x21,0xea,0xfb,0x83,0x6d,0x3d,0xe2,0x4c,0xac,0xe6,0x90,0x1f,0x09,0xa7,0x68,0x32,0xcd,0x8d,0xa0,0xc8,0x08,0xf1,0xb8,0x44,0x0f,0x4d,0x36,0x53,0x91,0x01};
uint8_t ttag2[] = {0x73,0x82,0xdc,0x99,0x5b,0xef,0x0f,0x27,0x0e,0xf8,0x31,0xf0,0x76,0xa3,0xf9,0x2b};

/* Main function, called first */
int main(void)
{
    /* Clear the homescreen */
    struct tls_aes_context ctx;
    os_ClrHome();
    uint8_t tbuf[100] = {0};
    uint8_t tag[TLS_AES_AUTH_TAG_SIZE];
    bool status = true;
    
    // verify and decrypt
    status &= tls_aes_init(&ctx, key1, sizeof key1, iv1, sizeof iv1);
    status &= tls_aes_update_aad(&ctx, aad1, strlen(aad1));
    status &= tls_aes_encrypt(&ctx, msg1, strlen(msg1), tbuf);
    status &= tls_aes_digest(&ctx, tag);
    
    // await message match string
    if(status &&
       (memcmp(tbuf, tciphertext1, strlen(msg1))==0) &&
       (memcmp(tag, ttag1, TLS_AES_AUTH_TAG_SIZE)==0))
        printf("success");
    else
        printf("failed");
    os_GetKey();
    os_ClrHome();
    
    // verify and decrypt
    status &= tls_aes_init(&ctx, key2, sizeof key2, iv2, sizeof iv2);
    status &= tls_aes_encrypt(&ctx, msg2, strlen(msg2), tbuf);
    status &= tls_aes_digest(&ctx, tag);
    
    // await message match string
    if(status &&
       (memcmp(tbuf, tciphertext2, strlen(msg2))==0) &&
       (memcmp(tag, ttag2, TLS_AES_AUTH_TAG_SIZE)==0))
        printf("success");
    else
        printf("failed");
    os_GetKey();
    os_ClrHome();
    
    return 0;
}
