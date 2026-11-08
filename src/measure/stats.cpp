#include "lob/measure/stats.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace lob::measure {
namespace {

/// log(n!) via lgamma -- the factorials in a binomial coefficient overflow a
/// double well before n reaches the sample sizes this project uses.
double log_factorial(std::size_t n) {
    return std::lgamma(static_cast<double>(n) + 1.0);
}

double log_binomial_pmf(std::size_t k, std::size_t n, double p) {
    if (p <= 0.0) return (k == 0) ? 0.0 : -std::numeric_limits<double>::infinity();
    if (p >= 1.0) return (k == n) ? 0.0 : -std::numeric_limits<double>::infinity();
    const double log_coeff =
        log_factorial(n) - log_factorial(k) - log_factorial(n - k);
    return log_coeff + static_cast<double>(k) * std::log(p) +
           static_cast<double>(n - k) * std::log1p(-p);
}

/// Standard normal CDF.  erfc rather than erf so the left tail keeps its
/// significant digits -- the same reason the volatility work uses erfcx.
double normal_cdf(double z) { return 0.5 * std::erfc(-z * M_SQRT1_2); }

/// Inverse standard normal, Acklam's rational approximation refined by one
/// Halley step.  Accurate to ~1e-15, which is more than the binomial
/// approximation it feeds ever deserves.
double normal_quantile(double p) {
    if (p <= 0.0 || p >= 1.0) throw std::domain_error("normal_quantile needs p in (0,1)");

    static constexpr double a[] = {-3.969683028665376e+01, 2.209460984245205e+02,
                                   -2.759285104469687e+02, 1.383577518672690e+02,
                                   -3.066479806614716e+01, 2.506628277459239e+00};
    static constexpr double b[] = {-5.447609879822406e+01, 1.615858368580409e+02,
                                   -1.556989798598866e+02, 6.680131188771972e+01,
                                   -1.328068155288572e+01};
    static constexpr double c[] = {-7.784894002430293e-03, -3.223964580411365e-01,
                                   -2.400758277161838e+00, -2.549732539343734e+00,
                                   4.374664141464968e+00,  2.938163982698783e+00};
    static constexpr double d[] = {7.784695709041462e-03, 3.224671290700398e-01,
                                   2.445134137142996e+00, 3.754408661907416e+00};
    static constexpr double lo = 0.02425, hi = 1.0 - 0.02425;

    double x;
    if (p < lo) {
        const double q = std::sqrt(-2.0 * std::log(p));
        x = (((((c[0] * q + c[1]) * q + c[2]) * q + c[3]) * q + c[4]) * q + c[5]) /
            ((((d[0] * q + d[1]) * q + d[2]) * q + d[3]) * q + 1.0);
    } else if (p > hi) {
        const double q = std::sqrt(-2.0 * std::log1p(-p));
        x = -(((((c[0] * q + c[1]) * q + c[2]) * q + c[3]) * q + c[4]) * q + c[5]) /
            ((((d[0] * q + d[1]) * q + d[2]) * q + d[3]) * q + 1.0);
    } else {
        const double q = p - 0.5, r = q * q;
        x = (((((a[0] * r + a[1]) * r + a[2]) * r + a[3]) * r + a[4]) * r + a[5]) * q /
            (((((b[0] * r + b[1]) * r + b[2]) * r + b[3]) * r + b[4]) * r + 1.0);
    }

    // One Halley refinement; cheap, and takes the approximation to full
    // double precision.
    const double err = normal_cdf(x) - p;
    const double density = std::exp(-0.5 * x * x) / std::sqrt(2.0 * M_PI);
    if (density > 0.0) {
        const double u = err / density;
        x -= u / (1.0 + 0.5 * x * u);
    }
    return x;
}

}  // namespace

double Quantile::relative_width() const {
    if (value == 0.0) return std::numeric_limits<double>::infinity();
    return (upper - lower) / (2.0 * std::abs(value));
}

bool Quantile::saturated() const { return effective_samples < 2; }

double binomial_cdf(std::size_t k, std::size_t n, double p) {
    if (k >= n) return 1.0;
    double total = 0.0;
    for (std::size_t i = 0; i <= k; ++i) {
        total += std::exp(log_binomial_pmf(i, n, p));
    }
    return std::min(total, 1.0);
}

double binomial_cdf_normal(std::size_t k, std::size_t n, double p) {
    const double mean = static_cast<double>(n) * p;
    const double sd = std::sqrt(static_cast<double>(n) * p * (1.0 - p));
    if (sd <= 0.0) return (static_cast<double>(k) >= mean) ? 1.0 : 0.0;
    // Continuity correction: the binomial is discrete, the normal is not.
    return normal_cdf((static_cast<double>(k) + 0.5 - mean) / sd);
}

