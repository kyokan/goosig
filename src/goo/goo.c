#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <gmp.h>

#include "goo.h"
#include "random.h"

#define goo_mpz_import(ret, data, len) \
  mpz_import((ret), (len), 1, sizeof((data)[0]), 0, 0, (data))

#define goo_mpz_export(data, size, n) \
  mpz_export((data), (size), 1, sizeof((data)[0]), 0, 0, (n));

#define goo_mpz_print(n) \
  (mpz_out_str(stdout, 16, (n)), printf("\n"))

#define goo_print_hex(data, len) do { \
  mpz_t n; \
  mpz_init(n); \
  goo_mpz_import(n, (data), (len)); \
  goo_mpz_print(n); \
  mpz_clear(n); \
} while (0)

static inline size_t
goo_mpz_bitlen(const mpz_t n) {
  size_t bits = mpz_sizeinbase(n, 2);

  if (bits == 1 && mpz_cmp_ui(n, 0) == 0)
    bits = 0;

  return bits;
}

#define goo_mpz_bytesize(n) \
  (goo_mpz_bitlen((n)) + 7) / 8

#include <stdio.h>

static inline unsigned char *
goo_mpz_pad(void *out, size_t size, const mpz_t n) {
  size_t len = goo_mpz_bytesize(n);

  if (len > size)
    return NULL;

  if (size == 0)
    return NULL;

  if (out == NULL) {
    out = malloc(size);
    if (out == NULL)
      return NULL;
  }

  size_t pos = size - len;

  memset(out, 0x00, pos);

  goo_mpz_export(out + pos, NULL, n);

  return out;
}

static const char goo_prefix[] = "libGooPy:";
static const char goo_pers[] = "libGooPy_prng";

static unsigned int goo_primes[168] = {
  2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37, 41, 43, 47, 53, 59, 61, 67, 71,
  73, 79, 83, 89, 97, 101, 103, 107, 109, 113, 127, 131, 137, 139, 149, 151,
  157, 163, 167, 173, 179, 181, 191, 193, 197, 199, 211, 223, 227, 229, 233,
  239, 241, 251, 257, 263, 269, 271, 277, 281, 283, 293, 307, 311, 313, 317,
  331, 337, 347, 349, 353, 359, 367, 373, 379, 383, 389, 397, 401, 409, 419,
  421, 431, 433, 439, 443, 449, 457, 461, 463, 467, 479, 487, 491, 499, 503,
  509, 521, 523, 541, 547, 557, 563, 569, 571, 577, 587, 593, 599, 601, 607,
  613, 617, 619, 631, 641, 643, 647, 653, 659, 661, 673, 677, 683, 691, 701,
  709, 719, 727, 733, 739, 743, 751, 757, 761, 769, 773, 787, 797, 809, 811,
  821, 823, 827, 829, 839, 853, 857, 859, 863, 877, 881, 883, 887, 907, 911,
  919, 929, 937, 941, 947, 953, 967, 971, 977, 983, 991, 997
};

static inline void *
goo_malloc(size_t size) {
  if (size == 0)
    return NULL;

  void *ptr = malloc(size);
  assert(ptr != NULL);
  return ptr;
}

static inline void *
goo_calloc(size_t nmemb, size_t size) {
  if (nmemb == 0 || size == 0)
    return NULL;

  void *ptr = calloc(nmemb, size);
  assert(ptr != NULL);
  return ptr;
}

static inline void *
goo_realloc(void *ptr, size_t size) {
  if (size == 0) {
    realloc(ptr, size);
    return NULL;
  }

  void *p = realloc(ptr, size);
  assert(p != NULL);
  return p;
}

static inline void
goo_free(void *ptr) {
  if (ptr != NULL)
    free(ptr);
}

static void
goo_group_pow(
  goo_group_t *group,
  mpz_t ret,
  const mpz_t b,
  const mpz_t b_inv,
  const mpz_t e
);

static void
goo_group_mul(goo_group_t *group, mpz_t ret, const mpz_t m1, const mpz_t m2);

static void
goo_prng_init(goo_prng_t *prng) {
  memset((void *)prng, 0x00, sizeof(goo_prng_t));

  mpz_init(prng->save);
  mpz_init(prng->tmp);
}

static void
goo_prng_uninit(goo_prng_t *prng) {
  mpz_clear(prng->save);
  mpz_clear(prng->tmp);
}

static void
goo_prng_seed(goo_prng_t *prng, unsigned char key[32]) {
  unsigned char entropy[64 + sizeof(goo_pers) - 1];

  memcpy(&entropy[0], &key[0], 32);
  memset(&entropy[32], 0x00, 32);
  memcpy(&entropy[64], &goo_pers[0], sizeof(goo_pers) - 1);

  goo_drbg_init(&prng->ctx, entropy, sizeof(entropy));

  mpz_set_ui(prng->save, 0);
}

static void
goo_prng_nextrand(goo_prng_t *prng, unsigned char out[32]) {
  goo_drbg_generate(&prng->ctx, &out[0], 32);
}

static void
goo_prng_getrandbits(goo_prng_t *prng, mpz_t r, unsigned int nbits) {
  mpz_set(r, prng->save);

  unsigned int b = goo_mpz_bitlen(r);
  unsigned char out[32];

  while (b < nbits) {
    mpz_mul_2exp(r, r, 256);
    goo_prng_nextrand(prng, &out[0]);
    goo_mpz_import(prng->tmp, &out[0], 32);
    mpz_ior(r, r, prng->tmp);
    b += 256;
  }

  unsigned int left = b - nbits;

  if (left == 0) {
    mpz_set_ui(prng->save, 0);
    return;
  }

  // save = r & ((1 << left) - 1)
  mpz_set_ui(prng->tmp, 1);
  mpz_mul_2exp(prng->tmp, prng->tmp, left);
  mpz_sub_ui(prng->tmp, prng->tmp, 1);
  mpz_set(prng->save, r);
  mpz_and(prng->save, prng->save, prng->tmp);

  // r >>= left;
  mpz_fdiv_q_2exp(r, r, left);
}

static size_t
goo_clog2(const mpz_t val) {
  mpz_t t;
  mpz_init_set(t, val);
  mpz_sub_ui(t, t, 1);
  size_t bits = goo_mpz_bitlen(t);
  mpz_clear(t);
  return bits;
}

static unsigned long
goo_sqrt(unsigned long n) {
  int len = 0;

  unsigned long nn = n;

  while (nn) {
    len += 1;
    nn >>= 1;
  }

  int shift = 2 * ((len + 1) / 2) - 2;

  if (shift < 0)
    shift = 0;

  unsigned long res = 0;

  while (shift >= 0) {
    res <<= 1;

    unsigned long res_c = res + 1;

    if ((res_c * res_c) <= (n >> (unsigned long)shift))
      res = res_c;

    shift -= 2;
  }

  return res;
}

static inline size_t
combspec_size(long bits) {
  long max = 0;

  for (long ppa = 2; ppa < 18; ppa++) {
    long bpw = (bits + ppa - 1) / ppa;
    long sqrt = goo_sqrt(bpw);

    for (long aps = 1; aps < sqrt + 2; aps++) {
      if (bpw % aps != 0)
        continue;

      long shifts = bpw / aps;
      long ops1 = shifts * (aps + 1) - 1;
      long ops2 = aps * (shifts + 1) - 1;
      long ops = (ops1 > ops2 ? ops1 : ops2) + 1;

      if (ops > max)
        max = ops;
    }
  }

  return max;
}

static void
combspec_result(
  goo_combspec_t *combs,
  long shifts,
  long aps,
  long ppa,
  long bps
) {
  long ops = shifts * (aps + 1) - 1;
  long size = ((1 << ppa) - 1) * aps;

  goo_combspec_t *best = &combs[ops];

  if (best->exists == 0 || best->size > size) {
    best->exists = 1;
    best->points_per_add = ppa;
    best->adds_per_shift = aps;
    best->shifts = shifts;
    best->bits_per_window = bps;
    best->ops = ops;
    best->size = size;
  }
}

