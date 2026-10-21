"""Observe the existing actor snapshot and its two reads, without new reads.

Only the original call destinations change. The injected bridge forwards the
same snapshot arguments/indirect result and remote-reader arguments/results.
"""
from minimal_features import call
from model_render_patch import DRAW_IMPORT_STUB

VERSION='1'
PROFILE='six-independent-ground-items-with-upstream-source-diagnostics'
ARTIFACT='Core-17.0-21A327-TrollStore-model-render-v19-source-scan.ipa'
CALLS=((0xD5404,0xD632C),(0xD63E0,0xE1908),(0xD6410,0xE1908))


def changes():
    return [(site,call(site,target),call(site,DRAW_IMPORT_STUB),
             'forward existing source snapshot' if i==0 else 'observe existing snapshot read result')
            for i,(site,target) in enumerate(CALLS)]


def source_scan_trace(data):
    result=bytearray(data)
    for site,old,new,reason in changes():
        if data[site:site+4]!=old:raise ValueError(f'Unexpected source scan call at {site:#x}')
        result[site:site+4]=new
    return bytes(result)


def manifest():return [dict(offset=hex(site),before=old.hex(),after=new.hex(),reason=reason) for site,old,new,reason in changes()]
