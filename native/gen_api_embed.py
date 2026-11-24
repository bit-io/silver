#!/usr/bin/env python3
"""Generuje native/silver_api_embed.h z src/js/*.js.

  silver_api_js    = silver_polyfills.js + silver_serial.js + silver_intl.js + silver_api.js     (główny kontekst JS)
  silver_worker_js = silver_polyfills.js + silver_serial.js + silver_worker.js  (środowisko Workera, 0.5)
"""
import os
here = os.path.dirname(os.path.abspath(__file__))
def cat(files):
    return b''.join(open(os.path.join(here, '..', 'src', 'js', f), 'rb').read() + b'\n' for f in files)
def emit(name, data, out):
    out.append('static const unsigned char %s[] = {' % name)
    row = []
    for b in data:
        row.append(str(b))
        if len(row) == 24:
            out.append('  ' + ','.join(row) + ','); row = []
    if row: out.append('  ' + ','.join(row) + ',')
    out.append('  0\n};')
    out.append('static const unsigned int %s_len = %d;' % (name, len(data)))
api = cat(('silver_polyfills.js', 'silver_serial.js', 'silver_intl.js', 'silver_api.js'))
wrk = cat(('silver_polyfills.js', 'silver_serial.js', 'silver_worker.js'))
out = ['/* GENEROWANY z plikow src/js przez native/gen_api_embed.py - nie edytuj recznie. */']
emit('silver_api_js', api, out)
emit('silver_worker_js', wrk, out)
open(os.path.join(here, 'silver_api_embed.h'), 'w').write('\n'.join(out) + '\n')
print('silver_api_embed.h: api', len(api), 'B, worker', len(wrk), 'B')