int
goo_combspec_init(
  goo_combspec_t *combspec,
  long bits,
  long maxsize
) {
  if (bits < 128)
    return 0;

  size_t map_size = combspec_size(bits);
  goo_combspec_t *combs = goo_calloc(map_size, sizeof(goo_combspec_t));

  for (long ppa = 2; ppa < 18; ppa++) {
    long bpw = (bits + ppa - 1) / ppa;
    long sqrt = goo_sqrt(bpw);

    for (long aps = 1; aps < sqrt + 2; aps++) {
      if (bpw % aps != 0) {
        // Only factorizations of
        // bits_per_window are useful.
        continue;
      }

      long shifts = bpw / aps;

      combspec_result(combs, shifts, aps, ppa, bpw);
      combspec_result(combs, aps, shifts, ppa, bpw);
    }
  }

  long sm = 0;
  goo_combspec_t *ret = NULL;

  for (size_t i = 0; i < map_size; i++) {
    goo_combspec_t *comb = &combs[i];

    if (comb->exists == 0)
      continue;

    if (sm != 0 && sm <= comb->size)
      continue;

    sm = comb->size;

    if (sm <= maxsize) {
      ret = comb;
      break;
    }
  }

  if (ret == NULL) {
    goo_free(combs);
    return 0;
  }

  memcpy(combspec, ret, sizeof(goo_combspec_t));
  goo_free(combs);

  return 1;
}

static void
goo_comb_init(
  goo_comb_t *comb,
  goo_group_t *group,
  mpz_t base,
  goo_combspec_t *spec,
  int tiny
) {
  memset((void *)comb, 0x00, sizeof(goo_comb_t));

  long skip = (1 << spec->points_per_add) - 1;

  comb->exists = 1;
  comb->points_per_add = spec->points_per_add;
  comb->adds_per_shift = spec->adds_per_shift;
  comb->shifts = spec->shifts;
  comb->bits_per_window = spec->bits_per_window;
  comb->bits = spec->bits_per_window * spec->points_per_add;
  comb->points_per_subcomb = skip;
  comb->size = spec->size;

  if (tiny) {
    assert(comb->points_per_add == 8);
    assert(comb->adds_per_shift == 2);
    assert(comb->shifts == 8);
    assert(comb->bits_per_window == 16);
    assert(comb->bits == 128);
    assert(comb->points_per_subcomb == 255);
    assert(comb->size == 510);
  }

  comb->wins = (long **)goo_calloc(comb->shifts, sizeof(long *));

  for (int i = 0; i < comb->shifts; i++)
    comb->wins[i] = (long *)goo_calloc(comb->adds_per_shift, sizeof(long));

  comb->items = (mpz_t *)goo_calloc(comb->size, sizeof(mpz_t));

  for (int i = 0; i < comb->size; i++)
    mpz_init(comb->items[i]);

  mpz_set(comb->items[0], base);

  mpz_t *it = &comb->items[0];

  mpz_t win;
  mpz_init(win);

  mpz_set_ui(win, 1);
  mpz_mul_2exp(win, win, comb->bits_per_window);

  for (int i = 1; i < comb->points_per_add; i++) {
    int oval = 1 << i;
    int ival = oval >> 1;

    goo_group_pow(group, it[oval - 1], it[ival - 1], NULL, win);

    for (int j = oval + 1; j < 2 * oval; j++)
      goo_group_mul(group, it[j - 1], it[j - oval - 1], it[oval - 1]);
  }

  mpz_set_ui(win, 1);
  mpz_mul_2exp(win, win, comb->shifts);

  for (int i = 1; i < comb->adds_per_shift; i++) {
    for (int j = 0; j < skip; j++) {
      int k = i * skip + j;

      goo_group_pow(group, it[k], it[k - skip], NULL, win);
    }
  }

  mpz_clear(win);
}

static void
goo_comb_uninit(goo_comb_t *comb) {
  for (int i = 0; i < comb->size; i++)
    mpz_clear(comb->items[i]);

  for (int i = 0; i < comb->shifts; i++)
    goo_free(comb->wins[i]);

  goo_free(comb->wins);

  comb->items = NULL;
  comb->wins = NULL;
}

static int
goo_to_comb_exp(goo_comb_t *comb, const mpz_t e) {
  int len = (int)goo_mpz_bitlen(e);

  if (len < 0 || len > comb->bits)
    return 0;

  int pad = comb->bits - len;

  for (int i = comb->adds_per_shift - 1; i >= 0; i--) {
    for (int j = 0; j < comb->shifts; j++) {
      long ret = 0;

      for (int k = 0; k < comb->points_per_add; k++) {
        int b = (i + k * comb->adds_per_shift) * comb->shifts + j;

        ret <<= 1;

        if (b < pad)
          continue;

        int p = (comb->bits - 1) - b;
        assert(p >= 0);

        ret += (long)mpz_tstbit(e, p);
      }

      comb->wins[j][(comb->adds_per_shift - 1) - i] = ret;
    }
  }

  return 1;
}

static int
goo_group_init(
  goo_group_t *group,
  const unsigned char *n,
  size_t n_len,
  unsigned long g,
  unsigned long h,
  unsigned long modbits
) {
  memset((void *)group, 0x00, sizeof(goo_group_t));

  mpz_init(group->n);
  mpz_init(group->nh);
  mpz_init(group->g);
  mpz_init(group->h);
  mpz_init(group->b12);
  mpz_init(group->b34);
  mpz_init(group->b1234);
  mpz_init(group->b12345);
  mpz_init(group->b12_inv);
  mpz_init(group->b34_inv);
  mpz_init(group->b1234_inv);
  mpz_init(group->b12345_inv);
  mpz_init(group->bsq);
  mpz_init(group->val);
  mpz_init(group->mask);
  mpz_init(group->r);
  mpz_init(group->gh);
  mpz_init(group->C1_inv);
  mpz_init(group->C2_inv);
  mpz_init(group->Aq_inv);
  mpz_init(group->Bq_inv);
  mpz_init(group->Cq_inv);
  mpz_init(group->A);
  mpz_init(group->B);
  mpz_init(group->C);
  mpz_init(group->D);
  mpz_init(group->z_w2_m_an);
  mpz_init(group->tmp);
  mpz_init(group->chal_out);
  mpz_init(group->ell_r_out);
  mpz_init(group->elldiff);
  mpz_init(group->C1);
  mpz_init(group->C2);
  mpz_init(group->t);
  mpz_init(group->msg);
  mpz_init(group->chal);
  mpz_init(group->ell);
  mpz_init(group->Aq);
  mpz_init(group->Bq);
  mpz_init(group->Cq);
  mpz_init(group->Dq);
  mpz_init(group->z_w);
  mpz_init(group->z_w2);
  mpz_init(group->z_s1);
  mpz_init(group->z_a);
  mpz_init(group->z_an);
  mpz_init(group->z_s1w);
  mpz_init(group->z_sa);

  for (int i = 0; i < GOO_TABLEN; i++) {
    mpz_init(group->pctab_p1[i]);
    mpz_init(group->pctab_n1[i]);
    mpz_init(group->pctab_p2[i]);
    mpz_init(group->pctab_n2[i]);
  }

  goo_mpz_import(group->n, n, n_len);

  mpz_set(group->nh, group->n);
  mpz_fdiv_q_ui(group->nh, group->nh, 2);

  mpz_set_ui(group->g, g);
  mpz_set_ui(group->h, h);

  group->rand_bits = goo_clog2(group->n) - 1;

  if (modbits != 0) {
    long big1 = 2 * modbits;
    long big2 = modbits + group->rand_bits;
    long big = big1 > big2 ? big1 : big2;
    long big_bits = big + GOO_CHAL_BITS + 1;

    goo_combspec_t big_spec;
    goo_combspec_init(&big_spec, big_bits, GOO_MAX_COMB_SIZE);

    long small_bits = group->rand_bits;
    goo_combspec_t small_spec;
    goo_combspec_init(&small_spec, small_bits, GOO_MAX_COMB_SIZE);

    goo_comb_init(&group->g_comb1, group, group->g, &small_spec, 0);
    goo_comb_init(&group->h_comb1, group, group->h, &small_spec, 0);
    goo_comb_init(&group->g_comb2, group, group->g, &big_spec, 0);
    goo_comb_init(&group->h_comb2, group, group->h, &big_spec, 0);
  } else {
    long tiny_bits = GOO_CHAL_BITS;

    goo_combspec_t tiny_spec;
    goo_combspec_init(&tiny_spec, tiny_bits, GOO_MAX_COMB_SIZE);

    goo_comb_init(&group->g_comb1, group, group->g, &tiny_spec, 1);
    goo_comb_init(&group->h_comb1, group, group->h, &tiny_spec, 1);

    memset(&group->g_comb2, 0x00, sizeof(goo_comb_t));
    memset(&group->h_comb2, 0x00, sizeof(goo_comb_t));
  }

  goo_prng_init(&group->prng);

  return 1;
}

