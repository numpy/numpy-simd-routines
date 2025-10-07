// Sollya utility functions for generating C++ header files with mathematical constants and lookup tables

// Initial Sollya configuration
prec = 512;           // High precision for accurate constant computation
display = hexadecimal; // Hex display for exact bit representation
verbosity = 4;        // Verbose output to detect NaN values
showmessagenumbers = on;

// Global state management
THE_OUTPUT_LINES = [||];   // Accumulates generated C++ code lines
THE_DISPLAY_STACK = [||];  // Stack for display mode changes
THE_PREC_STACK = [||];     // Stack for precision changes

// Type descriptors for IEEE 754 floating-point formats
// These objects encapsulate all properties needed for type-specific code generation
Float32 = {
  .kName = "float32",
  .kSize = 32,              // Bits in representation
  .kExpBits = 8,            // Exponent bits
  .kMantBits = 23,          // Mantissa bits (excluding implicit bit)
  .kDigits = 24,            // Precision digits (including implicit bit)
  .kDigits10 = 6,           // Decimal digits of precision
  .kMaxDigits10 = 9,        // Max decimal digits for round-trip
  .kMinExp = -126,          // Minimum exponent (normalized)
  .kMinExp10 = -37,         // Minimum decimal exponent
  .kBias = 127,             // Exponent bias
  .kMaxExp10 = 38,          // Maximum decimal exponent
  .kMinExpDenorm = -149,    // Minimum exponent (denormalized)
  .kMaxExpBiased = 254,     // Maximum biased exponent
  .kMin = 0x1p-126,         // Smallest normalized positive value
  .kLowest = -0x1.fffffep127, // Most negative finite value
  .kMax = 0x1.fffffep127,   // Largest finite value
  .kEps = 0x1p-23,          // Machine epsilon
  .kDenormMin = 0x1p-149,   // Smallest denormalized positive value
  .kPyName = "float32_t",   // Python type name
  .kCSFX = "f",             // C suffix for literals
  .kCName = "float",        // C type name
  .kCUint = "uint32_t",     // Corresponding unsigned integer type
  .kCUintSFX = "u",         // Suffix for unsigned literals
  .kRound = single(x),      // Rounding function
  .kRoundStr = "single",    // String representation of rounding
  .kPrintDigits = "printsingle" // Print function for exact hex output
};

Float64 = {
  .kName = "float64",
  .kSize = 64,
  .kExpBits = 11,
  .kMantBits = 52,
  .kDigits = 53,
  .kDigits10 = 15,
  .kMaxDigits10 = 17,
  .kMinExp = -1022,
  .kMinExp10 = -307,
  .kBias = 1023,
  .kMaxExp10 = 308,
  .kMinExpDenorm = -1074,
  .kMaxExpBiased = 2046,
  .kMin = 0x1p-1022,
  .kLowest = -0x1.fffffffffffffp1023,
  .kMax = 0x1.fffffffffffffp1023,
  .kEps = 0x1p-52,
  .kDenormMin = 0x1p-1074,
  .kPyName = "float64_t",
  .kCSFX = "",
  .kCName = "double",
  .kCUint = "uint64_t",
  .kCUintSFX = "ull",
  .kRound = double(x),
  .kRoundStr = "double",
  .kPrintDigits = "printdouble"
};

// Bit manipulation procedures
// These emulate C-style bit operations that Sollya doesn't natively support

// Right shift operation: equivalent to C's >> operator
procedure RightShift(pN, pK) {
  return floor(pN / 2^pK);
};

// Left shift operation: equivalent to C's << operator
procedure LeftShift(pN, pK) {
  return pN * 2^pK;
};

// String manipulation procedures

// Join list elements with separator (like Python's join)
procedure Join(pList, pSep) {
  var r, i, v;
  r = "";
  for i in pList do {
    v = i @ pSep;
    r = r @ v;
  };
  return r;
};

// Join with automatic line breaks for readability
procedure PrettyJoin(pList, pSfx, pSep, pLineEvery) {
  var r, i, v, l;
  r = "";
  l = 0;
  for i in pList do {
    // Callers that may emit a bare float zero run their lists through FixZero
    // first (0 -> "0.0"), so no "0f" reaches here.
    v = i @ pSfx;
    v = v @ pSep;
    r = r @ v;
    if (pLineEvery > 0) then {
      l = l + 1;
      if (l >= pLineEvery) then {
        r = r @ "\n";
        l = 0;
      };
    };
  };
  return r;
};

