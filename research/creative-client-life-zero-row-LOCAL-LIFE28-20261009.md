# LOCAL-LIFE28: zero component multiplier evidence

Pinned native code uses record+0x30 as the multiplier in ClientActor address calculation: storage + group*multiplier + offset. Native 0x8C7396 checks the type-index presence bit independently; 0x8C73D1 loads the multiplier and uses it in arithmetic without a nonzero test. Zero therefore remains valid arithmetic input, but it does not bypass ClientActor presence.

The parent-reported connected-client observation recorded record+0x30 == 0. Separately, LOCAL-LIFE38 type-map evidence shows generic Actor ordinal 3 absent and ClientActor ordinal 54 present in all three stable samples, at group 3128 and offset 7488; see E:\Build\enshrouded-local-life31-components-20261009T1746Z\redacted-type-map-evidence.json. That probe did not sample the newly typed life reader. This repair accepts zero only as a valid multiplier; it does not claim a successful live life snapshot or approval repair.

The helper still requires nonnull component storage, bounds the multiplier at 0x100000, checks address overflow, verifies exact ClientActor presence, and retains ownership and before/after consistency checks. Focused fixtures cover a present ClientActor with zero multiplier at storage + group*0 + offset, zero multiplier with ClientActor absent (still fails at actor-location), and oversized-multiplier rejection. The field remains named stride; evidence establishes multiplier use, not a row-index rename.

Verification: MSVC C++20 /W4 /WX focused runs passed in both parity and fullscreen trees, each with isolated CTest 1/1 and 128 fixture checks. The read-count bound for the typed ClientActor ordinal 54 fixture is 256; measured normal reads were 234.
