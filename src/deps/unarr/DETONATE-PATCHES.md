# Local archive reader fixes

- ZIP and 7z skip directory runs iteratively to avoid exhausting the call stack.
- ZIP extra-field bounds include the four-byte field header before reading ZIP64 values.
- 7z checks look-ahead buffer allocation, uses the common cleanup path on failure,
  and avoids null-pointer arithmetic when extracting an empty member.
- Individual 7z SDK allocations are limited to 256 MiB, including allocations
  for encoded headers and dictionaries made before Detonate can inspect entries.
  This is a per-allocation limit, not an aggregate memory budget.

Regression coverage is in `test/archive_browser_smoke.cpp`.
