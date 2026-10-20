"""Defer item distance/screen rejection until the existing final camera pass.

v17 collects before DA010 updates the camera and DA130 reprojects item records.
Its old-camera range and screen rejection can discard a record before that
pass. Keep finite/nonnegative validation while collecting; the unchanged
renderer checks updated distance <=600 and the final projection flag.
"""
from minimal_features import branch,word
from nearby_vehicles import _tbz

VERSION='1'
PROFILE='enemy-markers-600m-five-ground-items-final-camera-filters'
ARTIFACT='Core-17.0-21A327-TrollStore-model-render-v18-ground-late-camera.ipa'


def changes():
    return [
        (0xD9E64,branch(0xD9E64,0xD9EC8,12),word(0xD503201F),'defer 600-metre eligibility to unchanged final-camera render check'),
        (0xD9E6C,_tbz(0xD9E6C,0xD9EC8,20),word(0xD503201F),'retain current candidate for existing late-camera projection instead of pre-camera screen cull'),
    ]


def ground_late_camera(data):
    result=bytearray(data)
    for offset,old,new,reason in changes():
        if data[offset:offset+len(old)]!=old:raise ValueError(f'Unexpected ground late-camera source at {offset:#x}: {reason}')
        result[offset:offset+len(old)]=new
    return bytes(result)


def manifest():
    return [dict(offset=hex(offset),before=old.hex(),after=new.hex(),reason=reason) for offset,old,new,reason in changes()]
