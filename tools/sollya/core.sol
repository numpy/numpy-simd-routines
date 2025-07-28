prec = 512;
display = hexadecimal;
verbosity = 3;
showmessagenumbers = on;

THE_OUTPUT_LINES = [||];
THE_DISPLAY_STACK = [||];
THE_PREC_STACK = [||];

Float32 = {
  .kName = "float32",
  .kSize = 32,
  .kExpBits = 8,
  .kMantBits = 23,
  .kDigits = 24,
  .kDigits10 = 6,
  .kMaxDigits10 = 9,
  .kMinExp = -126,
  .kMinExp10 = -37,
  .kBias = 127,
  .kMaxExp10 = 38,
  .kMinExpDenorm = -149,
  .kMaxExpBiased = 254, 
  .kMin = 0x1p-126,
  .kLowest = -0x1.fffffep127,
  .kMax = 0x1.fffffep127,
  .kEps = 0x1p-23,
  .kDenormMin = 0x1p-149,
  .kPyName = "float32_t",
  .kCSFX = "f",
  .kCName = "float",
  .kCUint = "uint32_t",
  .kCUintSFX = "u",
  .kRound = single(x),
  .kRoundStr = "single",
  .kPrintDigits = "printsingle"
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

procedure RightShift(pN, pK) {
  return floor(pN / 2^pK);
};

procedure LeftShift(pN, pK) {
  return pN * 2^pK;
};

procedure Join(pList, pSep) {
  var r, i, v;
  r = "";
  for i in pList do {
    v = i @ pSep;
    r = r @ v;
  };
  return r;
};

procedure PyEval(pCode = ...) {
  var code;
  write(Join(pCode, "\n")) > PYTEMP_FILE_PATH;
  code = bashevaluate("python3 " @ PYTEMP_FILE_PATH);
  return code;
};

procedure SolEval(pCode = ...) {
  var code;
  write(Join(pCode, "\n") @ "quit;") > PYTEMP_FILE_PATH;
  code = bashevaluate("sollya " @ PYTEMP_FILE_PATH);
  return code;
};

procedure ToDigits(pT, pA) {
  var i, code, prfunc, $;
  code = "";
  prfunc = pT.kPrintDigits @ "(";
  for i in pA do {
    code = code @ (prfunc @ i @ ");");
  };
  $.hex = SolEval(code); 
  $.ints = PyEval(
    "x = '''",
    $.hex,
    "'''",
    "x = [str(int(l, base=0x10)) for l in x.splitlines() if l.strip()]",
    "print('[|', ', '.join(x), '|];')"
  );
  return parse($.ints);
};

procedure FromDigits(pT, pA) {
  var i, code, $;
  SetDisplay(decimal);
  $.hex = PyEval(
    "x = (",
    ToStringPyArray(pA, 8),
    ")",
    "rstr = '" @ pT.kRoundStr @ "'",
    "pad = '0" @ pT.kSize / 4 @ "x'",
    "x = [f'{rstr}(0x{format(l, pad)})' for l in x]",
    "print('[|', ', '.join(x), '|];')"
  );
  RestoreDisplay();
  return parse($.hex);
};

procedure ToStringArray(pData, pSFX, pColNum) {
  var r, r_final, i, j, col_widths, $;
  r = [||];
  for i from 0 to length(pData) - 1 do {
    $.v = pData[i] @ pSFX @ ", ";
    if ($.v == "0f, ") then $.v = "0.0f, "; 
    r = r :. $.v;
  };
  // Determine the max width for each column
  col_widths = [||];
  for i from 0 to pColNum - 1 do {
    col_widths = col_widths :. 0;
  };
  for i from 0 to length(r) - 1 do {
    $.idx = mod(i, pColNum);
    if (length(r[i]) > col_widths[$.idx]) then {
      col_widths[$.idx] = length(r[i]);
    };
  };
  
  // Create paddding string
  $.pad = "";
  for i from 1 to 2 do $.pad = $.pad @ " ";
  
  // Build lines array
  r_final = "";
  i = 0;
  while (i < length(r)) do {
    var chunks, chunk, line;
    // Create chunk of 'col' elements
    chunks = [||];
    for j from 0 to pColNum - 1 do {
        if (i + j < length(r)) then {
            chunks = chunks :. r[i + j];
        };
    };
    line = $.pad;
    for j from 0 to length(chunks) - 1 do {
      $.idx = mod(i + j, pColNum);
      chunk = chunks[j];
      // Left-justify to width
      while (length(chunk) < col_widths[$.idx]) do {
        chunk = chunk @ " ";
      };
      line = line @ chunk;
    };
    r_final = r_final @ line @ "\n";
    i = i + pColNum;
  };
  return r_final;
};

procedure ToStringCArray(pArr, pSFX, pNumCol) {
  return "{\n" @ ToStringArray(pArr, pSFX, pNumCol) @ "};";
};

procedure ToStringPyArray(pArr, pNumCol) {
  return "[\n" @ ToStringArray(pArr, "", pNumCol) @ "]";
};

procedure ConstantsFromArray(pArr) {
  var r, i, j, $;
  r = [||];
  $.exact = head(pArr);
  $.remainder = 0;
  for i in tail(pArr) do {
    $.r_mod = head(i);
    for j in tail(i) do {
      $.val = round($.exact - $.remainder, j, $.r_mod);
      $.remainder = $.remainder + $.val;
      r = r :. $.val;
    };
  };
  return r;
};
procedure Constants(pArgs = ...) {
  return ConstantsFromArray(pArgs);
};

procedure Append(pLines = ...) {
  suppressmessage(56);
  THE_OUTPUT_LINES = THE_OUTPUT_LINES @ pLines;
  unsuppressmessage(56);
};

procedure SetDisplay(pMod) {
  suppressmessage(56);
  THE_DISPLAY_STACK =  display .: THE_DISPLAY_STACK;
  unsuppressmessage(56);
  display = pMod;
}; 
 
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

procedure SetPrec(pPrec) {
  suppressmessage(56);
  THE_PREC_STACK = prec .: THE_PREC_STACK;
  unsuppressmessage(56);
  prec = pPrec;
}; 
 
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

procedure Dump() {
  var i;
  for i in THE_OUTPUT_LINES do {
    i; 
  };
  Assert(false, "Dump");
};

procedure Write() {
  write(Join(THE_OUTPUT_LINES, "\n")) > OUTPUT_FILE_PATH;
  suppressmessage(56);
  THE_OUTPUT_LINES = [||];
  unsuppressmessage(56);
};

procedure WriteCPPHeader(pNamespace = ...) { 
  var i, $;
  $.pre = [|
    "// Auto-generated by " @ SOURCE_FILE_PATH,
    "// Use `spin generate -f` to force regeneration",
    "#ifndef " @ SOURCE_GUARD_NAME,
    "#define " @ SOURCE_GUARD_NAME,
    ""
  |];
  $.post = [||];
  for i in pNamespace do {
    vNamespace = "namespace " @ i;
    $.pre = $.pre :. (vNamespace @ " {");
    $.post = $.post :. ("} // " @ vNamespace);
  };
  $.post = $.post @ [|
    "",
    "#endif // " @ SOURCE_GUARD_NAME
  |]; 
  Append @ ($.pre @ THE_OUTPUT_LINES @ $.post);
  Write();
};

