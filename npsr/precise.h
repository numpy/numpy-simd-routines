#ifndef NPSR_PRECISE_H_
#define NPSR_PRECISE_H_

#include <array>
#include <cfenv>
#include <string>
#include <type_traits>

namespace npsr {
using std::is_same_v;

// Tag types for configuring floating-point behavior
// These allow compile-time configuration without runtime overhead

// Algorithm configuration tags
// Skip extended precision for |x| > 2^24 (float) or 2^53 (double)
struct _NoLargeArgument {};
// Skip checks for NaN, Inf, and other special values
struct _NoSpecialCases {};
struct _NoExceptions {};  // Disable floating-point exception tracking
// Use faster, less accurate algorithms (typ. 1-4 ULP vs 1.0 ULP)
struct _LowAccuracy {};

// Convenience constants for cleaner API
constexpr auto kNoLargeArgument = _NoLargeArgument{};
constexpr auto kNoSpecialCases = _NoSpecialCases{};
constexpr auto kNoExceptions = _NoExceptions{};
constexpr auto kLowAccuracy = _LowAccuracy{};

// Subnormal (denormal) number handling modes
// Controls how the CPU handles numbers smaller than the minimum normalized
// value
struct Subnormal {
  struct _DAZ {};  // Denormals Are Zero: treat subnormals as zero on input
  struct _FTZ {};  // Flush To Zero: round subnormal results to zero
  struct _IEEE754 {
  };  // Strict IEEE 754 compliance: handle subnormals correctly

  static constexpr auto kDAZ = _DAZ{};
  static constexpr auto kFTZ = _FTZ{};
  static constexpr auto kIEEE754 = _IEEE754{};
};

// Floating-point exception flags
// These match the standard C library FE_* macros
class FPExceptions {
 public:
  static constexpr auto kNone = 0;
// guard against missing macros on some platforms
// (e.g. Emscripten)
#ifdef FE_INVALID
  static constexpr auto kInvalid = FE_INVALID;
#else
  static constexpr auto kInvalid = 0;
#endif
#ifdef FE_DIVBYZERO
  static constexpr auto kDivByZero = FE_DIVBYZERO;
#else
  static constexpr auto kDivByZero = 0;
#endif
#ifdef FE_OVERFLOW
  static constexpr auto kOverflow = FE_OVERFLOW;
#else
  static constexpr auto kOverflow = 0;
#endif
#ifdef FE_UNDERFLOW
  static constexpr auto kUnderflow = FE_UNDERFLOW;
#else
  static constexpr auto kUnderflow = 0;
#endif
  static constexpr auto kAll = kInvalid | kDivByZero | kOverflow | kUnderflow;

  void Raise(int errors) noexcept { mask_ |= errors; }

 protected:
  void Load() noexcept { loaded_ = std::fegetexceptflag(&saved_, kAll) == 0; }

  ~FPExceptions() noexcept {
    if (loaded_) {
      std::fesetexceptflag(&saved_, kAll);
    }
    if (mask_ != kNone) {
      std::feraiseexcept(mask_);
    }
  }

