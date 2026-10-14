#!/usr/bin/env python3
"""Package a local capture as a standalone offline viewer; no mesh execution.

Raw device captures stay local. Do not commit the generated HTML or send it to CI.
"""
import argparse
import json
import re
from pathlib import Path

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('report',type=Path)
    parser.add_argument('output',type=Path)
    args=parser.parse_args()
    if args.report.stat().st_size>16*1024*1024:
        raise SystemExit('Report exceeds 16 MiB')
    data=json.loads(args.report.read_text())
    if data.get('capture_version')!=7:
        raise SystemExit('Requires protocol 7')
    root=Path(__file__).parent
    module=re.sub(r'^export ', '', (root/'model_mesh_preview.mjs').read_text(),flags=re.MULTILINE)
    payload=json.dumps(data,ensure_ascii=False,separators=(',',':')).replace('<','\\u003c')
    html=(root/'model_mesh_preview.html').read_text().replace('// __MODEL_PREVIEW_MODULE__',module).replace('__CAPTURE_JSON__',payload)
    args.output.write_text(html)
    print(f'{args.output}: {args.output.stat().st_size} bytes; local capture embedded; offline only')

if __name__=='__main__':
    main()
