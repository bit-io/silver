#!/usr/bin/env python3
"""Generuje native/silver_api_embed.h z src/js/silver_polyfills.js + silver_api.js (wbudowanie API w libsilverjs.a)."""
import os, sys
here = os.path.dirname(os.path.abspath(__file__))
src = b''.join(open(os.path.join(here, '..', 'src', 'js', f), 'rb').read() + b'\n' for f in ('silver_polyfills.js', 'silver_api.js'))
out = ['/* GENEROWANY z src/js/silver_polyfills.js + silver_api.js przez native/gen_api_embed.py — nie edytuj ręcznie. */',
       'static const unsigned char silver_api_js[] = {']
row = []
for i, b in enumerate(src):
    row.append(str(b))
    if len(row) == 24:
        out.append('  ' + ','.join(row) + ','); row = []
if row: out.append('  ' + ','.join(row) + ',')
out.append('  0\n};')
out.append('static const unsigned int silver_api_js_len = %d;' % len(src))
open(os.path.join(here, 'silver_api_embed.h'), 'w').write('\n'.join(out) + '\n')
print('silver_api_embed.h:', len(src), 'bajtów')