static void
goo_group_uninit(goo_group_t *group) {
  mpz_clear(group->n);
  mpz_clear(group->nh);
  mpz_clear(group->g);
  mpz_clear(group->h);
  mpz_clear(group->b12);
  mpz_clear(group->b34);
  mpz_clear(group->b1234);
  mpz_clear(group->b12345);
  mpz_clear(group->b12_inv);
  mpz_clear(group->b34_inv);
  mpz_clear(group->b1234_inv);
  mpz_clear(group->b12345_inv);
  mpz_clear(group->bsq);
  mpz_clear(group->val);
  mpz_clear(group->mask);
  mpz_clear(group->r);
  mpz_clear(group->gh);
  mpz_clear(group->C1_inv);
  mpz_clear(group->C2_inv);
  mpz_clear(group->Aq_inv);
  mpz_clear(group->Bq_inv);
  mpz_clear(group->Cq_inv);
  mpz_clear(group->A);
  mpz_clear(group->B);
  mpz_clear(group->C);
  mpz_clear(group->D);
  mpz_clear(group->z_w2_m_an);
  mpz_clear(group->tmp);
  mpz_clear(group->chal_out);
  mpz_clear(group->ell_r_out);
  mpz_clear(group->elldiff);
  mpz_clear(group->C1);
  mpz_clear(group->C2);
  mpz_clear(group->t);
  mpz_clear(group->msg);
  mpz_clear(group->chal);
  mpz_clear(group->ell);
  mpz_clear(group->Aq);
  mpz_clear(group->Bq);
  mpz_clear(group->Cq);
  mpz_clear(group->Dq);
  mpz_clear(group->z_w);
  mpz_clear(group->z_w2);
  mpz_clear(group->z_s1);
  mpz_clear(group->z_a);
  mpz_clear(group->z_an);
  mpz_clear(group->z_s1w);
  mpz_clear(group->z_sa);

  for (int i = 0; i < GOO_TABLEN; i++) {
    mpz_clear(group->pctab_p1[i]);
    mpz_clear(group->pctab_n1[i]);
    mpz_clear(group->pctab_p2[i]);
    mpz_clear(group->pctab_n2[i]);
  }

  goo_comb_uninit(&group->g_comb1);
  goo_comb_uninit(&group->h_comb1);
  goo_comb_uninit(&group->g_comb2);
  goo_comb_uninit(&group->h_comb2);

  goo_prng_uninit(&group->prng);
}

static void
goo_group_reduce(goo_group_t *group, mpz_t ret, const mpz_t b) {
  if (mpz_cmp(b, group->nh) > 0)
    mpz_sub(ret, group->n, b);
}

static int
goo_group_is_reduced(goo_group_t *group, const mpz_t b) {
  return mpz_cmp(b, group->nh) <= 0 ? 1 : 0;
}

static void
goo_group_sqr(goo_group_t *group, mpz_t ret, const mpz_t b) {
  mpz_powm_ui(ret, b, 2, group->n);
}

static void
goo_group_pow(
  goo_group_t *group,
  mpz_t ret,
  const mpz_t b,
  const mpz_t b_inv,
  const mpz_t e
) {
  mpz_powm(ret, b, e, group->n);
}

static void
goo_group_mul(goo_group_t *group, mpz_t ret, const mpz_t m1, const mpz_t m2) {
  mpz_mul(ret, m1, m2);
  mpz_mod(ret, ret, group->n);
}

static int
goo_group_inv(goo_group_t *group, mpz_t ret, const mpz_t b) {
  return mpz_invert(ret, b, group->n) != 0 ? 1 : 0;
}

static int
goo_group_inv2(
  goo_group_t *group,
  mpz_t r1,
  mpz_t r2,
  const mpz_t b1,
  const mpz_t b2
) {
  mpz_mul(group->b12_inv, b1, b2);

  if (!goo_group_inv(group, group->b12_inv, group->b12_inv))
    return 0;

  goo_group_mul(group, r1, b2, group->b12_inv);
  goo_group_mul(group, r2, b1, group->b12_inv);

  return 1;
}

static int
goo_group_inv5(
  goo_group_t *group,
  mpz_t r1,
  mpz_t r2,
  mpz_t r3,
  mpz_t r4,
  mpz_t r5,
  const mpz_t b1,
  const mpz_t b2,
  const mpz_t b3,
  const mpz_t b4,
  const mpz_t b5
) {
  goo_group_mul(group, group->b12, b1, b2);
  goo_group_mul(group, group->b34, b3, b4);
  goo_group_mul(group, group->b1234, group->b12, group->b34);
  goo_group_mul(group, group->b12345, group->b1234, b5);

  if (!goo_group_inv(group, group->b12345_inv, group->b12345))
    return 0;

  goo_group_mul(group, group->b1234_inv, group->b12345_inv, b5);
  goo_group_mul(group, group->b34_inv, group->b1234_inv, group->b12);
  goo_group_mul(group, group->b12_inv, group->b1234_inv, group->b34);

  goo_group_mul(group, r1, group->b12_inv, b2);
  goo_group_mul(group, r2, group->b12_inv, b1);
  goo_group_mul(group, r3, group->b34_inv, b4);
  goo_group_mul(group, r4, group->b34_inv, b3);
  goo_group_mul(group, r5, group->b12345_inv, group->b1234);

  return 1;
}

static int
goo_group_powgh(goo_group_t *group, mpz_t ret, const mpz_t e1, const mpz_t e2) {
  goo_comb_t *gcomb = &group->g_comb1;
  goo_comb_t *hcomb = &group->h_comb1;

  size_t e1bits = goo_mpz_bitlen(e1);
  size_t e2bits = goo_mpz_bitlen(e2);
  size_t loge = e1bits > e2bits ? e1bits : e2bits;

  if (loge > (size_t)gcomb->bits) {
    if (group->g_comb2.exists && loge <= (size_t)group->g_comb2.bits) {
      gcomb = &group->g_comb2;
      hcomb = &group->h_comb2;
    } else {
      return 0;
    }
  }

  if (!goo_to_comb_exp(gcomb, e1))
    return 0;

  if (!goo_to_comb_exp(hcomb, e2))
    return 0;

  mpz_set_ui(ret, 1);

  for (int i = 0; i < gcomb->shifts; i++) {
    long *e1vs = gcomb->wins[i];
    long *e2vs = hcomb->wins[i];

    if (mpz_cmp_ui(ret, 1) != 0)
      goo_group_sqr(group, ret, ret);

    for (int j = 0; j < gcomb->adds_per_shift; j++) {
      long e1v = e1vs[j];
      long e2v = e2vs[j];

      if (e1v != 0) {
        mpz_t *g = &gcomb->items[j * gcomb->points_per_subcomb + e1v - 1];
        goo_group_mul(group, ret, ret, *g);
      }

      if (e2v != 0) {
        mpz_t *h = &hcomb->items[j * hcomb->points_per_subcomb + e2v - 1];
        goo_group_mul(group, ret, ret, *h);
      }
    }
  }

  return 1;
}

