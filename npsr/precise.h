#ifndef NUMPY_SIMD_ROUTINES_NPSR_PRECISE_H_
#define NUMPY_SIMD_ROUTINES_NPSR_PRECISE_H_
#include <cfenv>
#include <type_traits>

#include "hwy/highway.h"

namespace npsr {

struct _NoLargeArgument {};
struct _NoSpecialCases {};
struct _NoExceptions {};
struct _LowAccuracy {};
constexpr auto kNoLargeArgument = _NoLargeArgument{};
constexpr auto kNoSpecialCases = _NoSpecialCases{};
constexpr auto kNoExceptions = _NoExceptions{};
constexpr auto kLowAccuracy = _LowAccuracy{};

struct Round {
  struct _Force {};
  struct _Nearest {};
  struct _Down {};
  struct _Up {};
  struct _Zero {};
  static constexpr auto kForce = _Force{};
  static constexpr auto kNearest = _Nearest{};
#if 0  // not used yet
  static constexpr auto kDown = _Down{};
  static constexpr auto kUp = _Up{};
  static constexpr auto kZero = _Zero{};
#endif
};

struct Subnormal {
  struct _DAZ {};
  struct _FTZ {};
  struct _IEEE754 {};
#if 0  // not used yet
  static constexpr auto kDAZ = _DAZ{};
  static constexpr auto kFTZ = _FTZ{};
#endif
  static constexpr auto kIEEE754 = _IEEE754{};
};

struct FPExceptions {
  static constexpr auto kNone = 0;
  static constexpr auto kInvalid = FE_INVALID;
  static constexpr auto kDivByZero = FE_DIVBYZERO;
  static constexpr auto kOverflow = FE_OVERFLOW;
  static constexpr auto kUnderflow = FE_UNDERFLOW;
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
 * @example
 * ```cpp
 * using namespace hwy::HWY_NAMESPACE;
 * using namespace npsr;
 * using namespace npsr::HWY_NAMESPACE;
 *
 * Precise precise = {kLowAccuracy, kNoSpecialCases, kNoLargeArgument};
 * const ScalableTag<float> d;
 * typename V = Vec<DFromV<SclableTag>>;
 * for (size_t i = 0; i < n; i += Lanes(d)) {
 *     V input = LoadU(d, &input[i]);
 *     V result = Sin(precise, input);
 *     StoreU(result, d, &output[i]);
 * }
 * ```
 */
template <typename... Args>
class Precise {
 public:
  Precise() {
    if constexpr (!kNoExceptions) {
      fegetexceptflag(&_exceptions, FE_ALL_EXCEPT);
    }
    if constexpr (kRoundForce) {
      _rounding_mode = fegetround();
      int new_mode = _NewRoundingMode();
      if (_rounding_mode != new_mode) {
        _retrieve_rounding_mode = true;
        fesetround(new_mode);
      }
    }
  }
  template <typename T1, typename... Rest>
  Precise(T1&& arg1, Rest&&... rest) {}

  void FlushExceptions() { fesetexceptflag(&_exceptions, FE_ALL_EXCEPT); }

  void Raise(int errors) {
    static_assert(!kNoExceptions,
                  "Cannot raise exceptions in NoExceptions mode");
    _exceptions |= errors;
  }
  ~Precise() {
    FlushExceptions();
    if constexpr (kRoundForce) {
      if (_retrieve_rounding_mode) {
        fesetround(_rounding_mode);
      }
    }
  }
  static constexpr bool kNoExceptions =
      (std::is_same_v<_NoExceptions, Args> || ...);
  static constexpr bool kNoLargeArgument =
      (std::is_same_v<_NoLargeArgument, Args> || ...);
  static constexpr bool kNoSpecialCases =
      (std::is_same_v<_NoSpecialCases, Args> || ...);
  static constexpr bool kLowAccuracy =
      (std::is_same_v<_LowAccuracy, Args> || ...);
  // defaults to high accuracy if no low accuracy flag is set
  static constexpr bool kHighAccuracy = !kLowAccuracy;
  // defaults to large argument support if no no large argument flag is set
  static constexpr bool kLargeArgument = !kNoLargeArgument;
  // defaults to special cases support if no no special cases flag is set
  static constexpr bool kSpecialCases = !kNoSpecialCases;
  // defaults to exception support if no no exception flag is set
  static constexpr bool kExceptions = !kNoExceptions;

  static constexpr bool kRoundForce =
      (std::is_same_v<Round::_Force, Args> || ...);
  static constexpr bool _kRoundNearest =
      (std::is_same_v<Round::_Nearest, Args> || ...);
  static constexpr bool kRoundZero =
      (std::is_same_v<Round::_Zero, Args> || ...);
  static constexpr bool kRoundDown =
      (std::is_same_v<Round::_Down, Args> || ...);
  static constexpr bool kRoundUp = (std::is_same_v<Round::_Up, Args> || ...);
  // only one rounding mode can be set
  static_assert((_kRoundNearest + kRoundDown + kRoundUp + kRoundZero) <= 1,
                "Only one rounding mode can be set at a time");
  // if no rounding mode is set, default to round nearest
  static constexpr bool kRoundNearest =
      _kRoundNearest || (!kRoundDown && !kRoundUp && !kRoundZero);

  static constexpr bool kDAZ = (std::is_same_v<Subnormal::_DAZ, Args> || ...);
  static constexpr bool kFTZ = (std::is_same_v<Subnormal::_FTZ, Args> || ...);
  static constexpr bool _kIEEE754 =
      (std::is_same_v<Subnormal::_IEEE754, Args> || ...);
  static_assert(!_kIEEE754 || !(kDAZ || kFTZ),
                "IEEE754 mode cannot be used "
                "with Denormals Are Zero (DAZ) or Flush To Zero (FTZ) "
                "subnormal handling");
  static constexpr bool kIEEE754 = _kIEEE754 || !(kDAZ || kFTZ);

 private:
  int _NewRoundingMode() const {
    if constexpr (kRoundDown) {
      return FE_DOWNWARD;
    } else if constexpr (kRoundUp) {
      return FE_UPWARD;
    } else if constexpr (kRoundZero) {
      return FE_TOWARDZERO;
    } else {
      return FE_TONEAREST;
    }
  }
  int _rounding_mode = 0;
  bool _retrieve_rounding_mode = false;
  fexcept_t _exceptions;
};

Precise() -> Precise<>;

// For Precise{args...} -> Precise<decltype(args)...>
template <typename T1, typename... Rest>
Precise(T1&&, Rest&&...) -> Precise<std::decay_t<T1>, std::decay_t<Rest>...>;

}  // namespace npsr
#endif  // NUMPY_SIMD_ROUTINES_NPSR_PRECISE_H_
