"""Type stub for the _numpy_sr C extension (numpy_sr/_numpy_sr.cc), which
only exists under build-install/; a source tree imports it as None."""

from typing import TypedDict

class _TargetInfo(TypedDict):
    name: str
    has_fma: bool
    has_float64: bool

# {HWY_* target bit: info}; 0 is the MPFR oracle. Mirrors Targets() in the .cc.
def targets() -> dict[int, _TargetInfo]: ...
def fenv_clear() -> None: ...
def fenv_test() -> int: ...

FE_INVALID: int
FE_DIVBYZERO: int
FE_OVERFLOW: int
FE_UNDERFLOW: int
FE_INEXACT: int
FE_ERRORS: int