static int
goo_group_wnaf_pc_help(goo_group_t *group, const mpz_t b, mpz_t *out) {
  goo_group_sqr(group, group->bsq, b);

  mpz_set(out[0], b);

  for (int i = 1; i < GOO_TABLEN; i++)
    goo_group_mul(group, out[i], out[i - 1], group->bsq);

  return 1;
}

static int
goo_group_precomp_wnaf(
  goo_group_t *group,
  const mpz_t b,
  const mpz_t b_inv,
  mpz_t *p,
  mpz_t *n
) {
  if (!goo_group_wnaf_pc_help(group, b, p))
    return 0;

  if (!goo_group_wnaf_pc_help(group, b_inv, n))
    return 0;

  return 1;
}

static long *
goo_group_wnaf(goo_group_t *group, const mpz_t e, long *out, int bitlen) {
  long w = GOO_WINDOW_SIZE;

  mpz_set(group->r, e);

  for (int i = bitlen - 1; i >= 0; i--) {
    mpz_set_ui(group->val, 0);

    if (mpz_odd_p(group->r)) {
      mpz_set_ui(group->mask, (1 << w) - 1);
      mpz_and(group->val, group->r, group->mask);
      if (mpz_tstbit(group->val, w - 1))
        mpz_sub_ui(group->val, group->val, 1 << w);
      mpz_sub(group->r, group->r, group->val);
    }

    assert(mpz_fits_slong_p(group->val));
    out[i] = mpz_get_si(group->val);

    mpz_fdiv_q_ui(group->r, group->r, 2);
  }

  assert(mpz_cmp_ui(group->r, 0) == 0);

  return out;
}

static void
goo_group_one_mul(
  goo_group_t *group,
  mpz_t ret,
  long w,
  const mpz_t *p,
  const mpz_t *n
) {
  if (w > 0)
    goo_group_mul(group, ret, ret, p[(w - 1) >> 1]);
  else if (w < 0)
    goo_group_mul(group, ret, ret, n[(-1 - w) >> 1]);
}

static int
goo_group_pow2(
  goo_group_t *group,
  mpz_t ret,
  const mpz_t b1,
  const mpz_t b1_inv,
  const mpz_t e1,
  const mpz_t b2,
  const mpz_t b2_inv,
  const mpz_t e2
) {
  mpz_t *p1 = &group->pctab_p1[0];
  mpz_t *n1 = &group->pctab_n1[0];
  mpz_t *p2 = &group->pctab_p2[0];
  mpz_t *n2 = &group->pctab_n2[0];

  if (!goo_group_precomp_wnaf(group, b1, b1_inv, p1, n1))
    return 0;

  if (!goo_group_precomp_wnaf(group, b2, b2_inv, p2, n2))
    return 0;

  size_t e1len = goo_mpz_bitlen(e1);
  size_t e2len = goo_mpz_bitlen(e2);
  size_t totlen = (e1len > e2len ? e1len : e2len) + 1;

  long *e1bits = goo_group_wnaf(group, e1, &group->e1bits[0], totlen);
  long *e2bits = goo_group_wnaf(group, e2, &group->e2bits[0], totlen);

  mpz_set_ui(ret, 1);

  for (size_t i = 0; i < totlen; i++) {
    long w1 = e1bits[i];
    long w2 = e2bits[i];

    if (mpz_cmp_ui(ret, 1) != 0)
      goo_group_sqr(group, ret, ret);

    goo_group_one_mul(group, ret, w1, p1, n1);
    goo_group_one_mul(group, ret, w2, p2, n2);
  }

  return 1;
}

static int
goo_group_recon(
  goo_group_t *group,
  mpz_t ret,
  const mpz_t b1,
  const mpz_t b1_inv,
  const mpz_t e1,
  const mpz_t b2,
  const mpz_t b2_inv,
  const mpz_t e2,
  const mpz_t e3,
  const mpz_t e4
) {
  if (!goo_group_pow2(group, ret, b1, b1_inv, e1, b2, b2_inv, e2))
    return 0;

  if (!goo_group_powgh(group, group->gh, e3, e4))
    return 0;

  goo_group_mul(group, ret, ret, group->gh);
  goo_group_reduce(group, ret, ret);

  return 1;
}

static void
goo_hash_item(
  goo_sha256_t *ctx,
  const mpz_t n,
  unsigned char *size,
  unsigned char *buf
) {
  size_t len = 0;

  goo_mpz_export(&buf[0], &len, n);

  assert(len <= 768);

  // Commit to sign.
  if (mpz_cmp_ui(n, 0) < 0)
    len |= 0x8000;

  size[0] = len;
  size[1] = len >> 8;

  len &= ~0x8000;

  goo_sha256_update(ctx, size, 2);
  goo_sha256_update(ctx, buf, len);
}

static void
goo_hash_all(
  unsigned char *out,
  goo_group_t *group,
  const mpz_t C1,
  const mpz_t C2,
  const mpz_t t,
  const mpz_t A,
  const mpz_t B,
  const mpz_t C,
  const mpz_t D,
  const mpz_t msg
) {
  unsigned char *size = &group->slab[0];
  unsigned char *buf = &group->slab[2];

  goo_sha256_t ctx;
  goo_sha256_init(&ctx);
  goo_sha256_update(&ctx, (void *)goo_prefix, sizeof(goo_prefix) - 1);

  goo_hash_item(&ctx, group->n, size, buf);
  goo_hash_item(&ctx, group->g, size, buf);
  goo_hash_item(&ctx, group->h, size, buf);
  goo_hash_item(&ctx, C1, size, buf);
  goo_hash_item(&ctx, C2, size, buf);
  goo_hash_item(&ctx, t, size, buf);
  goo_hash_item(&ctx, A, size, buf);
  goo_hash_item(&ctx, B, size, buf);
  goo_hash_item(&ctx, C, size, buf);
  goo_hash_item(&ctx, D, size, buf);
  goo_hash_item(&ctx, msg, size, buf);

  goo_sha256_final(&ctx, out);
}

static int
goo_is_prime(const mpz_t p) {
  return mpz_probab_prime_p(p, 2) != 0 ? 1 : 0;
}

static int
goo_next_prime(mpz_t ret, const mpz_t p, unsigned long maxinc) {
  unsigned long inc = 0;

  mpz_set(ret, p);

  if (mpz_even_p(ret)) {
    inc += 1;
    mpz_add_ui(ret, ret, 1);
  }

  while (!goo_is_prime(ret)) {
    if (maxinc != 0 && inc > maxinc)
      break;
    mpz_add_ui(ret, ret, 2);
    inc += 2;
  }

  if (maxinc != 0 && inc > maxinc)
    return 0;

  return 1;
}

static int
goo_group_fs_chal(
  goo_group_t *group,
  mpz_t chal,
  mpz_t ell,
  const mpz_t C1,
  const mpz_t C2,
  const mpz_t t,
  const mpz_t A,
  const mpz_t B,
  const mpz_t C,
  const mpz_t D,
  const mpz_t msg,
  int verify
) {
  unsigned char key[32];

  goo_hash_all(&key[0], group, C1, C2, t, A, B, C, D, msg);

  goo_prng_seed(&group->prng, &key[0]);
  goo_prng_getrandbits(&group->prng, chal, GOO_CHAL_BITS);
  goo_prng_getrandbits(&group->prng, ell, GOO_CHAL_BITS);

  if (!verify) {
    // For prover, call nextPrime on ell_r to get ell.
    if (!goo_next_prime(ell, ell, GOO_ELLDIFF_MAX)) {
      mpz_set_ui(chal, 0);
      mpz_set_ui(ell, 0);
      return 0;
    }
  }

  return 1;
}

