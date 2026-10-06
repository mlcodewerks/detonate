"""Embed the pinned UADE player data in the helper (no runtime data install)."""
import pathlib
import sys

output, *inputs = sys.argv[1:]
assets = []
for filename in inputs:
    path = pathlib.Path(filename)
    # Every input comes from the vendored data directory.
    parts = path.parts
    index = parts.index("data")
    assets.append((pathlib.PurePosixPath(*parts[index + 1:]).as_posix(), path.read_bytes()))
with pathlib.Path(output).open("w", encoding="utf-8", newline="\n") as stream:
    stream.write("#pragma once\n#include <cstddef>\n")
    for i, (name, data) in enumerate(assets):
        stream.write(f"static const unsigned char asset_{i}[] = {{\n")
        for offset in range(0, len(data), 32):
            stream.write(",".join(str(b) for b in data[offset:offset + 32]) + ",\n")
        stream.write("};\n")
    stream.write("struct uade_asset { const char *name; const unsigned char *bytes; size_t size; };\n")
    stream.write("static const uade_asset assets[] = {\n")
    for i, (name, data) in enumerate(assets):
        stream.write(f'{{"{name}", asset_{i}, {len(data)}}},\n')
    stream.write("};\n")