Quantile quantile(std::span<const double> sorted, double probability, double confidence) {
    const std::size_t n = sorted.size();
    if (n == 0) throw std::invalid_argument("quantile of an empty sample");
    if (probability <= 0.0 || probability >= 1.0)
        throw std::domain_error("probability must lie in (0, 1)");

    // Point estimate: nearest-rank, which needs no interpolation between
    // observations and so never reports a latency that was not observed.
    const auto rank = static_cast<std::size_t>(
        std::ceil(probability * static_cast<double>(n)));
    const std::size_t index = std::min(rank == 0 ? 0 : rank - 1, n - 1);

    // Confidence bounds from order statistics.  The count of samples below
    // the true quantile is Binomial(n, probability); bounding that count at
    // the requested confidence bounds the rank, and the sorted sample turns a
    // rank into a value.  No assumption is made about the distribution's
    // shape, which is the point -- latency is nowhere near normal.
    const double alpha = 1.0 - confidence;
    const double z = normal_quantile(1.0 - alpha / 2.0);
    const double mean = static_cast<double>(n) * probability;
    const double sd = std::sqrt(static_cast<double>(n) * probability * (1.0 - probability));

    double lower_rank = mean - z * sd;
    double upper_rank = mean + z * sd;

    if (n <= kExactBinomialLimit) {
        // Exact: walk out from the normal estimate until the tail mass is
        // under alpha/2 on each side.  The approximation is poor in the far
        // tail at small n, which is exactly where percentiles are quoted.
        auto lo = static_cast<std::size_t>(std::max(0.0, std::floor(lower_rank)));
        while (lo > 0 && binomial_cdf(lo, n, probability) > alpha / 2.0) --lo;
        while (lo + 1 < n && binomial_cdf(lo + 1, n, probability) <= alpha / 2.0) ++lo;

        auto hi = static_cast<std::size_t>(
            std::min(static_cast<double>(n), std::ceil(upper_rank)));
        while (hi < n && binomial_cdf(hi, n, probability) < 1.0 - alpha / 2.0) ++hi;
        while (hi > 0 && binomial_cdf(hi - 1, n, probability) >= 1.0 - alpha / 2.0) --hi;

        lower_rank = static_cast<double>(lo);
        upper_rank = static_cast<double>(hi);
    }

    const auto lower_index = static_cast<std::size_t>(
        std::clamp(lower_rank, 0.0, static_cast<double>(n - 1)));
    const auto upper_index = static_cast<std::size_t>(
        std::clamp(upper_rank, 0.0, static_cast<double>(n - 1)));

    return Quantile{
        .probability = probability,
        .value = sorted[index],
        .lower = sorted[lower_index],
        .upper = sorted[upper_index],
        .rank = index + 1,
        .effective_samples = n - index,
    };
}

std::vector<Quantile> summarise(std::vector<double> samples,
                                std::span<const double> probabilities,
                                double confidence) {
    std::sort(samples.begin(), samples.end());
    std::vector<Quantile> out;
    out.reserve(probabilities.size());
    for (double p : probabilities) out.push_back(quantile(samples, p, confidence));
    return out;
}

std::size_t samples_needed(double probability, double target_width, double confidence,
                           std::size_t max_samples) {
    // What limits a tail percentile is how many observations lie beyond it:
    // n(1-p).  The rank uncertainty is z sqrt(n p (1-p)), so measured against
    // that tail count the relative half-width is
    //
    //     w = 2 z sqrt(n p (1-p)) / (n (1-p)) = 2 z sqrt(p) / sqrt(n (1-p))
    //
    // and solving for n gives the expression below.  It grows as 1/(1-p),
    // which is why p99.99 needs ten thousand times the samples of p99 -- the
    // practical reason so few published tail figures are sound.
    //
    // Rank space only: the achievable width in *value* space also depends on
    // how dense the distribution is near that percentile, so this is a
    // necessary condition rather than a sufficient one.
    const double alpha = 1.0 - confidence;
    const double z = normal_quantile(1.0 - alpha / 2.0);
    const double needed = 4.0 * z * z * probability /
                          (target_width * target_width * (1.0 - probability));
    const auto n = static_cast<std::size_t>(std::ceil(needed));
    return (n > max_samples) ? 0 : std::max<std::size_t>(n, 1);
}

}  // namespace lob::measure
