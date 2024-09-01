#include <ti/screen.h>
#include <ti/getkey.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "tls/includes/hash.h"
#include "tls/includes/passwords.h"

// input vectors
const char *test1 = "Science7!";
const char *test2 = "Cemetech12?";
const char *test3 = "CryptX$1";

const uint8_t salt1[] = {0x35,0x0c,0x80,0x4a,0xea,0xfa,0xb3,0x01,0x2c,0x23,0xb1,0x85,0x70,0xac,0xd5,0xcd};
const uint8_t salt2[] = {0xc9,0x90,0x03,0x15,0x5e,0xc3,0xec,0x9b,0xf2,0x26,0xd0,0x37,0xef,0xf7,0x4f,0xcf};
const uint8_t salt3[] = {0x0b,0x5b,0x93,0x49,0xd0,0x60,0xb9,0x0d,0xa4,0xe5,0x76,0x86,0xda,0xcd,0xd9,0x8f};

// test vectors
const uint8_t expected1[] = {0xa0,0x96,0x7c,0xcb,0xe8,0x2c,0x53,0x2a,0x89,0x50,0x1a,0xef,0x41,0xa8,0xb2,0xb6};

const uint8_t expected2[] = {0x74,0x75,0x6f,0x10,0x05,0x91,0x56,0x13,0x73,0x50,0x8c,0x2d,0x4a,0x1a,0x94,0x32,0xab,0xc8,0xce,0xf5,0xec,0xde,0xde,0xb6};

const uint8_t expected3[] = {0xf1,0x1f,0x9c,0xc4,0x42,0xfc,0xeb,0x41,0xc5,0x52,0x4a,0x45,0x04,0xab,0x1b,0x8a,0xfd,0x9a,0xb7,0x49,0x46,0x14,0x66,0x17,0x70,0xb8,0x7b,0x1e,0x0f,0xb3,0x45,0xb0};


/* Main function, called first */
int main(void)
{
    /* Clear the homescreen */
    os_ClrHome();
    
    uint8_t key[32];
    
    // test 1
    tls_pbkdf2(test1, strlen(test1), salt1, sizeof salt1, key, 16, 10, TLS_HASH_SHA256);
    
    if(memcmp(key, expected1, sizeof expected1)==0)
        printf("success");
    else printf("failed");
    os_GetKey();
    os_ClrHome();
    
    // test 2
    tls_pbkdf2(test2, strlen(test2), salt2, sizeof salt2, key, 24, 100, TLS_HASH_SHA256);
    
    if(memcmp(key, expected2, sizeof expected2)==0)
        printf("success");
    else printf("failed");
    os_GetKey();
    os_ClrHome();
    
    // test 3
    tls_pbkdf2(test3, strlen(test3), salt3, sizeof salt3, key, 32, 1000, TLS_HASH_SHA256);
    
    if(memcmp(key, expected3, sizeof expected3)==0)
        printf("success");
    else printf("failed");
    os_GetKey();
    os_ClrHome();
    return 0;
}