// Ensure zeros are represented as 0.0 for C++ template deduction
procedure FixZero(pList) {
  var r, i;
  r = [||];
  for i in pList do {
    if (i == 0) then {
      r = r :. "0.0"; // Ensure zero is represented as 0.0
    } else {
      r = r :. i;
    };
  };
  return r;
};

// C array formatting procedures
// Generate C array initializer
procedure CArray(pList, pLineEvery) {
  return "{\n" @ PrettyJoin(pList, "", ", ", pLineEvery) @ "}";
};

// Generate C array with type-specific suffix (e.g., "f" for float)
procedure CArrayT(pT, pList, pLineEvery) {
  return "{\n" @ PrettyJoin(FixZero(pList), pT.kCSFX, ", ", pLineEvery) @ "}";
};

// Generate C array with unsigned integer suffix
procedure CArrayTU(pT, pList, pLineEvery) {
  return "{\n" @ PrettyJoin(pList, pT.kCUintSFX, ", ", pLineEvery) @ "}";
};

// Python array formatting
procedure PyArray(pList, pLineEvery) {
  return "[\n" @ PrettyJoin(pList, "", ", ", pLineEvery) @ "]";
};

// External tool integration
// These procedures allow Sollya to leverage Python for complex operations

// Execute Python code and return output
// Uses temporary file to pass code to Python interpreter
procedure PyEval(pCode = ...) {
  var code;
  write(Join(pCode, "\n")) > PYTEMP_FILE_PATH;
  code = bashevaluate("python3 " @ PYTEMP_FILE_PATH);
  return code;
};

// Execute Sollya code in a subprocess
// Useful for operations that need isolated evaluation
procedure SolEval(pCode = ...) {
  var code;
  write(Join(pCode, "\n") @ "quit;") > PYTEMP_FILE_PATH;
  code = bashevaluate("sollya " @ PYTEMP_FILE_PATH);
  return code;
};

// Floating-point to integer bit representation conversion

// Convert array of floats to their integer bit representations
// Uses Sollya's print functions to get exact hex, then Python to convert to decimal
procedure ToDigits(pT, pA) {
  var i, code, prfunc, $;
  code = "";
  prfunc = pT.kPrintDigits @ "(";
  
  // Generate Sollya code to print each value in hex
  for i in pA do {
    code = code @ (prfunc @ i @ ");");
  };
  
  // Execute Sollya to get hex representations
  $.hex = SolEval(code); 
  
  // Use Python to convert hex to decimal integers
  $.ints = PyEval(
    "x = '''",
    $.hex,
    "'''",
    "x = [str(int(l, base=0x10)) for l in x.splitlines() if l.strip()]",
    "print('[|', ', '.join(x), '|];')"
  );
  return parse($.ints);
};

// Convert array of integer bit representations back to floats
procedure FromDigits(pT, pA) {
  var i, code, $;
  SetDisplay(decimal);
  // Use Python to format integers as hex strings with Sollya rounding function
  $.hex = PyEval(
    "x = (",
    PyArray(pA, 8),
    ")",
    "rstr = '" @ pT.kRoundStr @ "'",
    "pad = '0" @ pT.kSize / 4 @ "x'",  // Pad to full hex width
    "x = [f'{rstr}(0x{format(l, pad)})' for l in x]",
    "print('[|', ', '.join(x), '|];')"
  );
  RestoreDisplay();
  return parse($.hex);
};

// Multi-precision constant generation
// Splits a constant into multiple floating-point pieces for extended precision
// See usage in the trigonometric constant generation scripts
procedure Constants(pArgs = ...) {
  var r, i, j, $;
  r = [||];
  $.exact = head(pArgs);
  $.remainder = 0;
  for i in tail(pArgs) do {
    $.r_mod = head(i);      // Rounding mode
    for j in tail(i) do {   // Precision bits
      $.val = round($.exact - $.remainder, j, $.r_mod);
      $.remainder = $.remainder + $.val;
      r = r :. $.val;
    };
  };
  return r;
};

// Output accumulation procedures

// Append lines to the output buffer
procedure Append(pLines = ...) {
  suppressmessage(56);  // Suppress assignment warnings
  THE_OUTPUT_LINES = THE_OUTPUT_LINES @ pLines;
  unsuppressmessage(56);
};

// Prepend lines to the output buffer
procedure Prepend(pLines = ...) {
  suppressmessage(56);
  THE_OUTPUT_LINES = pLines @ THE_OUTPUT_LINES;
  unsuppressmessage(56);
};

// Display mode management with stack

// Push current display mode and set new one
procedure SetDisplay(pMod) {
  suppressmessage(56);
  THE_DISPLAY_STACK =  display .: THE_DISPLAY_STACK;
  unsuppressmessage(56);
  display = pMod;
}; 
 
