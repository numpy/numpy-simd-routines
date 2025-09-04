{
var header;
Append("#include \"npsr/lut-inl.h\"");
for header in [|"constants", "kpi16-inl", "approx", "reduction"|] do {
  Append(
    "#include \"npsr/trig/data/" @ header @ ".h\""
  );
};
};

Write();