static int
goo_group_verify(
  goo_group_t *group,

  // msg
  const mpz_t msg,

  // pubkey
  const mpz_t C1,
  const mpz_t C2,
  const mpz_t t,

  // sigma
  const mpz_t chal,
  const mpz_t ell,
  const mpz_t Aq,
  const mpz_t Bq,
  const mpz_t Cq,
  const mpz_t Dq,

  // z_prime
  const mpz_t z_w,
  const mpz_t z_w2,
  const mpz_t z_s1,
  const mpz_t z_a,
  const mpz_t z_an,
  const mpz_t z_s1w,
  const mpz_t z_sa
) {
  mpz_t *C1_inv = &group->C1_inv;
  mpz_t *C2_inv = &group->C2_inv;
  mpz_t *Aq_inv = &group->Aq_inv;
  mpz_t *Bq_inv = &group->Bq_inv;
  mpz_t *Cq_inv = &group->Cq_inv;
  mpz_t *A = &group->A;
  mpz_t *B = &group->B;
  mpz_t *C = &group->C;
  mpz_t *D = &group->D;
  mpz_t *z_w2_m_an = &group->z_w2_m_an;
  mpz_t *tmp = &group->tmp;
  mpz_t *chal_out = &group->chal_out;
  mpz_t *ell_r_out = &group->ell_r_out;
  mpz_t *elldiff = &group->elldiff;

  // `t` must be one of the small primes in our list.
  int found = 0;

  for (int i = 0; i < 168; i++) {
    if (mpz_cmp_ui(t, goo_primes[i]) == 0) {
      found = 1;
      break;
    }
  }

  if (!found)
    return 0;

  // All group elements must be the "canonical"
  // element of the quotient group (Z/n)/{1,-1}.
  if (!goo_group_is_reduced(group, C1)
      || !goo_group_is_reduced(group, C2)
      || !goo_group_is_reduced(group, Aq)
      || !goo_group_is_reduced(group, Bq)
      || !goo_group_is_reduced(group, Cq)) {
    return 0;
  }

  // compute inverses of C1, C2, Aq, Bq, Cq
  if (!goo_group_inv5(group, *C1_inv, *C2_inv, *Aq_inv,
                      *Bq_inv, *Cq_inv, C1, C2, Aq, Bq, Cq)) {
    return 0;
  }

  // Step 1: reconstruct A, B, C, and D from signature.
  if (!goo_group_recon(group, *A, Aq, *Aq_inv, ell,
                       *C2_inv, C2, chal, z_w, z_s1)) {
    return 0;
  }

  if (!goo_group_recon(group, *B, Bq, *Bq_inv, ell,
                       *C2_inv, C2, z_w, z_w2, z_s1w)) {
    return 0;
  }

  if (!goo_group_recon(group, *C, Cq, *Cq_inv, ell,
                       *C1_inv, C1, z_a, z_an, z_sa)) {
    return 0;
  }

  // Make sure sign of (z_w2 - z_an) is positive.
  mpz_sub(*z_w2_m_an, z_w2, z_an);

  mpz_mul(*D, Dq, ell);
  mpz_add(*D, *D, *z_w2_m_an);
  mpz_mul(*tmp, t, chal);
  mpz_sub(*D, *D, *tmp);

  if (mpz_cmp_ui(*z_w2_m_an, 0) < 0)
    mpz_add(*D, *D, ell);

  // Step 2: recompute implicitly claimed V message, viz., chal and ell.
  goo_group_fs_chal(group, *chal_out, *ell_r_out,
                    C1, C2, t, *A, *B, *C, *D,
                    msg, 1);

  // Final checks.
  // chal has to match
  // AND 0 <= (ell_r_out - ell) <= elldiff_max
  // AND ell is prime
  mpz_sub(*elldiff, ell, *ell_r_out);

  if (mpz_cmp(chal, *chal_out) != 0
      || mpz_cmp_ui(*elldiff, 0) < 0
      || mpz_cmp_ui(*elldiff, GOO_ELLDIFF_MAX) > 0
      || !goo_is_prime(ell)) {
    return 0;
  }

  return 1;
}

static int
goo_group_randbits(goo_group_t *group, mpz_t ret, size_t size) {
  unsigned char key[32];

  if (!goo_random(&key[0], 32))
    return 0;

  goo_prng_seed(&group->prng, &key[0]);
  goo_prng_getrandbits(&group->prng, ret, size);

  return 1;
}

static int
goo_group_expand_sprime(goo_group_t *group, mpz_t s, const mpz_t s_prime) {
  unsigned char key[32];
  size_t pos = 32 - goo_mpz_bytesize(s);

  if (pos > 32) // Overflow
    return 0;

  memset(&key[0], 0x00, pos);
  goo_mpz_export(&key[pos], NULL, s_prime);

  goo_prng_seed(&group->prng, &key[0]);
  goo_prng_getrandbits(&group->prng, s, GOO_EXPONENT_SIZE);

  return 1;
}

static int
goo_group_challenge(
  goo_group_t *group,
  mpz_t s_prime,
  mpz_t C1,
  const mpz_t n
) {
  mpz_t s;
  mpz_init(s);

  if (!goo_group_randbits(group, s_prime, 256))
    goto fail;

  if (!goo_group_expand_sprime(group, s, s_prime))
    goto fail;

  // The challenge: a commitment to the RSA modulus.
  if (!goo_group_powgh(group, C1, n, s))
    goto fail;

  goo_group_reduce(group, C1, C1);

  mpz_clear(s);
  return 1;
fail:
  mpz_clear(s);
  return 0;
}

static void
goo_sig_init(goo_sig_t *sig) {
  mpz_init(sig->C2);
  mpz_init(sig->t);
  mpz_init(sig->chal);
  mpz_init(sig->ell);
  mpz_init(sig->Aq);
  mpz_init(sig->Bq);
  mpz_init(sig->Cq);
  mpz_init(sig->Dq);
  mpz_init(sig->z_w);
  mpz_init(sig->z_w2);
  mpz_init(sig->z_s1);
  mpz_init(sig->z_a);
  mpz_init(sig->z_an);
  mpz_init(sig->z_s1w);
  mpz_init(sig->z_sa);
}

static void
goo_sig_uninit(goo_sig_t *sig) {
  mpz_clear(sig->C2);
  mpz_clear(sig->t);
  mpz_clear(sig->chal);
  mpz_clear(sig->ell);
  mpz_clear(sig->Aq);
  mpz_clear(sig->Bq);
  mpz_clear(sig->Cq);
  mpz_clear(sig->Dq);
  mpz_clear(sig->z_w);
  mpz_clear(sig->z_w2);
  mpz_clear(sig->z_s1);
  mpz_clear(sig->z_a);
  mpz_clear(sig->z_an);
  mpz_clear(sig->z_s1w);
  mpz_clear(sig->z_sa);
}

static void
goo_factor_twos(mpz_t d, unsigned long *s, const mpz_t n) {
  mpz_set(d, n);
  *s = 0;

  while (mpz_even_p(d)) {
    mpz_fdiv_q_ui(d, d, 2);
    *s += 1;
  }
}

