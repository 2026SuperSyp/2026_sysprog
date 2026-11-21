/* 스테이지용 난수. 상태를 직접 들고 있어서 초파리용, 유저용을 따로 둘 수 있다.
 * 씨앗 섞기는 splitmix64, 수열은 xorshift64* 알고리즘이다. */
#include "stage.h"

void stage_rng_seed(stage_rng_t *r, uint32_t seed)
{
    uint64_t z = (uint64_t)seed + 0x9E3779B97F4A7C15ULL;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    z ^= z >> 31;
    r->s = z ? z : 0x1234567ULL;       /* xorshift는 0에서 시작하면 안 된다 */
}

uint32_t stage_rng_u32(stage_rng_t *r)
{
    uint64_t x = r->s;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    r->s = x;
    return (uint32_t)((x * 0x2545F4914F6CDD1DULL) >> 32);
}

float stage_rng_float(stage_rng_t *r)
{
    return (float)(stage_rng_u32(r) >> 8) / 16777216.0f;   /* 24비트: 0.0 이상 1.0 미만 */
}

int stage_rng_range(stage_rng_t *r, int lo, int hi)
{
    if (hi <= lo) return lo;
    return lo + (int)(stage_rng_u32(r) % (uint32_t)(hi - lo + 1));
}
