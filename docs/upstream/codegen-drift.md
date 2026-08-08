# Upstream issue draft — file on rexglue/rexglue-sdk

**Title:** codegen regression (0.8.1.28 → 0.8.1.93): internal branch label dropped → `InvalidFunctionTrap` at runtime

**Body:**

## Summary
A function body that an older rexglue emitted with an internal `goto loc_XXXX`
(label inside the same function) is, on a newer rexglue, emitted without that
label/target — so the guest's internal jump becomes a call into an unmapped
address and the runtime aborts via `InvalidFunctionTrap` ("Call to invalid or
unregistered function at guest address 0x…"). The **function-entry set is
identical** between the two codegen versions; only the body emission differs.

## Repro (Mushihimesama Futari Ver 1.5, reco_normal module, base 0x88000000)
- **rexglue 0.8.1.28** generated `reco_normal/futari_recomp.4.cpp` contains:
  ```cpp
  ... goto loc_880EC954;   // and: goto loc_880EC900;
  loc_880EC900:            // internal labels inside one function
  loc_880EC954:
  ```
  → reco character select works.
- **rexglue 0.8.1.93** (`c3d47a0`) generated code has **no** `loc_880EC900`/
  `loc_880EC954` and **no** `sub_880EC900`/`sub_880EC954`. At runtime, entering
  reco character select:
  ```
  [FATAL] Call to invalid or unregistered function at guest address 0x880EC954
  abort() in rex::runtime::InvalidFunctionTrap
    sub_88152CF0+1140  ->  sub_88138F00  ->  sub_88168D48  (in reco_normal)
  ```
- Diff of the two `generated/reco_normal/` trees: `futari_recomp.6/.7.cpp` are
  identical; `futari_recomp.0.cpp` and `futari_recomp.4.cpp` differ; the
  registered function set (`futari_register.cpp` sub_ entries) is identical.

## Suspected area
Branch/label emission or jump-table handling changed between 0.8.1.28 and
0.8.1.93. A `git bisect` across that range should pin it; the commit "jump-table
under-sizing fix" (bumped the futari pin to `c3d47a0`) is a plausible starting
suspect since it touches that path.

## Impact
Platform-independent (reproduced on macOS/arm64 but the generated C++ is the
same on any target). Any title relying on this internal-jump pattern would hit
it on the newer codegen.

## Workaround in use
Built the affected modules from the 0.8.1.28-generated `futari_recomp.*.cpp`
bodies on top of 0.8.1.93 mac glue — game runs through gameplay. Not a fix.
