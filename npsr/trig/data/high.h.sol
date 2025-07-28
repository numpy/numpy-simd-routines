procedure PiDivTable_(pT, pFunc, pBy) {
  var r, i, pi_by;
  pi_by = pi / pBy;
  r = [||];
  for i from 0 to pBy - 1 do {
    r = r :. pT.kRound(pFunc(i * pi_by));
  };
  return ToStringCArray(r, pT.kCSFX, 1);
};

procedure PiDivPackLowTable_(pT, pFunc0, pFunc1, pBy) {
  var r, i, digits, $;
  $.pi_by = pi / pBy;
  r = [||];
  for i from 0 to pBy - 1 do {
    $.hi0 = pT.kRound(pFunc0(i * $.pi_by));
    $.hi1 = pT.kRound(pFunc1(i * $.pi_by));
    $.hi0_low = pT.kRound(pFunc0(i * $.pi_by) - $.hi0);
    $.hi1_low = pT.kRound(pFunc1(i * $.pi_by) - $.hi1);
    r = r @ [|$.hi0_low, $.hi1_low|];
  }; 
  digits = ToDigits(pT, r);
  $.half_size = pT.kSize / 2;
  $.lower_bits = 2^$.half_size;
  r = [||];
  for i from 0 to length(digits) - 1 by 2 do {
    $.hi0 = digits[i];
    $.hi1 = digits[i + 1];
    // F64: (hi1 & 0xFFFFFFFF00000000) | ((hi0 >> 32) & 0xFFFFFFFF)
    $.pack = mod(RightShift($.hi0, $.half_size), $.lower_bits);
    $.pack = $.pack + $.hi1 - mod($.hi1, $.lower_bits);
    r = r :. $.pack; 
  };
  r = FromDigits(pT, r);
  return ToStringCArray(r, "", 4);
};

Append(
  "constexpr double kHiSinKPi16Table[] = " @ 
  PiDivTable_(Float64, sin(x), 16),
  "",
  "constexpr double kHiCosKPi16Table[] = " @ 
  PiDivTable_(Float64, cos(x), 16),
  ""
);

Append(
  "constexpr double kPackedLowSinCosKPi16Table[] = " @
  PiDivPackLowTable_(Float64, sin(x), cos(x), 16),
  ""
);

// Dump();
WriteCPPHeader("npsr::trig::data");
