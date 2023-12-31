/**
 * Ed25519-donna implementation
 * By Marek Koza, qyx@krtko.org
 * 
 * Based on public domain sources by Andrew M. <liquidsun@gmail.com>
 * 
 * Based on public domain sources and papers by Daniel J. Bernstein,
 * Niels Duif, Tanja Lange, Peter Schwabe and Bo-Yin Yang
 *
 * http://ed25519.cr.yp.to
 * 
 * Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 * 
 * This file is part of uMesh node firmware (http://qyx.krtko.org/embedded/umesh)
 *
 */

#ifndef _ED25519_H_
#define _ED25519_H_



/* 512bit hash function used in sign/verify */
#define ed25519_hash_context sha512_ctx
#define ed25519_hash_init sha512_init
#define ed25519_hash_update sha512_update
#define ed25519_hash_final sha512_final
#define ed25519_hash sha512

/* macros */
#define mul32x32_64(a, b) (((uint64_t)(a)) * (b))
#define ALIGN(x) __attribute__((aligned(x)))
#define ROTL32(a,b) (((a) << (b)) | ((a) >> (32 - b)))
#define ROTR32(a,b) (((a) >> (b)) | ((a) << (32 - b)))
#define mul64x64_128(out,a,b) out = (uint128_t)a * b;
#define shr128_pair(out,hi,lo,shift) out = (uint64_t)((((uint128_t)hi << 64) | lo) >> (shift));
#define shl128_pair(out,hi,lo,shift) out = (uint64_t)(((((uint128_t)hi << 64) | lo) << (shift)) >> 64);
#define shr128(out,in,shift) out = (uint64_t)(in >> (shift));
#define shl128(out,in,shift) out = (uint64_t)((in << shift) >> 64);
#define add128(a,b) a += b;
#define add128_64(a,b) a += (uint64_t)b;
#define lo128(a) ((uint64_t)a)
#define hi128(a) ((uint64_t)(a >> 64))

#define bignum256modm_bits_per_limb 30
#define bignum256modm_limb_size 9


typedef unsigned char ed25519_signature[64];
typedef unsigned char ed25519_public_key[32];
typedef unsigned char ed25519_secret_key[32];
typedef unsigned char curved25519_key[32];
typedef unsigned char hash_512bits[64];

typedef uint32_t bignum25519[10];
typedef uint32_t bignum25519align16[12];

typedef uint32_t bignum256modm_element_t;
typedef bignum256modm_element_t bignum256modm[9];

typedef struct ge25519_t {
	bignum25519 x, y, z, t;
} ge25519;

typedef struct ge25519_p1p1_t {
	bignum25519 x, y, z, t;
} ge25519_p1p1;

typedef struct ge25519_niels_t {
	bignum25519 ysubx, xaddy, t2d;
} ge25519_niels;

typedef struct ge25519_pniels_t {
	bignum25519 ysubx, xaddy, z, t2d;
} ge25519_pniels;




/**
 * API
 */
void ed25519_publickey(const ed25519_secret_key sk, ed25519_public_key pk);

int ed25519_verify(const ed25519_signature RS, const ed25519_public_key pk, const unsigned char *m, size_t mlen);
#define ED25519_VERIFY_OK 0
#define ED25519_VERIFY_BAD_SIGNATURE 1

void ed25519_sign(ed25519_signature RS, const ed25519_secret_key sk, const ed25519_public_key pk, const unsigned char *m, size_t mlen);

void curved25519_scalarmult_basepoint(curved25519_key pk, const curved25519_key e);



#endif
