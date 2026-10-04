import os, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TPL = os.path.join(ROOT, 'templates')
SKIP_DIRS = {'node_modules', 'dist', 'cache', '.git'}
SKIP_FILES = {'notes.json', 'todos.json'}
DESC = {
    'vanilla':      'czysty HTML/CSS/JS (najprostszy start)',
    'counter':      'licznik: stan w H#, cykliczny on_tick, JS z async/await',
    'kitchen_sink': 'przegląd silnika: formularze, flex, grid, przewijanie, motyw ciemny',
    'todo_js':      'lista zadań napisana głównie w JS (DOM, zapis przez H#, dialogi)',
    'dashboard':    'pulpit: grid, wykres z JS, zadania w tle, fetch, przejścia CSS',
    'ts_preact':    'TypeScript + Preact + esbuild',
    'svelte':       'Svelte 5 + esbuild',
}

def lit(s):
    s = s.replace('\\', '\\\\').replace('"', '\\"').replace('\r', '')
    s = s.replace('{', '{{').replace('}', '}}')
    s = s.replace('\n', '\\n').replace('\t', '\\t')
    return '"' + s + '"'

def read_tree(base, prefix=''):
    out = []
    for dp, dn, fn in os.walk(base):
        dn[:] = sorted(d for d in dn if d not in SKIP_DIRS)
        for f in sorted(fn):
            if f in SKIP_FILES or f.endswith(('.ttf', '.png', '.pyc')):
                continue
            full = os.path.join(dp, f)
            rel = os.path.relpath(full, base).replace(os.sep, '/')
            try:
                out.append((prefix + rel, open(full, encoding='utf-8').read()))
            except UnicodeDecodeError:
                pass
    return out

def emit_fn(name, files):
    lines = ['fn %s() -> [string] is' % name, '    let mut f: [string] = []']
    for path, content in files:
        lines.append('    f = array_push(f, %s)' % lit(path))
        lines.append('    f = array_push(f, %s)' % lit(content))
    lines.append('    return f')
    lines.append('end\n')
    return '\n'.join(lines)

def main():
    names = sorted(d for d in os.listdir(TPL) if os.path.isdir(os.path.join(TPL, d)))
    out = [';; GENEROWANY przez tools/gen_templates.py — nie edytuj ręcznie.',
           ';; Źródła: templates/<nazwa>/** oraz packaging/** (kopiowane do każdego projektu).\n']
    for n in names:
        out.append(emit_fn('files_' + n, read_tree(os.path.join(TPL, n))))
    out.append(emit_fn('files_common', read_tree(os.path.join(ROOT, 'packaging'), 'packaging/')))
    out.append('pub fn names() -> [string] is\n    return [%s]\nend\n' % ', '.join('"%s"' % n for n in names))
    out.append('pub fn description(name: string) -> string is')
    for n in names:
        out.append('    if name == "%s" is return %s end' % (n, lit(DESC.get(n, ''))))
    out.append('    return ""\nend\n')
    out.append('pub fn files(name: string) -> [string] is')
    for n in names:
        out.append('    if name == "%s" is return files_%s() end' % (n, n))
    out.append('    return []\nend\n')
    out.append('pub fn common() -> [string] is\n    return files_common()\nend')
    open(os.path.join(ROOT, 'src', 'cli', 'templates_data.h#'), 'w', encoding='utf-8').write('\n'.join(out) + '\n')
    print('templates_data.h#:', len(names), 'szablonów')

if __name__ == '__main__':
    main()
