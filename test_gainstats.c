// Test the gain statistics / AGC logic extracted from gainStatistics().
// Tests behavior with various noise/loudness conditions.
#include <stdio.h>
#include <stdint.h>
#include <string.h>

typedef struct {
    uint64_t loudEvents;
    uint64_t noiseLowSamples;
    uint64_t noiseHighSamples;
    uint64_t totalSamples;
} gain_stats_t;

// AGC state needed between calls
typedef struct {
    int slowRise;
    int64_t nextRaiseAgc;
    float loudRebound;
} agc_state_t;

static void agc_init(agc_state_t *s) {
    s->slowRise = 0;
    s->nextRaiseAgc = 0;
    s->loudRebound = 0.0f;
}

static const double interval = 0.5;
static const double riseTime = 15;
static const double reboundTime = 1.5;

// Return values: 0 = no change, 1 = increase gain, 2 = lower gain (small), 3 = lower gain (large)
// Modified fields: *gainChange, *lowerGain
static int process_gain_stats(gain_stats_t *stats, int autoGain, int gainStartup, agc_state_t *s) {
    if (stats->totalSamples < interval * 2400000) {
        return 0;
    }

    if (!autoGain) {
        memset(s, 0, sizeof(*s));
        return 0;
    }

    double noiseLowPercent = stats->noiseLowSamples / (double) stats->totalSamples * 100.0;
    double noiseHighPercent = stats->noiseHighSamples / (double) stats->totalSamples * 100.0;

    int noiseLow = noiseLowPercent > 5.0;
    int noiseHigh = noiseHighPercent < 1.0;
    int loud = stats->loudEvents > 0;
    int veryLoud = stats->loudEvents > 5;

    if (loud || noiseHigh) {
        if (veryLoud && !gainStartup) {
            s->loudRebound += 2.0f;
            return 3; // lower gain (large)
        }
        s->loudRebound += 1.0f;
        return 2; // lower gain (small)
    } else if (noiseLow) {
        if (gainStartup || s->slowRise >= riseTime / interval || (s->loudRebound > 1 && s->slowRise >= reboundTime / interval)) {
            s->slowRise = 0;
            if (s->loudRebound > 0) {
                s->loudRebound *= 0.95f;
                s->loudRebound -= 1.0f;
            }
            return 1; // increase gain
        } else {
            s->slowRise++;
        }
    } else {
        // normal conditions, slowly drift
        s->loudRebound *= 0.97f;
        if (s->loudRebound < 0) s->loudRebound = 0;
    }
    return 0;
}

// --- Tests ---

static int test_auto_gain_lower_on_loud(void) {
    agc_state_t s;
    agc_init(&s);
    // noiseHighPercent = 25000/2400000*100 = 1.04% >= 1% -> noiseHigh = false
    gain_stats_t stats = {.loudEvents = 1, .totalSamples = 2400000, .noiseLowSamples = 1000, .noiseHighSamples = 25000};
    int result = process_gain_stats(&stats, 1, 0, &s);
    if (result != 2) {
        printf("FAIL: test_auto_gain_lower_on_loud: expected 2 got %d\n", result);
        return 0;
    }
    printf("PASS: test_auto_gain_lower_on_loud\n");
    return 1;
}

static int test_auto_gain_raise_on_noiseLow(void) {
    agc_state_t s;
    agc_init(&s);
    // gainStartup always raises on noiseLow
    // noiseLowPercent = 200000/2400000*100 = 8.3% > 5% -> noiseLow = true
    // noiseHighPercent = 25000/2400000*100 = 1.04% >= 1% -> noiseHigh = false
    gain_stats_t stats = {.loudEvents = 0, .totalSamples = 2400000, .noiseLowSamples = 200000, .noiseHighSamples = 25000};
    int result = process_gain_stats(&stats, 1, 1, &s);
    if (result != 1) {
        printf("FAIL: test_auto_gain_raise_on_noiseLow: expected 1 got %d\n", result);
        return 0;
    }
    printf("PASS: test_auto_gain_raise_on_noiseLow\n");
    return 1;
}

