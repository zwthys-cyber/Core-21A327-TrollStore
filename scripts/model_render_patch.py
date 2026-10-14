"""Opt-in, pre-signing redirects for the two verified native draw call sites."""
from remote_imports import _redirect_imports
from minimal_features import call

MODEL_RENDER_ALIASES={b'_getpid':b'_cmdraw'}
DRAW_CALLS=(0x21BC0,0x22638)
DRAW_FUNCTION=0xDB144
DRAW_IMPORT_STUB=0x725C20

def redirect_model_render(data):
    result=bytearray(_redirect_imports(data,MODEL_RENDER_ALIASES))
    for site in DRAW_CALLS:
        if data[site:site+4]!=call(site,DRAW_FUNCTION):
            raise ValueError('Unsupported native draw call')
        result[site:site+4]=call(site,DRAW_IMPORT_STUB)
    return bytes(result)