 private:
  bool loaded_ = false;
  int mask_ = kNone;
  std::fexcept_t saved_;
};

/**
 * @brief RAII floating-point precision control class
 *
 * The Precise class provides automatic management of floating-point
 * environment settings during its lifetime. It uses RAII principles to save
 * the current floating-point state on construction and restore it on
 * destruction.
 *
 * The class is configured using variadic template arguments that specify
 * the desired floating-point behavior through tag types.
 *
 * **IMPORTANT PERFORMANCE NOTE**: Create the Precise object BEFORE loops,
 * not inside them. The constructor and destructor have overhead from saving
 * and restoring floating-point state, so it should be done once per
 * computational scope, not per iteration.
 *
 * @tparam Args Variadic template arguments for configuration flags
 *
 * Configuration options:
 * - kLowAccuracy: Use faster algorithms with ~1-4 ULP error (default: high
 * accuracy ~1.0 ULP)
 * - kNoLargeArgument: Skip extended precision reduction for large arguments
 * - kNoSpecialCases: Skip NaN/Inf handling (assumes finite inputs)
 * - kNoExceptions: Disable FP exception tracking for better performance
 * - Subnormal::kDAZ/kFTZ: Flush subnormals to zero for performance
 * - Subnormal::kIEEE754: Strict IEEE 754 compliance (default if DAZ/FTZ not
 * specified)
 *
 * @example
 * ```cpp
 * using namespace hwy::HWY_NAMESPACE;
 * using namespace npsr;
 * using namespace npsr::HWY_NAMESPACE;
 *
 * // Configure for maximum performance with reduced accuracy
 * Precise precise = {kLowAccuracy, kNoSpecialCases, kNoLargeArgument};
 *
 * const ScalableTag<float> d;
 * using V = Vec<decltype(d)>;
 *
 * for (size_t i = 0; i < n; i += Lanes(d)) {
 *     V input = LoadU(d, &input_data[i]);
 *     V result = Sin(precise, input);  // Uses configured precision
 *     StoreU(result, d, &output_data[i]);
 * }
 * ```
 */
template <typename... Args>
class Precise : public FPExceptions {
 public:
  // Default constructor saves current FP state
  Precise() noexcept {
    // Save exception flags unless disabled
    if constexpr (!kNoExceptions) {
      FPExceptions::Load();
    }
  }

  // Variadic constructor for tag-based configuration
  template <typename T1, typename... Rest>
  Precise(T1&& arg1, Rest&&... rest) noexcept : Precise() {
    // Tags are processed at compile time via template parameters
    // This constructor exists to enable Precise{tag1, tag2, ...} syntax
  }

  // Compile-time configuration queries
  // These allow algorithms to optimize based on precision requirements
  static constexpr bool kNoExceptions = (is_same_v<_NoExceptions, Args> || ...);
  static constexpr bool kNoLargeArgument =
      (is_same_v<_NoLargeArgument, Args> || ...);
  static constexpr bool kNoSpecialCases =
      (is_same_v<_NoSpecialCases, Args> || ...);
  static constexpr bool kLowAccuracy = (is_same_v<_LowAccuracy, Args> || ...);

  // Derived flags (defaults when not explicitly specified)
  static constexpr bool kHighAccuracy = !kLowAccuracy;
  static constexpr bool kLargeArgument = !kNoLargeArgument;
  static constexpr bool kSpecialCases = !kNoSpecialCases;
  static constexpr bool kExceptions = !kNoExceptions;

  // Subnormal handling configuration
  static constexpr bool kDAZ = (is_same_v<Subnormal::_DAZ, Args> || ...);
  static constexpr bool kFTZ = (is_same_v<Subnormal::_FTZ, Args> || ...);
  static constexpr bool _kIEEE754 =
      (is_same_v<Subnormal::_IEEE754, Args> || ...);

  // Ensure IEEE754 mode is exclusive with DAZ/FTZ
  static_assert(!_kIEEE754 || !(kDAZ || kFTZ),
                "IEEE754 mode cannot be used "
                "with Denormals Are Zero (DAZ) or Flush To Zero (FTZ) "
                "subnormal handling");

  // Default to IEEE754 if no subnormal mode specified
  static constexpr bool kIEEE754 = _kIEEE754 || !(kDAZ || kFTZ);
};  // namespace npsr

// Deduction guides for convenient construction

// Enable Precise{} with no arguments
Precise() -> Precise<>;
// Enable Precise{tag1, tag2, ...} syntax
template <typename T1, typename... Rest>
Precise(T1&&, Rest&&...) -> Precise<std::decay_t<T1>, std::decay_t<Rest>...>;

}  // namespace npsr
#endif  // NPSR_PRECISE_H_