static int
goo_mod_sqrtp(mpz_t ret, const mpz_t n, const mpz_t p) {
  if (mpz_cmp_ui(p, 0) < 0)
    return 0;

  unsigned long s;
  mpz_t nn, t, Q, w, y, q, y_save;
  mpz_inits(nn, t, Q, w, y, q, y_save, NULL);

  mpz_set(nn, n);
  mpz_mod(nn, nn, p);

  if (mpz_cmp_ui(nn, 0) == 0) {
    mpz_set_ui(ret, 0);
    goto succeed;
  }

  if (mpz_jacobi(nn, p) == -1)
    goto fail;

  mpz_mod_ui(t, p, 4);

  if (mpz_cmp_ui(t, 3) == 0) {
    mpz_set(t, p);
    mpz_add_ui(t, t, 1);
    mpz_fdiv_q_ui(t, t, 4);
    mpz_powm(ret, nn, t, p);
    goto succeed;
  }

  // Factor out 2^s from p - 1.
  mpz_set(t, p);
  mpz_sub_ui(t, t, 1);

  goo_factor_twos(Q, &s, t);

  // Find a non-residue mod p.
  mpz_set_ui(w, 2);

  while (mpz_jacobi(w, p) != -1)
    mpz_add_ui(w, w, 1);

  mpz_powm(w, w, Q, p);
  mpz_powm(y, nn, Q, p);

  mpz_set(t, Q);
  mpz_add_ui(t, t, 1);
  mpz_fdiv_q_ui(t, t, 2);
  mpz_powm(q, nn, t, p);

  for (;;) {
    unsigned long i = 0;

    mpz_set(y_save, y);

    while (i < s && mpz_cmp_ui(y, 1) != 0) {
      mpz_powm_ui(y, y, 2, p);
      i += 1;
    }

    if (i == 0)
      break;

    if (i == s)
      goto fail;

    mpz_set_ui(t, 1);
    mpz_mul_2exp(t, t, s - i - 1);
    mpz_powm(w, w, t, p);

    s = i;

    mpz_mul(q, q, w);
    mpz_mod(q, q, p);

    mpz_powm_ui(w, w, 2, p);

    mpz_set(y, y_save);
    mpz_mul(y, y, w);
    mpz_mod(y, y, p);
  }

  mpz_set(t, p);
  mpz_fdiv_q_ui(t, t, 2);

  if (mpz_cmp(q, t) > 0)
    mpz_sub(q, p, q);

  mpz_set(t, q);
  mpz_mul(t, t, t);
  mpz_mod(t, t, p);

  assert(mpz_cmp(nn, t) == 0);

  mpz_set(ret, q);

succeed:
  mpz_clears(nn, t, Q, w, y, q, y_save, NULL);
  return 1;
fail:
  mpz_clears(nn, t, Q, w, y, q, y_save, NULL);
  return 0;
}

static int
goo_mod_sqrtn(mpz_t ret, const mpz_t x, const mpz_t p, const mpz_t q) {
  mpz_t sqrt_p, sqrt_q, mp, mq, xx, xy;
  mpz_inits(sqrt_p, sqrt_q, mp, mq, xx, xy, NULL);

  if (!goo_mod_sqrtp(sqrt_p, x, p)
      || !goo_mod_sqrtp(sqrt_q, x, q)) {
    goto fail;
  }

  mpz_gcdext(ret, mp, mq, p, q);

  mpz_set(xx, sqrt_q);
  mpz_mul(xx, xx, mp);
  mpz_mul(xx, xx, p);

  mpz_set(xy, sqrt_p);
  mpz_mul(xy, xy, mq);
  mpz_mul(xy, xy, q);

  mpz_add(xx, xx, xy);

  mpz_set(xy, p);
  mpz_mul(xy, xy, q);

  mpz_mod(ret, xx, xy);

  mpz_clears(sqrt_p, sqrt_q, mp, mq, xx, xy, NULL);
  return 1;

fail:
  mpz_clears(sqrt_p, sqrt_q, mp, mq, xx, xy, NULL);
  return 0;
}

static int
goo_group_rand_scalar(goo_group_t *group, mpz_t ret) {
  size_t size = group->rand_bits;

  if (size > GOO_EXPONENT_SIZE)
    size = GOO_EXPONENT_SIZE;

  unsigned char key[32];

  if (!goo_random(&key[0], 32))
    return 0;

  goo_prng_seed(&group->prng, &key[0]);
  goo_prng_getrandbits(&group->prng, ret, size);

  return 1;
}

