from .datatypes import (
    uint16_t,
    int16_t,
    uint32_t,
    int32_t,
    uint64_t,
    int64_t,
    float32_t,
    float64_t,
    Vec,
    Precise,
    SRType,
)
from .utils import bitcast


from ._intrins import __targets__ as targets


def _get_type_from_overload_arg(probs):
    scalar_map = {
        (2, 0, 1): uint16_t,
        (2, 0, 0): int16_t,
        (4, 0, 1): uint32_t,
        (4, 0, 0): int32_t,
        (8, 0, 1): uint64_t,
        (8, 0, 0): int64_t,
        (4, 1, 0): float32_t,
        (8, 1, 0): float64_t,
    }
    scalar_type = scalar_map.get(
        (probs["kTypeSize"], probs["kIsFloat"], probs["kIsUnsigned"])
    )
    if scalar_type is None:
        raise ValueError(
            f"Unsupported scalar type: kTypeSize={probs['kTypeSize']}, "
            f"kIsFloat={probs['kIsFloat']}, kIsUnsigned={probs['kIsUnsigned']}"
        )
    nlanes = probs.get("kLanes", 1)
    if nlanes == 1:
        return scalar_type
    else:
        return Vec(scalar_type)


class WrapIntrinsic:
    def __init__(self, name, overloads):
        self.name = name
        self.overloads = {}
        for cfunc, info in overloads:
            args_types = []
            ret_type = None
            for dct in info:
                if dct is None:
                    continue
                if dct.pop("kIsPrecise", False):
                    args_types.append(Precise(**dct))
                elif dct.pop("kIsRet", False):
                    ret_type = _get_type_from_overload_arg(dct)
                elif dct:
                    args_types += [_get_type_from_overload_arg(dct)]
            self.overloads[hash(tuple(args_types))] = (cfunc, ret_type, args_types)

    def signatures(self):
        args = self.overloads.values()
        return "\n".join(
            [
                f"{ret_type.__name__} {self.name}( {', '.join([str(a) for a in args_types])} )"
                for _, ret_type, args_types in args
            ]
        )

    def __call__(self, *args):
        # Check if the first argument is a Precise instance
        # hash of Precise based on the value of its attributes
        args_prec = [a if isinstance(a, Precise) else type(a) for a in args]
        args_only = [a for a in args if not isinstance(a, Precise)]
        try:
            h = hash(tuple(args_prec))
            cfunc, ret_type, _ = self.overloads.get(h, [None] * 3)
        # incase of mismatch unhashable types
        except TypeError:
            cfunc, ret_type = None, None
        if not cfunc:
            args_str = ", ".join([str(a) for a in args_prec])
            raise TypeError(
                f"no matching signature to call {self.name}( {args_str} )\n"
                f"only the following signatures are supported:\n" + self.signatures()
            )
        ret_bytes = cfunc(*[a.to_bytearray() for a in args_only])
        if not isinstance(ret_bytes, bytearray):
            raise TypeError(
                f"expected bytearray return type from {self.name}, got {type(ret_bytes).__name__}"
            )
        if ret_type is not None:
            return ret_type.from_bytes(ret_bytes)
        return ret_type


class SimdExtention:
    def __init__(self, name, mod_ext):
        self.__name__ = name
        self.intrinsics = {}
        for name, val in mod_ext.__dict__.items():
            if name.startswith("_"):
                setattr(self, name, val)
                continue
            func = WrapIntrinsic(name, val)
            self.intrinsics[name] = func
            setattr(self, name, func)

        for extra_intrin in ["Lanes", "Set", "BitCast", "assert_ulp"]:
            self.intrinsics[extra_intrin] = getattr(self, extra_intrin)

    def __repr__(self):
        return "numpy_sr." + self.__name__

    def __str__(self):
        return self.__name__

    def Lanes(self, element_type):
        return self._REGISTER_WIDTH // element_type.element_size

    def Set(self, element_type, val):
        return Vec(element_type)([val] * self.Lanes(element_type))

    def BitCast(self, eltype, value):
        return bitcast(eltype, value)

    def assert_ulp(self, actual, expected, input, max_ulp=1, per_line=4):
        __tracebackhide__ = True  # Hide traceback for py.test
        ulp = self.UlpDistance(actual, expected)
        actual, expected, input, ulp = (
            arg.to_list() for arg in [actual, expected, input, ulp]
        )
        if any([i > max_ulp for i in ulp]):
            vals = []
            for i, ul in enumerate(ulp):
                if int(ul) <= max_ulp:
                    continue
                vals.append(
                    str(tuple(x for x in (input[i], actual[i], expected[i], ul)))
                )
            raise AssertionError(
                f"Expected ULP distance {max_ulp}, worst {max(ulp)}, interleaved(input, actual, expected, ulp) as follow:"
                + "\n "
                + "\n ".join(vals)
                + "\n",
            )


wrap_targets = {}
for name, simd_ext in targets.items():
    wrap_targets[name] = SimdExtention(name, simd_ext)
    globals()[name] = wrap_targets[name]
targets = wrap_targets

__version__ = "1.0.0"

__all__ = [
    "uint16_t",
    "int16_t",
    "uint32_t",
    "int32_t",
    "uint64_t",
    "int64_t",
    "float32_t",
    "float64_t",
    "Vec",
    "Precise",
    "targets",
    "bitcast",
]