// Pop and restore previous display mode
procedure RestoreDisplay() {
  Assert(
    length(THE_DISPLAY_STACK) > 0,
    "Display stack is empty, cannot restore display."
  );
  display = head(THE_DISPLAY_STACK);
  suppressmessage(56);
  if (length(THE_DISPLAY_STACK) == 1) then {
    THE_DISPLAY_STACK = [||];
  } else {
    THE_DISPLAY_STACK = tail(THE_DISPLAY_STACK);
  };
  unsuppressmessage(56);
};

// Precision management with stack

// Push current precision and set new one
procedure SetPrec(pPrec) {
  suppressmessage(56);
  THE_PREC_STACK = prec .: THE_PREC_STACK;
  unsuppressmessage(56);
  prec = pPrec;
}; 
 
// Pop and restore previous precision
procedure RestorePrec() {
  Assert(
    length(THE_PREC_STACK) > 0,
    "Prec stack is empty, cannot restore prec."
  );
  prec = head(THE_PREC_STACK);
  suppressmessage(56);
  if (length(THE_PREC_STACK) == 1) then {
    THE_PREC_STACK = [||];
  } else {
    THE_PREC_STACK = tail(THE_PREC_STACK);
  };
  unsuppressmessage(56);
};

// Error handling

// Assert condition with error message
// On failure, deletes output file and kills parent process
procedure Assert(pCondition, pMessage) {
  if (!pCondition) then {
    "Assertion failed: " @ pMessage;
    PyEval(
      "import os, signal; from pathlib import Path;",
      "Path('" @OUTPUT_FILE_PATH@ "').unlink(missing_ok=True)",
      "os.kill(os.getppid(), signal.SIGKILL)"
    );
  };
};

// Debug helper - prints accumulated output and exits
procedure Dump() {
  var i;
  for i in THE_OUTPUT_LINES do {
    i; 
  };
  Assert(false, "Dump");
};

// File writing procedures

// Write accumulated output to file and clear buffer
procedure Write() {
  write(Join(THE_OUTPUT_LINES, "\n")) > OUTPUT_FILE_PATH;
  suppressmessage(56);
  THE_OUTPUT_LINES = [||];
  unsuppressmessage(56);
};

// Generate standard C++ header with namespace and include guards
// Example: WriteCPPHeader("npsr", "trig", "data") creates nested namespaces
procedure WriteCPPHeader(pNamespace = ...) { 
  var i, $;
  $.pre = [|
    "// Auto-generated by " @ SOURCE_FILE_PATH,
    "// Use `spin sollya -f` to force regeneration",
    "#ifndef " @ SOURCE_GUARD_NAME,
    "#define " @ SOURCE_GUARD_NAME,
    ""
  |];
  $.post = [||];
  
  // Create nested namespace declarations
  for i in pNamespace do {
    vNamespace = "namespace " @ i;
    $.pre = $.pre :. (vNamespace @ " {");
    $.post = $.post :. ("} // " @ vNamespace);
  };
  
  $.post = $.post @ [|
    "",
    "#endif // " @ SOURCE_GUARD_NAME
  |];
  
  Prepend @ $.pre;
  Append @ $.post;
  Write();
};

// Generate Highway SIMD library header with special include guard pattern
// Highway uses a toggle pattern for target-specific includes
procedure WriteHighwayHeader(pNamespace = ...) { 
  var i, $;
  $.pre = [|
    "// Auto-generated by " @ SOURCE_FILE_PATH,
    "// Use `spin sollya -f` to force regeneration",
    "#if defined("@ SOURCE_GUARD_NAME @") == defined(HWY_TARGET_TOGGLE)  // NOLINT",
    "#ifdef " @ SOURCE_GUARD_NAME,
    "#undef " @ SOURCE_GUARD_NAME,
    "#else",
    "#define " @ SOURCE_GUARD_NAME,
    "#endif",
    "",
    "HWY_BEFORE_NAMESPACE();"
  |];
  $.post = [||];
  
  // Create nested namespace declarations
  for i in pNamespace do {
    vNamespace = "namespace " @ i;
    $.pre = $.pre :. (vNamespace @ " {");
    $.post = $.post :. ("} // " @ vNamespace);
  };
  
  $.post = $.post @ [|
    "HWY_AFTER_NAMESPACE();",
    "#endif // " @ SOURCE_GUARD_NAME
  |];
  
  Prepend @ $.pre;
  // to suppress the unused 'target' attribute warning from '#pragma clang attribute push'
  Append @ [|"inline HWY_ATTR void _dummy_suppress_unused_target(){}"|]; 
  Append @ $.post;
  Write();
};
