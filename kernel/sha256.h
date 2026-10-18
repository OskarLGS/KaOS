#ifndef SHA256_H
#define SHA256_H

#include <stdint.h>

// SHA-256 (FIPS 180-4), for password hashing.
typedef struct {
    uint32_t state[8];
    uint64_t length;      // Bytes hashed so far
    uint8_t  block[64];
    uint32_t used;        // Bytes waiting in `block`
} sha256_t;

void sha256_init(sha256_t* c);
void sha256_update(sha256_t* c, const void* data, uint32_t len);
void sha256_final(sha256_t* c, uint8_t out[32]);
void sha256(const void* data, uint32_t len, uint8_t out[32]);

#endif
