// Percentiles with confidence intervals.
//
// A latency distribution has no useful mean.  It is heavy-tailed and usually
// multi-modal -- cache hit, cache miss, page fault, preemption -- so an average
// describes none of those modes and is dominated by whichever tail happened to
// show up.  Percentiles are the right summary, and they come with a catch that
// almost every benchmark ignores: a percentile estimated from a finite sample
// is itself an estimate, and its uncertainty grows sharply as the percentile
// approaches one.
//
// A p99.99 from 100,000 samples rests on about ten observations.  Quoting it
// to three significant figures is not meaningful, and that is exactly what
// published matching-engine benchmarks tend to do.  Everything here exists so
// this project does not.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace lob::measure {

/// A percentile estimate with a distribution-free confidence interval.
struct Quantile {
    double probability;  ///< e.g. 0.999 for p99.9
    double value;        ///< the point estimate
    double lower;        ///< lower confidence bound
    double upper;        ///< upper confidence bound
    std::size_t rank;    ///< 1-based rank of the point estimate in the sample
    std::size_t effective_samples;  ///< samples at or beyond this percentile

    /// Half-width of the interval relative to the estimate.
    ///
    /// The number to look at before quoting a percentile.  Above a few percent
    /// the figure should be reported with its interval or not at all.
    [[nodiscard]] double relative_width() const;

    /// Whether the interval is one-sided because the sample ran out.
    ///
    /// True when the upper bound would need a rank beyond the largest
    /// observation -- i.e. the sample contains no evidence about where this
    /// percentile actually sits.  Reporting such a number as if it were
    /// measured is the single most common error in latency benchmarking.
    [[nodiscard]] bool saturated() const;
};

/// Exact binomial CDF, P(X <= k) for X ~ Binomial(n, p), by summation.
///
/// Used for the confidence bounds at moderate sample sizes, where the normal
/// approximation is not trustworthy in the far tail -- which is precisely
/// where latency percentiles live.
[[nodiscard]] double binomial_cdf(std::size_t k, std::size_t n, double p);

/// Normal approximation to the binomial CDF, with continuity correction.
///
/// Used above `kExactBinomialLimit` where exact summation costs too much.  The
/// test suite checks the two agree across the overlap; they are kept as
/// separate functions so that check is possible rather than assumed.
[[nodiscard]] double binomial_cdf_normal(std::size_t k, std::size_t n, double p);

/// Sample size above which the normal approximation is used.
inline constexpr std::size_t kExactBinomialLimit = 100'000;

/// Percentile with a distribution-free confidence interval.
///
/// The interval comes from order statistics rather than from any assumption
/// about the shape of the distribution: the count of samples falling below the
/// true p-quantile is Binomial(n, p), so a confidence interval on that count
/// maps directly onto a pair of ranks in the sorted sample.  Nothing here
/// assumes normality, which matters because latency is about as far from
/// normal as a measured quantity gets.
///
/// `sorted` must be sorted ascending.
[[nodiscard]] Quantile quantile(std::span<const double> sorted, double probability,
                                double confidence = 0.95);

/// Convenience: sort a copy and compute several percentiles.
[[nodiscard]] std::vector<Quantile> summarise(std::vector<double> samples,
                                              std::span<const double> probabilities,
                                              double confidence = 0.95);

/// The percentiles this project reports by default.
inline constexpr double kDefaultProbabilities[] = {0.5, 0.9, 0.99, 0.999, 0.9999};

/// Smallest sample size at which `probability` can be estimated to within
/// `target_width`, as a fraction of the number of samples beyond it.
///
/// Answers "how many samples before p99.99 is worth quoting?" -- the question
/// that decides whether a benchmark run was long enough.  The requirement
/// grows as 1/(1-p), so each extra nine costs ten times the samples.
///
/// Returns 0 when the requirement exceeds `max_samples`, which is a useful
/// answer in itself: it means the percentile should not be quoted at all.
[[nodiscard]] std::size_t samples_needed(double probability, double target_width,
                                         double confidence = 0.95,
                                         std::size_t max_samples = 1'000'000'000'000ull);

}  // namespace lob::measure
