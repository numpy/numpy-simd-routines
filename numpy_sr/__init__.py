"""Front-end for the per-operation SIMD test libraries.

Each op library is a ``.so`` exporting ``npsr_py_load`` (numpy_sr/pyext.h);
kernels are called through ctypes over contiguous numpy buffers:

    import numpy as np, numpy_sr as sr
    y = sr.sin(x)                    # best target, accuracy=Precise()
    y = sr.targets["AVX2"].sin(x)    # a specific target
    ref, residual = sr.mpfr.sin(x)   # correctly rounded MPFR oracle
"""

from __future__ import annotations

import ctypes
import pathlib
import warnings
from collections.abc import Callable
from typing import Any

import numpy as np

__version__ = "1.0.0"

# Target detection + FP env; absent in an uninstalled source tree -- load()
# then yields no targets and fenv refuses to enter.
try:
    from . import _numpy_sr as _core
except ImportError:
    _core = None

# Populated by load(); best (lowest target_id) target first.
targets: dict[str, Target] = {}
best: Target | None = None
mpfr: _MpfrReference | None = None

# A dlopened kernel: a ctypes function pointer over raw buffer addresses.
_Kernel = Callable[..., Any]


# --- precision profiles (mirror npsr::Precise / PrecBit in pyext.h) ---------

# Precise kwarg -> (PrecBit, label token).
_PREC = {
    "kLowAccuracy": (1 << 0, "low"),
    "kNoLargeArgument": (1 << 1, "nolarge"),
    "kNoSpecialCases": (1 << 2, "nospecial"),
    "kNoExceptions": (1 << 3, "noexc"),
    "kDAZ": (1 << 4, "daz"),
    "kFTZ": (1 << 5, "ftz"),
}
_LOW_BIT = _PREC["kLowAccuracy"][0]


def prec_label(mask: int) -> str:
    """Profile label for a prec_mask: 'high', 'low', 'low+noexc', ..."""
    tokens = [token for bit, token in _PREC.values() if mask & bit]
    if not mask & _LOW_BIT:
        tokens.insert(0, "high")
    return "+".join(tokens)


class Precise:
    """Precision profile, an op's ``accuracy=`` keyword; flags match
    npsr::Precise. The default ``Precise()`` is the high-accuracy profile."""

    def __init__(self, **flags: bool):
        unknown = set(flags) - set(_PREC)
        if unknown:
            raise TypeError(
                f"unknown Precise flags {sorted(unknown)}; "
                f"expected one of {sorted(_PREC)}"
            )
        self.mask = sum(_PREC[name][0] for name, on in flags.items() if on)

    @property
    def label(self) -> str:
        return prec_label(self.mask)

    def __hash__(self) -> int:
        return hash(self.mask)

    def __eq__(self, other: object) -> bool:
        return isinstance(other, Precise) and other.mask == self.mask

    def __repr__(self) -> str:
        on = [name for name, (bit, _) in _PREC.items() if self.mask & bit]
        return f"Precise({', '.join(on)})"


_HIGH = Precise()


# --- FP exception flags ------------------------------------------------------


class fenv:
    """Clear the thread's FP exception flags on entry, capture them on exit
    (ctypes kernels run on the calling thread, so their flags are seen):

        with fenv() as f:
            targets["AVX2"].sin(buf)
        assert f.errors == f.FE_INVALID

    ``raised`` holds every FE_* bit; ``errors`` masks out FE_INEXACT.
    """

    FE_INVALID = getattr(_core, "FE_INVALID", 0)
    FE_DIVBYZERO = getattr(_core, "FE_DIVBYZERO", 0)
    FE_OVERFLOW = getattr(_core, "FE_OVERFLOW", 0)
    FE_UNDERFLOW = getattr(_core, "FE_UNDERFLOW", 0)
    FE_INEXACT = getattr(_core, "FE_INEXACT", 0)
    FE_ERRORS = getattr(_core, "FE_ERRORS", 0)

    raised = 0

    @property
    def errors(self) -> int:
        return self.raised & self.FE_ERRORS

    def __enter__(self) -> fenv:
        if _core is None:
            raise RuntimeError(
                "numpy_sr._numpy_sr is not installed; run `spin build`"
            )
        _core.fenv_clear()
        return self

    def __exit__(self, *exc: object) -> None:
        assert _core is not None  # guaranteed by __enter__
        self.raised = _core.fenv_test()


