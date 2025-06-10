#!/usr/bin/env python3
"""
Precompute int(2^exp × 4/π) with ~96-bit precision (f32) or ~192-bit precision (f64)
and split them into three chunks: 32-bit chunks for single precision, 64-bit chunks for double precision.

This generates a lookup table for large range reduction in trigonometric functions.
The table is used to compute mantissa × (2^exp × 4/π) using wider integer multiplications for precision:
- f32: 16×16 → 32-bit multiplications
- f64: 32×32 → 64-bit multiplications

For input x = mantissa × 2^exp, the algorithm becomes:
x × 4/π = mantissa × table_lookup[exp], providing high precision without floating-point errors.

Args:
    float_size: 32 for f32 or 64 for f64
"""
from generator import c_array, c_header, sollya


def gen_reduction(float_size=64):
    exp = (1 << (8 if float_size == 32 else 11)) - 1
    offset = 70 if float_size == 32 else 137
    bias = exp >> 1
    sol = f"""
    prec = {bias + offset + 1}; 
    four_over_pi = 4 / pi;
    for e from 0 to {exp} do {{ 
        e_shift = e - {bias} + {offset}; 
        floor(four_over_pi * (2^e_shift));
    }};
    """
    ints = sollya(sol, as_int=10)
    chunk_mask = (1 << float_size) - 1
    chunks = [
        hex((i >> shift) & chunk_mask)
        for i in ints
        for shift in [float_size * 2, float_size, 0]
    ]
    return chunks


print(
    c_header(
        "template <typename T> constexpr T kLargeReductionTable[] = {};",
        "template <> constexpr uint32_t kLargeReductionTable<float>[] = "
        + c_array(gen_reduction(32), col=3, sfx="U"),
        "template <> constexpr uint64_t kLargeReductionTable<double>[] = "
        + c_array(gen_reduction(64), col=3, sfx="ULL"),
        namespace="npsr::trig::data",
    )
)
