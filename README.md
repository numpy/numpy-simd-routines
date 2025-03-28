# NumPy SIMD Routines

A collection of C++ SIMD implementations for numerical operations in NumPy.

> **Note:** This project is in early stages of development. APIs, structure, and functionality may change significantly as the project evolves.

## Overview

`numpy-simd-routines` is a collection of C++ header files that provide optimized SIMD (Single Instruction Multiple Data) implementations for common numerical operations used in NumPy.
The library consists of inline functions that can be easily integrated into NumPy or other numerical computing projects.

These routines are developed outside the main NumPy repository for two primary reasons:

1. To share optimized SIMD implementations with other projects in the Python numerical computing ecosystem, enabling code reuse across multiple libraries
2. To speed up development by focusing on SIMD optimizations separately from the main NumPy codebase

As a collection of header files, it can be easily integrated into NumPy or other numerical computing projects without build dependencies, simplifying adoption and maintenance.