# --- ctypes mirror of numpy_sr/pyext.h; keep the two in sync -----------------


class _FuncData(ctypes.Structure):
    _fields_ = [
        ("ptr", ctypes.c_void_p),
        ("prec_mask", ctypes.c_uint64),
        ("type_id", ctypes.c_uint8),
    ]


class _Operation(ctypes.Structure):
    _fields_ = [
        ("name", ctypes.c_char_p),
        ("func_id", ctypes.c_uint8),
        ("target_id", ctypes.c_int64),
        ("ndata", ctypes.c_int32),
        ("data", _FuncData * 64),
    ]


class _OperationTable(ctypes.Structure):
    _fields_ = [
        ("noperations", ctypes.c_int32),
        ("operations", ctypes.POINTER(_Operation)),  # contiguous array
    ]


# Operation.target_id of the MPFR oracle (kTargetMpfr in pyext.h).
_TARGET_MPFR = 0
_TYPECODE = {4: "f", 5: "d"}  # TypeID -> numpy typecode

_UNARY = ctypes.CFUNCTYPE(
    None, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t
)
# MPFR oracle kernels write two buffers (ref, residual) from one pass.
_UNARY_REF = ctypes.CFUNCTYPE(
    None, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t
)


# --- targets -----------------------------------------------------------------


class Target:
    """One SIMD target; registered ops are attributes:
    ``target.sin(buf, accuracy=Precise())``."""

    def __init__(
        self,
        name: str,
        target_id: int,
        has_fma: bool = False,
        has_float64: bool = False,
    ):
        self.name = name
        self.target_id = target_id
        # From _numpy_sr.targets(); drive ULP tolerance and dtype pairing.
        self.has_fma = has_fma
        self.has_float64 = has_float64
        # op name -> {(typecode, profile label): ctypes kernel}
        self._kernels: dict[str, dict[tuple[str, str], _Kernel]] = {}

    def __getattr__(self, op: str) -> Callable[..., Any]:
        if op.startswith("_") or op not in self._kernels:
            raise AttributeError(f"{self.name} has no operation {op!r}")
        return self._method(op)

    def __dir__(self) -> list[str]:
        return [*super().__dir__(), *self._kernels]

    def _method(self, op: str) -> Callable[..., Any]:
        def call(
            data: np.ndarray, accuracy: Precise | str = _HIGH
        ) -> np.ndarray:
            profile = (
                accuracy.label if isinstance(accuracy, Precise) else accuracy
            )
            src = np.ascontiguousarray(data)
            out = np.empty_like(src)
            self._kernel(op, profile, src)(
                src.ctypes.data, out.ctypes.data, src.size
            )
            return out

        return call

    def _add(self, op: str, typecode: str, profile: str, fn: _Kernel) -> None:
        self._kernels.setdefault(op, {})[(typecode, profile)] = fn

    def _kernel(self, op: str, profile: str, src: np.ndarray) -> _Kernel:
        table = self._kernels[op]
        fn = table.get((src.dtype.char, profile))
        if fn is not None:
            return fn
        types = sorted({t for t, _ in table})
        if src.dtype.char not in types:
            supported = ", ".join(np.dtype(t).name for t in types)
            raise TypeError(
                f"{self.name}.{op}: unsupported dtype {src.dtype.name!r};"
                f" operates on {supported} -- pass a float array, "
                f"e.g. np.asarray(x, np.float64)"
            )
        profiles = sorted({p for t, p in table if t == src.dtype.char})
        raise ValueError(
            f"{self.name}.{op}: no {src.dtype.name} kernel for profile "
            f"{profile!r}; available profiles={profiles}"
        )

    @property
    def dtypes(self) -> list[type]:
        """Float types with kernels; f64 only on native-f64 hardware."""
        return [np.float32, np.float64] if self.has_float64 else [np.float32]

    def ops(self) -> list[str]:
        return list(self._kernels)

    def __repr__(self) -> str:
        return f"<Target {self.name} ops={self.ops()}>"