static int test_auto_gain_noop_normal(void) {
    agc_state_t s;
    agc_init(&s);
    // noiseLowPercent = 1000/2400000*100 = 0.04% <= 5% -> noiseLow = false
    // noiseHighPercent = 25000/2400000*100 = 1.04% >= 1% -> noiseHigh = false
    // loudEvents = 0 -> loud = false
    gain_stats_t stats = {.loudEvents = 0, .totalSamples = 2400000, .noiseLowSamples = 1000, .noiseHighSamples = 25000};
    int result = process_gain_stats(&stats, 1, 0, &s);
    if (result != 0) {
        printf("FAIL: test_auto_gain_noop_normal: expected 0 got %d\n", result);
        return 0;
    }
    printf("PASS: test_auto_gain_noop_normal\n");
    return 1;
}

static int test_auto_gain_not_enough_samples(void) {
    agc_state_t s;
    agc_init(&s);
    gain_stats_t stats = {.loudEvents = 5, .totalSamples = 100, .noiseLowSamples = 0, .noiseHighSamples = 100};
    int result = process_gain_stats(&stats, 1, 0, &s);
    if (result != 0) {
        printf("FAIL: test_auto_gain_not_enough_samples: expected 0 got %d\n", result);
        return 0;
    }
    printf("PASS: test_auto_gain_not_enough_samples\n");
    return 1;
}

static int test_auto_gain_very_loud(void) {
    agc_state_t s;
    agc_init(&s);
    gain_stats_t stats = {.loudEvents = 10, .totalSamples = 2400000, .noiseLowSamples = 1000, .noiseHighSamples = 1000};
    int result = process_gain_stats(&stats, 1, 0, &s);
    if (result != 3) {
        printf("FAIL: test_auto_gain_very_loud: expected 3 got %d\n", result);
        return 0;
    }
    printf("PASS: test_auto_gain_very_loud\n");
    return 1;
}

static int test_auto_gain_reset_on_no_auto_gain(void) {
    agc_state_t s;
    agc_init(&s);
    s.slowRise = 20;
    gain_stats_t stats = {.loudEvents = 1, .totalSamples = 2400000, .noiseLowSamples = 1000, .noiseHighSamples = 1000};
    int result = process_gain_stats(&stats, 0, 0, &s);
    if (result != 0 || s.slowRise != 0 || s.loudRebound != 0) {
        printf("FAIL: test_auto_gain_reset_on_no_auto_gain: result=%d slowRise=%d loudRebound=%f\n", result, s.slowRise, s.loudRebound);
        return 0;
    }
    printf("PASS: test_auto_gain_reset_on_no_auto_gain\n");
    return 1;
}

static int test_noise_high_lowers_gain(void) {
    agc_state_t s;
    agc_init(&s);
    gain_stats_t stats = {.loudEvents = 0, .totalSamples = 2400000, .noiseLowSamples = 1000, .noiseHighSamples = 0}; // noiseHighPercent < 1%
    int result = process_gain_stats(&stats, 1, 0, &s);
    if (result != 2) {
        printf("FAIL: test_noise_high_lowers_gain: expected 2 got %d\n", result);
        return 0;
    }
    printf("PASS: test_noise_high_lowers_gain\n");
    return 1;
}

int main(void) {
    int ok = 1;
    ok = test_auto_gain_lower_on_loud() && ok;
    ok = test_auto_gain_raise_on_noiseLow() && ok;
    ok = test_auto_gain_noop_normal() && ok;
    ok = test_auto_gain_not_enough_samples() && ok;
    ok = test_auto_gain_very_loud() && ok;
    ok = test_auto_gain_reset_on_no_auto_gain() && ok;
    ok = test_noise_high_lowers_gain() && ok;
    return ok ? 0 : 1;
}
