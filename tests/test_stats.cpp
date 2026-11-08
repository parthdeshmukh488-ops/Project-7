// The statistics the project's credibility rests on.
#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <numeric>
#include <random>
#include <vector>

#include "lob/measure/stats.hpp"

using namespace lob::measure;
using Catch::Approx;

namespace {
std::vector<double> sorted_uniform(std::size_t n) {
    std::vector<double> v(n);
    std::iota(v.begin(), v.end(), 1.0);  // 1..n, already sorted
    return v;
}
}  // namespace

TEST_CASE("binomial cdf matches hand-computable cases") {
    // n=1: P(X<=0) = 1-p
    REQUIRE(binomial_cdf(0, 1, 0.25) == Approx(0.75));
    // n=2, p=0.5: 0.25, 0.75, 1.0
    REQUIRE(binomial_cdf(0, 2, 0.5) == Approx(0.25));
    REQUIRE(binomial_cdf(1, 2, 0.5) == Approx(0.75));
    REQUIRE(binomial_cdf(2, 2, 0.5) == Approx(1.0));
    // n=10, p=0.5 is symmetric about 5
    REQUIRE(binomial_cdf(4, 10, 0.5) == Approx(1.0 - binomial_cdf(5, 10, 0.5)));
}

TEST_CASE("binomial cdf is monotone and bounded") {
    double previous = -1.0;
    for (std::size_t k = 0; k <= 50; ++k) {
        const double c = binomial_cdf(k, 50, 0.3);
        REQUIRE(c >= previous);
        REQUIRE(c >= 0.0);
        REQUIRE(c <= 1.0);
        previous = c;
    }
    REQUIRE(binomial_cdf(50, 50, 0.3) == Approx(1.0));
}

TEST_CASE("the normal approximation converges to the exact binomial") {
    // Kept as two functions precisely so this can be checked.  Testing a
    // fixed tolerance would be testing the wrong thing: the approximation's
    // error at the central term falls as 1/sqrt(n), so what matters is that
    // it *converges*, not that it is small at any one n.
    double previous = 1.0;
    for (std::size_t n : {1000u, 10000u, 100000u}) {
        const double p = 0.5;
        const auto k = static_cast<std::size_t>(n * p);
        const double error = std::abs(binomial_cdf(k, n, p) - binomial_cdf_normal(k, n, p));
        REQUIRE(error < previous);          // strictly improving
        REQUIRE(error < 2.0 / std::sqrt(static_cast<double>(n)));
        previous = error;
    }
}

TEST_CASE("quantile point estimate is the nearest rank") {
    const auto v = sorted_uniform(100);  // values 1..100
    REQUIRE(quantile(v, 0.5).value == Approx(50.0));
    REQUIRE(quantile(v, 0.99).value == Approx(99.0));
    // Never interpolates: every reported value was actually observed.
    for (double p : {0.1, 0.33, 0.5, 0.75, 0.99}) {
        const double value = quantile(v, p).value;
        REQUIRE(std::find(v.begin(), v.end(), value) != v.end());
    }
}

TEST_CASE("the confidence interval brackets the true quantile") {
    // Draw repeatedly from a known distribution and count how often the
    // interval contains the truth.  A 95% interval should contain it about
    // 95% of the time -- the only honest test of a confidence interval.
    std::mt19937_64 rng(12345);
    std::exponential_distribution<double> dist(1.0);  // heavy-ish tail

    const double p = 0.99;
    const double truth = -std::log1p(-p);  // exact quantile of Exp(1)
    const std::size_t n = 2000, trials = 400;

    std::size_t covered = 0;
    for (std::size_t t = 0; t < trials; ++t) {
        std::vector<double> sample(n);
        for (auto& x : sample) x = dist(rng);
        std::sort(sample.begin(), sample.end());
        const auto q = quantile(sample, p, 0.95);
        if (q.lower <= truth && truth <= q.upper) ++covered;
    }
    const double coverage = static_cast<double>(covered) / trials;
    REQUIRE(coverage > 0.90);
    REQUIRE(coverage < 1.00);
}

TEST_CASE("intervals widen as the percentile approaches one") {
    // The property that makes p99.99 from a short run meaningless.
    std::mt19937_64 rng(7);
    std::exponential_distribution<double> dist(1.0);
    std::vector<double> sample(10'000);
    for (auto& x : sample) x = dist(rng);
    std::sort(sample.begin(), sample.end());

    const double w50 = quantile(sample, 0.5).relative_width();
    const double w99 = quantile(sample, 0.99).relative_width();
    const double w999 = quantile(sample, 0.999).relative_width();
    REQUIRE(w50 < w99);
    REQUIRE(w99 < w999);
}

TEST_CASE("a percentile beyond the sample is flagged rather than reported") {
    // 1000 samples carry essentially no information about p99.99: there are
    // not enough observations past it to bound it at all.
    std::mt19937_64 rng(3);
    std::exponential_distribution<double> dist(1.0);
    std::vector<double> sample(1000);
    for (auto& x : sample) x = dist(rng);
    std::sort(sample.begin(), sample.end());

    const auto q = quantile(sample, 0.9999);
    REQUIRE(q.saturated());
    REQUIRE(q.effective_samples <= 1);
}

TEST_CASE("samples_needed grows with the percentile and with tightness") {
    REQUIRE(samples_needed(0.999, 0.10) > samples_needed(0.99, 0.10));
    REQUIRE(samples_needed(0.9999, 0.10) > samples_needed(0.999, 0.10));
    REQUIRE(samples_needed(0.99, 0.01) > samples_needed(0.99, 0.10));

    // Each extra nine costs about ten times the samples.
    const double ratio = static_cast<double>(samples_needed(0.9999, 0.10)) /
                         static_cast<double>(samples_needed(0.999, 0.10));
    REQUIRE(ratio > 8.0);
    REQUIRE(ratio < 12.0);

    // Quoting p99.99 to one percent needs over a billion samples, which is
    // the practical reason almost no published tail figure is sound.
    REQUIRE(samples_needed(0.9999, 0.01) > 1'000'000'000);

    // And a requirement that cannot be met says so rather than lying.
    REQUIRE(samples_needed(0.999999, 0.001, 0.95, 1'000'000) == 0);
}

TEST_CASE("summarise sorts its input and returns one entry per probability") {
    std::mt19937_64 rng(1);
    std::vector<double> sample(5000);
    std::uniform_real_distribution<double> dist(0.0, 1.0);
    for (auto& x : sample) x = dist(rng);

    const auto out = summarise(sample, kDefaultProbabilities);
    REQUIRE(out.size() == std::size(kDefaultProbabilities));
    for (std::size_t i = 1; i < out.size(); ++i)
        REQUIRE(out[i].value >= out[i - 1].value);
}

TEST_CASE("degenerate input is rejected rather than guessed at") {
    std::vector<double> empty;
    REQUIRE_THROWS(quantile(empty, 0.5));
    const auto v = sorted_uniform(10);
    REQUIRE_THROWS(quantile(v, 0.0));
    REQUIRE_THROWS(quantile(v, 1.0));
}