class _MpfrReference(Target):
    """MPFR oracle pseudo-target: ``mpfr.sin(x) -> (ref, residual)`` for
    fractional-ULP scoring (numpy_sr.testing); no precision profile."""

    def _method(self, op: str) -> Callable[..., Any]:
        def call(data: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
            src = np.ascontiguousarray(data)
            ref = np.empty_like(src)
            residual = np.empty(src.shape, dtype=np.float64)
            self._kernel(op, "high", src)(
                src.ctypes.data,
                ref.ctypes.data,
                residual.ctypes.data,
                src.size,
            )
            return ref, residual

        return call


# --- plugin loading ----------------------------------------------------------

# Op libraries are installed beside this file (see numpy_sr/meson.build).
_EXTENSIONS_DIR = pathlib.Path(__file__).resolve().parent / "extensions"
_libs: list[ctypes.CDLL] = []  # keep CDLL handles (kernel pointers) alive


def _open(path: pathlib.Path) -> _OperationTable | None:
    """dlopen a plugin and return its OperationTable; warn and return None on
    failure -- everything under extensions/ is expected to load."""
    try:
        lib = ctypes.CDLL(str(path))
        load_fn = lib.npsr_py_load
    except (OSError, AttributeError) as e:
        warnings.warn(
            f"numpy_sr: skipping plugin {path.name}: {e}",
            RuntimeWarning,
            stacklevel=3,
        )
        return None
    load_fn.restype = ctypes.POINTER(_OperationTable)
    load_fn.argtypes = []
    _libs.append(lib)
    return load_fn().contents


def _register(target: Target, op: _Operation) -> None:
    cast = _UNARY_REF if op.target_id == _TARGET_MPFR else _UNARY
    for fd in op.data[: op.ndata]:
        target._add(
            op.name.decode(),
            _TYPECODE[fd.type_id],
            prec_label(fd.prec_mask),
            cast(fd.ptr),
        )


def load() -> dict[str, Target]:
    """(Re)build ``targets``, ``best`` and ``mpfr`` from the bundled op
    libraries."""
    global targets, best, mpfr
    _libs.clear()
    targets, best, mpfr = {}, None, None
    if _core is None:
        return targets

    supported = _core.targets()  # {bit: {"name", "has_fma", "has_float64"}}
    oracle = _MpfrReference("MPFR", _TARGET_MPFR)
    built: dict[str, Target] = {}
    paths = [
        path
        for pattern in ("*.so", "*.dll", "*.dylib")
        for path in sorted(_EXTENSIONS_DIR.rglob(pattern))
    ]
    for path in paths:
        table = _open(path)
        if table is None:
            continue
        for i in range(table.noperations):
            op = table.operations[i]
            if op.target_id == _TARGET_MPFR:
                _register(oracle, op)
            elif (info := supported.get(op.target_id)) is not None:
                target = built.setdefault(
                    info["name"],
                    Target(
                        info["name"],
                        op.target_id,
                        info["has_fma"],
                        info["has_float64"],
                    ),
                )
                _register(target, op)
            # else: an ISA this CPU cannot execute -- drop it.

    mpfr = oracle if oracle._kernels else None
    targets = dict(sorted(built.items(), key=lambda kv: kv[1].target_id))
    best = next(iter(targets.values()), None)
    return targets


def __getattr__(name: str) -> Any:
    """Module-scope ops delegate to the best target: numpy_sr.sin(x)."""
    if best is not None and name in best._kernels:
        return getattr(best, name)
    raise AttributeError(f"module {__name__!r} has no attribute {name!r}")


def __dir__() -> list[str]:
    return [*globals(), *(best._kernels if best is not None else ())]


# No-op if numpy_sr/extensions/ is absent (uninstalled source tree).
load()
