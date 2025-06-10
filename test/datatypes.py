import operator
import ctypes
from dataclasses import dataclass
from enum import IntEnum, auto


@dataclass(frozen=True)
class Precise:
    kNoExceptions: bool = False
    kLowAccuracy: bool = False
    kNoLargeArgument: bool = False
    kNoSpecialCases: bool = False
    kDAZ: bool = False
    kFTZ: bool = False
    kIEEE754: bool = True
    kRoundForce: bool = False
    kRoundNearest: bool = True
    kRoundDown: bool = False
    kRoundUp: bool = False
    kRoundZero: bool = False


class _ContainerID(IntEnum):
    SCALAR = 1
    VEC = auto()


class _ElementID(IntEnum):
    VOID = 0
    UINT16 = auto()
    INT16 = auto()
    UINT32 = auto()
    INT32 = auto()
    UINT64 = auto()
    INT64 = auto()
    FLOAT32 = auto()
    FLOAT64 = auto()


def _float_cvt(val):
    if isinstance(val, float):
        return val
    elif isinstance(val, int):
        return float(val)
    elif isinstance(val, str):
        return float.fromhex(val)
    else:
        raise TypeError(f"Cannot convert {type(val)} to float")


class _Sequence:
    def __init__(self, vals):
        data_type = self.element_ctype * len(vals)
        cvt = _float_cvt if self.element_id > _ElementID.INT64 else int
        data = data_type(*[cvt(v) for v in vals])
        self._bytearray = bytearray(bytes(data))

    def data(self):
        data_type = self.element_ctype * len(self)
        return data_type.from_buffer(self._bytearray)

    def __getitem__(self, index):
        data = self.data()
        try:
            data = data[index]
        except IndexError:
            raise IndexError(type(self).__name__ + " index out of range")
        if isinstance(index, int):
            tp = SRType.byid(_ContainerID.SCALAR, self.element_id)
            return tp(data)
        return type(self)(*data)

    def __setitem__(self, index, value):
        data = self.data()
        data[index] = value

    def __len__(self):
        return len(self._bytearray) // self.element_size

    def __iter__(self):
        yield from self._iterate_items()

    def _iterate_items(self):
        tp = SRType.byid(_ContainerID.SCALAR, self.element_id)
        data = self.data()
        for d in data:
            yield tp(d)

    def __str__(self):
        return str(tuple(self.data()))

    def __repr__(self):
        return type(self).__name__ + str(self)

    def to_list(self):
        return [v for v in self.data()]

    def to_bytearray(self):
        return self._bytearray

    @classmethod
    def from_bytes(cls, raw):
        self = super().__new__(cls)
        self._bytearray = bytearray(bytes(raw))
        return self


class _Scalar:
    def __init__(self, val):
        cvt = _float_cvt if self.element_id > _ElementID.INT64 else int
        data = self.element_ctype(cvt(val))
        self._bytearray = bytearray(bytes(data))

    def data(self):
        return self.element_ctype.from_buffer(self._bytearray)

    def __str__(self):
        return str(self.data().value)

    def __repr__(self):
        return type(self).__name__ + f"({str(self)})"

    def __int__(self):
        return int(self.data().value)

    def __float__(self):
        return float(self.data().value)

    def __bool__(self):
        return bool(self.data().value)

    def to_bytearray(self):
        return self._bytearray

    @classmethod
    def from_bytes(cls, raw):
        self = super().__new__(cls)
        self._bytearray = bytearray(bytes(raw))
        return self


class SRType(type):
    _insta_cache = {}
    _prop_by_eid = {
        _ElementID.UINT16: ("uint16_t", ctypes.c_uint16),
        _ElementID.INT16: ("int16_t", ctypes.c_int16),
        _ElementID.UINT32: ("uint32_t", ctypes.c_uint32),
        _ElementID.INT32: ("int32_t", ctypes.c_int32),
        _ElementID.UINT64: ("uint64_t", ctypes.c_uint64),
        _ElementID.INT64: ("int64_t", ctypes.c_int64),
        _ElementID.FLOAT32: ("float32_t", ctypes.c_float),
        _ElementID.FLOAT64: ("float64_t", ctypes.c_double),
    }

    _prop_by_cid = {
        _ContainerID.SCALAR: ("{}", _Scalar),
        _ContainerID.VEC: ("Vec({})", _Sequence),
    }

    def __new__(cls, _, bases, attrs):
        container_id = attrs["container_id"]
        element_id = attrs["element_id"]
        cache_key = (container_id, element_id)
        instance = cls._insta_cache.get(cache_key)
        if instance:
            return instance
        element_name, element_ctype = cls._prop_by_eid[element_id]
        container_name, container_base = cls._prop_by_cid[container_id]
        bases += (container_base,)
        name = container_name.format(element_name)
        attrs.update(
            dict(element_ctype=element_ctype, element_size=ctypes.sizeof(element_ctype))
        )
        instance = super().__new__(cls, name, bases, attrs)
        SRType._insta_cache[cache_key] = instance
        return instance

    @classmethod
    def byid(cls, container_id, element_id):
        return SRType(cls, (), dict(container_id=container_id, element_id=element_id))


class uint16_t(metaclass=SRType):
    container_id = _ContainerID.SCALAR
    element_id = _ElementID.UINT16


class int16_t(metaclass=SRType):
    container_id = _ContainerID.SCALAR
    element_id = _ElementID.INT16


class uint32_t(metaclass=SRType):
    container_id = _ContainerID.SCALAR
    element_id = _ElementID.UINT32


class int32_t(metaclass=SRType):
    container_id = _ContainerID.SCALAR
    element_id = _ElementID.INT32


class uint64_t(metaclass=SRType):
    container_id = _ContainerID.SCALAR
    element_id = _ElementID.UINT64


class int64_t(metaclass=SRType):
    container_id = _ContainerID.SCALAR
    element_id = _ElementID.INT64


class float32_t(metaclass=SRType):
    container_id = _ContainerID.SCALAR
    element_id = _ElementID.FLOAT32


class float64_t(metaclass=SRType):
    container_id = _ContainerID.SCALAR
    element_id = _ElementID.FLOAT64


class Vec(SRType):
    def __new__(cls, element_type):
        s = cls.byid(_ContainerID.VEC, element_type.element_id)
        return s
