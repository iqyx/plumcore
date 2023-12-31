/**
 * poly1305 implementation
 * By Marek Koza, qyx@krtko.org
 * 
 * Based on code by
 * 20080912, D. J. Bernstein, Public domain.
 * 
 * Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 * 
 * This file is part of uMesh node firmware (http://qyx.krtko.org/embedded/umesh)
 *
 */


#ifndef _POLY1305_H_
#define _POLY1305_H_



int poly1305(unsigned char *out, const unsigned char *in, unsigned long long inlen, const unsigned char *k);
#define POLY1305_OK 0

#endif
