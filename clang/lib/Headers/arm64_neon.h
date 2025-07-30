/*===---- arm64_neon.h - ARM Windows intrinsics ----------------------------===
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===-----------------------------------------------------------------------===
 */

/* Only include this if we're compiling for the windows platform. */
#ifndef _MSC_VER
#include_next <arm64_neon.h>
#else

#ifndef __ARM64_NEON_H
#define __ARM64_NEON_H

#include <arm_neon.h>

#if defined (__cplusplus)
extern "C" {
#endif

static inline uint8x16_t __iso_volatile_neon_load128(const volatile uint8x16_t *ptr)
{
  return *ptr;
}

static inline uint8x16x2_t __iso_volatile_neon_load128_p(const volatile uint8x16x2_t *ptr)
{
  uint8x16x2_t result;
  __asm__ __volatile__(
    "ldp %q0, %q1, %2"
    : "=w" (result.val[0]), "=w" (result.val[1])
    : "m" (*ptr)
  );
  return result;
}

static inline uint8x8x2_t __iso_volatile_neon_load64_np(const volatile uint8x8x2_t *ptr) {
  uint8x8x2_t result;
  __asm__ __volatile__(
    "ldnp %d0, %d1, %2"
    : "=w" (result.val[0]), "=w" (result.val[1])
    : "m" (*ptr)
  );
  return result;
}

static inline uint8x16x2_t __iso_volatile_neon_load128_np(const volatile uint8x16x2_t *ptr) {
  uint8x16x2_t result;
  __asm__ __volatile__(
    "ldnp %q0, %q1, %2"
    : "=w" (result.val[0]), "=w" (result.val[1])
    : "m" (*ptr)
  );
  return result;
}

static inline void __iso_volatile_neon_store128(volatile uint8x16_t *p, uint8x16_t val)
{
    *p = val;
}

static inline void __iso_volatile_neon_store128_p(volatile uint8x16x2_t *ptr, uint8x16x2_t data) {
  __asm__ __volatile__(
    "stp %q[i1], %q[i2], %[out]"
    : [out] "=m" (*ptr)
    : [i1]"w" (data.val[0]), [i2]"w" (data.val[1])
  );
}

static inline void __iso_volatile_neon_store64_np(volatile uint8x8x2_t *ptr, uint8x8x2_t data) {
  __asm__ __volatile__(
    "stnp %d[i1], %d[i2], %[out]"
    : [out] "=m" (*ptr)
    : [i1]"w" (data.val[0]), [i2]"w" (data.val[1])
  );
}

static inline void __iso_volatile_neon_store128_np(volatile uint8x16x2_t *ptr, uint8x16x2_t data) {
  __asm__ __volatile__(
    "stnp %q[i1], %q[i2], %[out]"
    : [out] "=m" (*ptr)
    : [i1]"w" (data.val[0]), [i2]"w" (data.val[1])
  );
}

#if defined (__cplusplus)
}
#endif

#endif /* __ARM64_NEON_H */
#endif /* _MSC_VER */