static int
goo_group_sign(
  goo_group_t *group,
  goo_sig_t *sig,
  const mpz_t msg,
  const mpz_t s_prime,
  const mpz_t C1,
  const mpz_t n,
  const mpz_t p,
  const mpz_t q
) {
  goo_sig_init(sig);

  mpz_t *C2 = &sig->C2;
  mpz_t *t = &sig->t;
  mpz_t *chal = &sig->chal;
  mpz_t *ell = &sig->ell;
  mpz_t *Aq = &sig->Aq;
  mpz_t *Bq = &sig->Bq;
  mpz_t *Cq = &sig->Cq;
  mpz_t *Dq = &sig->Dq;
  mpz_t *z_w = &sig->z_w;
  mpz_t *z_w2 = &sig->z_w2;
  mpz_t *z_s1 = &sig->z_s1;
  mpz_t *z_a = &sig->z_a;
  mpz_t *z_an = &sig->z_an;
  mpz_t *z_s1w = &sig->z_s1w;
  mpz_t *z_sa = &sig->z_sa;

  mpz_t *s = &group->Aq;
  mpz_t *w = &group->Bq;
  mpz_t *a = &group->Cq;
  mpz_t *s1 = &group->Dq;

  mpz_t *x = &group->Aq_inv;
  mpz_t *y = &group->Bq_inv;
  mpz_t *z = &group->Cq_inv;
  mpz_t *xx = &group->chal_out;
  mpz_t *yy = &group->ell_r_out;

  mpz_t *C1_inv = &group->C1_inv;
  mpz_t *C2_inv = &group->C2_inv;

  mpz_t *r_w = &group->z_w;
  mpz_t *r_w2 = &group->z_w2;
  mpz_t *r_s1 = &group->z_s1;
  mpz_t *r_a = &group->z_a;
  mpz_t *r_an = &group->z_an;
  mpz_t *r_s1w = &group->z_s1w;
  mpz_t *r_sa = &group->z_sa;

  mpz_t *A = &group->A;
  mpz_t *B = &group->B;
  mpz_t *C = &group->C;
  mpz_t *D = &group->D;

  int r = 0;

  // s = expand_sprime(s_prime)
  if (!goo_group_expand_sprime(group, *s, s_prime))
    goto fail;

  // x = powgh(n, s)
  if (!goo_group_powgh(group, *x, n, *s))
    goto fail;

  goo_group_reduce(group, *x, *x);

  if (mpz_cmp(C1, *x) != 0) {
    // C1 does not commit to our RSA modulus with opening s.
    goto fail;
  }

  // Preliminaries: compute values P needs to run the ZKPOK.
  // Find `t`.
  int found = 0;

  for (int i = 0; i < (int)sizeof(goo_primes); i++) {
    // t = small_primes[i]
    mpz_set_ui(*t, goo_primes[i]);

    // w = mod_sqrtn(t, p, q)
    if (goo_mod_sqrtn(*w, *t, p, q)) {
      found = 1;
      break;
    }
  }

  if (!found) {
    // No prime quadratic residue less than 1000 mod N!
    goto fail;
  }

  // a = (w ** 2 - t) / n
  mpz_set(*a, *w);
  mpz_pow_ui(*a, *a, 2);
  mpz_sub(*a, *a, *t);
  mpz_fdiv_q(*a, *a, n);

  // x = a * n
  mpz_set(*x, *a);
  mpz_mul(*x, *x, n);

  // y = w ** 2 - t
  mpz_set(*y, *w);
  mpz_pow_ui(*y, *y, 2);
  mpz_sub(*y, *y, *t);

  // if x != y
  if (mpz_cmp(*x, *y) != 0) {
    // w^2 - t was not divisible by N!
    goto fail;
  }

  // Commitment to `w`.
  // s1 = rand_scalar()
  // C2 = powgh(w, s1)
  if (!goo_group_rand_scalar(group, *s1)
      || !goo_group_powgh(group, *C2, *w, *s1)) {
    goto fail;
  }

  goo_group_reduce(group, *C2, *C2);

  // Inverses of `C1` and `C2`.
  // [C1_inv, C2_inv] = inv2(C1, C2)
  if (!goo_group_inv2(group, *C1_inv, *C2_inv, C1, *C2))
    goto fail;

  // P's first message: commit to randomness.
  // P's randomness (except for r_s1; see "V's message", below).
  // [r_w, r_w2, r_a, r_an, r_s1w, r_sa] = rand_scalar(7)
  if (!goo_group_rand_scalar(group, *r_w)
      || !goo_group_rand_scalar(group, *r_w2)
      || !goo_group_rand_scalar(group, *r_a)
      || !goo_group_rand_scalar(group, *r_an)
      || !goo_group_rand_scalar(group, *r_s1w)
      || !goo_group_rand_scalar(group, *r_sa)) {
    goto fail;
  }

  // Prevent D from being negative.
  if (mpz_cmp(*r_w2, *r_an) < 0) {
    // [r_w2, r_an] = [r_an, r_w2]
    mpz_set(*x, *r_w2);
    mpz_set(*r_w2, *r_an);
    mpz_set(*r_an, *x);
  }

  // P's first message (except for A; see "V's message", below).
  // B = pow(C2_inv, C2, r_w) * powgh(r_w2, r_s1w)
  goo_group_pow(group, *x, *C2_inv, *C2, *r_w);

  if (!goo_group_powgh(group, *y, *r_w2, *r_s1w))
    goto fail;

  goo_group_mul(group, *B, *x, *y);
  goo_group_reduce(group, *B, *B);

  // C = pow(C1_inv, C1, r_a) * powgh(r_an, r_sa)
  goo_group_pow(group, *x, *C1_inv, C1, *r_a);

  if (!goo_group_powgh(group, *y, *r_an, *r_sa))
    goto fail;

  goo_group_mul(group, *C, *x, *y);
  goo_group_reduce(group, *C, *C);

  // D = r_w2 - r_an
  mpz_sub(*D, *r_w2, *r_an);

  // V's message: random challenge and random prime.
  while (r == 0 || goo_mpz_bitlen(*ell) != 128) {
    // Randomize the signature until Fiat-Shamir
    // returns an admissable ell. Note that it's
    // not necessary to re-start the whole
    // signature! Just pick a new r_s1, which
    // only requires re-computing A.
    // r_s1 = rand_scalar()
    // A = powgh(r_w, r_s1)
    if (!goo_group_rand_scalar(group, *r_s1)
        || !goo_group_powgh(group, *A, *r_w, *r_s1)) {
      goto fail;
    }

    goo_group_reduce(group, *A, *A);

    // [chal, ell] = fs_chal(C1, C2, t, A, B, C, D, msg)
    r = goo_group_fs_chal(group, *chal, *ell, C1, *C2,
                          *t, *A, *B, *C, *D, msg, 0);
  }

  // P's second message: compute quotient message.
  // Compute z' = c*(w, w2, s1, a, an, s1w, sa)
  //            + (r_w, r_w2, r_s1, r_a, r_an, r_s1w, r_sa)
  // z_w = chal * w + r_w
  mpz_mul(*z_w, *chal, *w);
  mpz_add(*z_w, *z_w, *r_w);
  // z_w2 = chal * w * w + r_w2
  mpz_mul(*z_w2, *chal, *w);
  mpz_mul(*z_w2, *z_w2, *w);
  mpz_add(*z_w2, *z_w2, *r_w2);
  // z_s1 = chal * s1 + r_s1
  mpz_mul(*z_s1, *chal, *s1);
  mpz_add(*z_s1, *z_s1, *r_s1);
  // z_a = chal * a + r_a
  mpz_mul(*z_a, *chal, *a);
  mpz_add(*z_a, *z_a, *r_a);
  // z_an = chal * a * n + r_an
  mpz_mul(*z_an, *chal, *a);
  mpz_mul(*z_an, *z_an, n);
  mpz_add(*z_an, *z_an, *r_an);
  // z_s1w = chal * s1 * w + r_s1w
  mpz_mul(*z_s1w, *chal, *s1);
  mpz_mul(*z_s1w, *z_s1w, *w);
  mpz_add(*z_s1w, *z_s1w, *r_s1w);
  // z_sa = chal * s * a + r_sa
  mpz_mul(*z_sa, *chal, *s);
  mpz_mul(*z_sa, *z_sa, *a);
  mpz_add(*z_sa, *z_sa, *r_sa);

  // Compute quotient commitments.

  // Aq = powgh(z_w / ell, z_s1 / ell)
  mpz_fdiv_q(*x, *z_w, *ell);
  mpz_fdiv_q(*y, *z_s1, *ell);

  if (!goo_group_powgh(group, *Aq, *x, *y))
    goto fail;

  goo_group_reduce(group, *Aq, *Aq);

  // Bq = pow(C2_inv, C2, z_w / ell) * powgh(z_w2 / ell, z_s1w / ell)
  mpz_fdiv_q(*x, *z_w, *ell);
  mpz_fdiv_q(*y, *z_w2, *ell);
  mpz_fdiv_q(*z, *z_s1w, *ell);
  goo_group_pow(group, *xx, *C2_inv, *C2, *x);

  if (!goo_group_powgh(group, *yy, *y, *z))
    goto fail;

  goo_group_mul(group, *Bq, *xx, *yy);
  goo_group_reduce(group, *Bq, *Bq);

  // Cq = pow(C1_inv, C2, z_a / ell) * powgh(z_an / ell, z_sa / ell)
  mpz_fdiv_q(*x, *z_a, *ell);
  mpz_fdiv_q(*y, *z_an, *ell);
  mpz_fdiv_q(*z, *z_sa, *ell);
  goo_group_pow(group, *xx, *C1_inv, *C2, *x);

  if (!goo_group_powgh(group, *yy, *y, *z))
    goto fail;

  goo_group_mul(group, *Cq, *xx, *yy);
  goo_group_reduce(group, *Cq, *Cq);

  // Dq = (z_w2 - z_an) / ell
  mpz_sub(*Dq, *z_w2, *z_an);
  mpz_fdiv_q(*Dq, *Dq, *ell);

  mpz_mod(*z_w, *z_w, *ell);
  mpz_mod(*z_w2, *z_w2, *ell);
  mpz_mod(*z_s1, *z_s1, *ell);
  mpz_mod(*z_a, *z_a, *ell);
  mpz_mod(*z_an, *z_an, *ell);
  mpz_mod(*z_s1w, *z_s1w, *ell);
  mpz_mod(*z_sa, *z_sa, *ell);

  // z_prime: (z_w, z_w2, z_s1, z_a, z_an, z_s1w, z_sa).
  // Signature: (chal, ell, Aq, Bq, Cq, Dq, z_prime).

  return 1;
fail:
  goo_sig_uninit(sig);
  return 0;
}

/*
 * Expose
 */

int
goo_init(
  goo_ctx_t *ctx,
  const unsigned char *n,
  size_t n_len,
  unsigned long g,
  unsigned long h,
  unsigned long modbits
) {
  if (ctx == NULL || n == NULL)
    return 0;

  if (modbits != 0) {
    if (modbits < 1024 || modbits > 4096)
      return 0;
  }

  return goo_group_init(ctx, n, n_len, g, h, modbits);
}

void
goo_uninit(goo_ctx_t *ctx) {
  if (ctx != NULL)
    goo_group_uninit(ctx);
}

