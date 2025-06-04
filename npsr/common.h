#ifndef NUMPY_SIMD_ROUTINES_NPSR_COMMON_H_
#define NUMPY_SIMD_ROUTINES_NPSR_COMMON_H_

#include "hwy/highway.h"

#include <cfenv>
#include <type_traits>

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
#if 0 // not used yet
  static constexpr auto kDown = _Down{};
  static constexpr auto kUp = _Up{};
  static constexpr auto kZero = _Zero{};
#endif
};

struct Subnormal {
  struct _DAZ {};
  struct _FTZ {};
  struct _IEEE754 {};
#if 0 // not used yet
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

template <typename... Args> class Precise {
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
  static constexpr bool kException = !kNoExceptions;

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

} // namespace npsr

#endif // NUMPY_SIMD_ROUTINES_NPSR_COMMON_H_
