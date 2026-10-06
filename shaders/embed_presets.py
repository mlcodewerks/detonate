import json
import pathlib
import sys

with pathlib.Path(sys.argv[1]).open('w', encoding='utf-8', newline='\n') as out:
    out.write('static constexpr shader_view::preset shader_presets[] = {\n')
    for filename in sys.argv[2:]:
        path = pathlib.Path(filename)
        out.write('{' + json.dumps(path.stem.title()) + ', R"STOY(')
        out.write(path.read_text(encoding='utf-8'))
        out.write(')STOY"},\n')
    out.write('};\n')