#define goo_write_item(n) do {             \
  size_t nsize = goo_mpz_bytesize((n));    \
  if (nsize > 768) {                       \
    free(data);                            \
    goto fail;                             \
  }                                        \
  data[pos++] = nsize;                     \
  data[pos++] = nsize >> 8;                \
  goo_mpz_export(&data[pos], NULL, (n));   \
  pos += nsize;                            \
} while (0)

#define goo_write_final() \
  assert(pos == len)

#define goo_read_item(n) do {              \
  if (pos + 2 > sig_len)                   \
    return 0;                              \
                                           \
  len = (sig[pos + 1] * 0x100) | sig[pos]; \
                                           \
  if (len > 768)                           \
    return 0;                              \
                                           \
  pos += 2;                                \
                                           \
  if (pos + len > sig_len)                 \
    return 0;                              \
                                           \
  goo_mpz_import((n), &sig[pos], len);     \
  pos += len;                              \
} while (0)                                \

#define goo_read_final() do { \
  assert(pos <= sig_len);     \
                              \
  if (pos != sig_len)         \
    return 0;                 \
} while (0)                   \

int
goo_challenge(
  goo_ctx_t *ctx,
  unsigned char **s_prime,
  size_t *s_prime_len,
  unsigned char **C1,
  size_t *C1_len,
  const unsigned char *n,
  size_t n_len
) {
  if (ctx == NULL
      || s_prime == NULL
      || s_prime_len == NULL
      || C1 == NULL
      || C1_len == NULL
      || n == NULL) {
    return 0;
  }

  if (n_len > 768)
    return 0;

  mpz_t nn, spn;
  mpz_inits(nn, spn, NULL);

  goo_mpz_import(nn, n, n_len);

  if (!goo_group_challenge(ctx, spn, ctx->C1, nn))
    goto fail;

  *s_prime_len = 32;
  *s_prime = goo_mpz_pad(NULL, *s_prime_len, spn);
  *C1_len = goo_mpz_bytesize(ctx->n);
  *C1 = goo_mpz_pad(NULL, *C1_len, ctx->C1);

  // *s_prime = goo_mpz_export(NULL, s_prime_len, spn);
  // *C1 = goo_mpz_export(NULL, C1_len, ctx->C1);

  if (*s_prime == NULL || *C1 == NULL)
    goto fail;

  mpz_clears(nn, spn, NULL);
  return 1;
fail:
  mpz_clears(nn, spn, NULL);
  return 0;
}

int
goo_sign(
  goo_ctx_t *ctx,
  unsigned char **out,
  size_t *out_len,
  const unsigned char *msg,
  size_t msg_len,
  const unsigned char *s_prime,
  size_t s_prime_len,
  const unsigned char *C1,
  size_t C1_len,
  const unsigned char *n,
  size_t n_len,
  const unsigned char *p,
  size_t p_len,
  const unsigned char *q,
  size_t q_len
) {
  if (ctx == NULL
      || out == NULL
      || out_len == NULL
      || msg == NULL
      || s_prime == NULL
      || C1 == NULL
      || n == NULL
      || p == NULL
      || q == NULL) {
    return 0;
  }

  if (msg_len > 768
      || s_prime_len > 768
      || C1_len > 768
      || n_len > 768
      || p_len > 768
      || q_len > 768) {
    return 0;
  }

  mpz_t spn, nn, pn, qn;
  mpz_inits(spn, nn, pn, qn, NULL);

  goo_mpz_import(ctx->msg, msg, msg_len);
  goo_mpz_import(spn, s_prime, s_prime_len);
  goo_mpz_import(ctx->C1, C1, C1_len);
  goo_mpz_import(nn, n, n_len);
  goo_mpz_import(pn, p, p_len);
  goo_mpz_import(qn, q, q_len);

  goo_sig_t sig;

  if (!goo_group_sign(ctx, &sig, ctx->msg,
                      spn, ctx->C1, nn, pn, qn)) {
    goto fail;
  }

  size_t pos = 0;
  size_t len = 0;

  len += 2 + goo_mpz_bytesize(sig.C2);
  len += 2 + goo_mpz_bytesize(sig.t);

  len += 2 + goo_mpz_bytesize(sig.chal);
  len += 2 + goo_mpz_bytesize(sig.ell);
  len += 2 + goo_mpz_bytesize(sig.Aq);
  len += 2 + goo_mpz_bytesize(sig.Bq);
  len += 2 + goo_mpz_bytesize(sig.Cq);
  len += 2 + goo_mpz_bytesize(sig.Dq);

  len += 2 + goo_mpz_bytesize(sig.z_w);
  len += 2 + goo_mpz_bytesize(sig.z_w2);
  len += 2 + goo_mpz_bytesize(sig.z_s1);
  len += 2 + goo_mpz_bytesize(sig.z_a);
  len += 2 + goo_mpz_bytesize(sig.z_an);
  len += 2 + goo_mpz_bytesize(sig.z_s1w);
  len += 2 + goo_mpz_bytesize(sig.z_sa);

  unsigned char *data = malloc(len);

  if (data == NULL)
    goto fail;

  goo_write_item(sig.C2);
  goo_write_item(sig.t);

  goo_write_item(sig.chal);
  goo_write_item(sig.ell);
  goo_write_item(sig.Aq);
  goo_write_item(sig.Bq);
  goo_write_item(sig.Cq);
  goo_write_item(sig.Dq);

  goo_write_item(sig.z_w);
  goo_write_item(sig.z_w2);
  goo_write_item(sig.z_s1);
  goo_write_item(sig.z_a);
  goo_write_item(sig.z_an);
  goo_write_item(sig.z_s1w);
  goo_write_item(sig.z_sa);

  goo_write_final();

  *out = data;
  *out_len = len;

  goo_sig_uninit(&sig);
  mpz_clears(spn, nn, pn, qn, NULL);

  return 1;
fail:
  goo_sig_uninit(&sig);
  mpz_clears(spn, nn, pn, qn, NULL);
  return 0;
}

int
goo_verify(
  goo_ctx_t *ctx,
  const unsigned char *msg,
  size_t msg_len,
  const unsigned char *sig,
  size_t sig_len,
  const unsigned char *C1,
  size_t C1_len
) {
  if (ctx == NULL || msg == NULL || sig == NULL || C1 == NULL)
    return 0;

  if (msg_len > 768)
    return 0;

  if (C1_len > 768)
    return 0;

  goo_mpz_import(ctx->msg, msg, msg_len);
  goo_mpz_import(ctx->C1, C1, C1_len);

  size_t pos = 0;
  size_t len = 0;

  goo_read_item(ctx->C2);
  goo_read_item(ctx->t);

  goo_read_item(ctx->chal);
  goo_read_item(ctx->ell);
  goo_read_item(ctx->Aq);
  goo_read_item(ctx->Bq);
  goo_read_item(ctx->Cq);
  goo_read_item(ctx->Dq);

  goo_read_item(ctx->z_w);
  goo_read_item(ctx->z_w2);
  goo_read_item(ctx->z_s1);
  goo_read_item(ctx->z_a);
  goo_read_item(ctx->z_an);
  goo_read_item(ctx->z_s1w);
  goo_read_item(ctx->z_sa);

  goo_read_final();

  return goo_group_verify(
    ctx,

    // msg
    ctx->msg,

    // pubkey
    ctx->C1,
    ctx->C2,
    ctx->t,

    // sigma
    ctx->chal,
    ctx->ell,
    ctx->Aq,
    ctx->Bq,
    ctx->Cq,
    ctx->Dq,

    // z_prime
    ctx->z_w,
    ctx->z_w2,
    ctx->z_s1,
    ctx->z_a,
    ctx->z_an,
    ctx->z_s1w,
    ctx->z_sa
  );
}
