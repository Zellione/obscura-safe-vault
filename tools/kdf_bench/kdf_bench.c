/* Break-in effort Phase 5 — Argon2id cost benchmark (standalone).
 *
 * Times ONE Argon2id derivation with the vault's PRODUCTION parameters
 * (src/crypto/kdf.h DEFAULT_KDF_PARAMS: t=3, m=64 MiB, p=1) using the exact
 * Monocypher call the app makes (src/crypto/kdf.cpp derive_key), so the
 * number is the honest per-guess cost of an offline attacker running the
 * KDF per password candidate. NOT part of the main osv build or CI.
 */
#define _POSIX_C_SOURCE 199309L /* clock_gettime under -std=c17 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <monocypher.h>

#define KEY_SIZE    32u
#define SALT_SIZE   16u
#define PASS_SIZE   32u
#define NB_BLOCKS   65536u /* 64 MiB (KiB == blocks) */
#define NB_PASSES   3u
#define NB_LANES    1u
#define DEFAULT_ITERS 20u

static double now_seconds(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

int main(int argc, char **argv)
{
    unsigned iters = DEFAULT_ITERS;
    if (argc > 1) {
        char *end = NULL;
        const unsigned long v = strtoul(argv[1], &end, 10);
        if (!end || *end != '\0' || v < 1 || v > 1000) {
            fprintf(stderr, "usage: %s [iterations 1..1000]\n", argv[0]);
            return 2;
        }
        iters = (unsigned)v;
    }

    uint8_t *work_area = (uint8_t *)calloc((size_t)NB_BLOCKS * 1024u, 1);
    if (!work_area) {
        fprintf(stderr, "failed to allocate %u MiB work area\n", NB_BLOCKS / 1024);
        return 1;
    }

    /* Fixed representative inputs; the KDF cost is independent of their value. */
    uint8_t pass[PASS_SIZE];
    uint8_t salt[SALT_SIZE];
    for (unsigned i = 0; i < PASS_SIZE; ++i) pass[i] = (uint8_t)(i * 7u + 1u);
    for (unsigned i = 0; i < SALT_SIZE; ++i) salt[i] = (uint8_t)(0xA5u ^ i);

    const crypto_argon2_config config = {
        .algorithm = CRYPTO_ARGON2_ID,
        .nb_blocks = NB_BLOCKS,
        .nb_passes = NB_PASSES,
        .nb_lanes  = NB_LANES,
    };
    const crypto_argon2_inputs inputs = {
        .pass      = pass,
        .salt      = salt,
        .pass_size = PASS_SIZE,
        .salt_size = SALT_SIZE,
    };

    /* Warm-up: page-in the work area, settle frequency scaling. */
    uint8_t hash[KEY_SIZE];
    crypto_argon2(hash, KEY_SIZE, work_area, config, inputs, crypto_argon2_no_extras);

    volatile uint8_t sink = 0;
    double t_min = 1e300, t_max = 0.0, t_sum = 0.0;
    for (unsigned i = 0; i < iters; ++i) {
        const double t0 = now_seconds();
        crypto_argon2(hash, KEY_SIZE, work_area, config, inputs, crypto_argon2_no_extras);
        const double dt = now_seconds() - t0;
        sink ^= hash[0] ^ hash[KEY_SIZE - 1]; /* defeat dead-code elimination */
        if (dt < t_min) t_min = dt;
        if (dt > t_max) t_max = dt;
        t_sum += dt;
        printf("iter %3u  %10.3f ms\n", i + 1, dt * 1e3);
    }

    const double mean = t_sum / (double)iters;
    const double bytes_touched = (double)NB_BLOCKS * 1024.0 * (double)NB_PASSES;
    printf("\nparams: argon2id t=%u m=%u MiB p=%u (vault production DEFAULT_KDF_PARAMS)\n",
           NB_PASSES, NB_BLOCKS / 1024, NB_LANES);
    printf("iters: %u  (min %.3f ms, mean %.3f ms, max %.3f ms)\n",
           iters, t_min * 1e3, mean * 1e3, t_max * 1e3);
    printf("throughput: %.2f GB/s (t*m per pass)\n", bytes_touched / mean / 1e9);
    printf("guess rate: %.3f guesses/s\n", 1.0 / mean);
    (void)sink;
    free(work_area);
    return 0;
}
