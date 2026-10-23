#!/usr/bin/env python3
"""Pokrycie CSS Silvera: ile właściwości jest parsowanych, a ile faktycznie renderowanych.
Źródła: css_properties.txt (pełna lista), css_rendered.txt (renderowane), src/engine/props_data.h# (rejestr).
Użycie: python3 tools/css_coverage.py"""
import os, re, sys
H = os.path.dirname(os.path.abspath(__file__))
def words(path):
    out = []
    for l in open(os.path.join(H, path), encoding='utf-8'):
        if l.startswith('#'): continue
        out += l.split()
    return out
props = words('css_properties.txt')
rendered = set(words('css_rendered.txt'))
data = open(os.path.join(H, '..', 'src', 'engine', 'props_data.h#'), encoding='utf-8').read()
registry = set(re.findall(r'([a-z0-9-]+)\\u0001[01]\\u0001', data))
parsed = [p for p in props if p in registry]
rend = [p for p in props if p in rendered]
bad = [p for p in rendered if p not in props]
# najczęściej używane właściwości (przekrój typowych arkuszy stylów)
TOP = """display position top right bottom left z-index float clear width height min-width max-width min-height max-height
margin margin-top margin-right margin-bottom margin-left padding padding-top padding-right padding-bottom padding-left
border border-top border-right border-bottom border-left border-width border-style border-color border-radius box-sizing box-shadow
overflow overflow-x overflow-y visibility opacity color background background-color background-image background-position
background-size background-repeat font font-family font-size font-weight font-style line-height letter-spacing text-align
text-decoration text-transform text-overflow text-shadow white-space word-break overflow-wrap vertical-align flex flex-direction
flex-wrap flex-grow flex-shrink flex-basis flex-flow justify-content align-items align-content align-self order gap row-gap
column-gap grid-template-columns grid-template-rows grid-column grid-row grid-area grid-auto-flow place-items cursor
pointer-events user-select transition transition-property transition-duration transition-timing-function transition-delay
animation animation-name animation-duration animation-timing-function animation-delay animation-iteration-count
animation-direction animation-fill-mode transform transform-origin filter backdrop-filter clip-path object-fit object-position
aspect-ratio content outline outline-offset list-style resize appearance scroll-behavior inset""".split()
top_r = [p for p in TOP if p in rendered]
top_p = [p for p in TOP if p not in rendered]
def pct(a, b): return "%.1f%%" % (100.0 * a / b)
print("Właściwości CSS na liście:        %d" % len(props))
print("Parsowane / walidowane / w stylu: %d (%s)" % (len(parsed), pct(len(parsed), len(props))))
print("Faktycznie renderowane:           %d (%s)" % (len(rend), pct(len(rend), len(props))))
print("Najczęściej używane (%d):          renderowane %d (%s)" % (len(TOP), len(top_r), pct(len(top_r), len(TOP))))
if top_p: print("  popularne, ale tylko parsowane:  " + " ".join(top_p))
if bad: print("UWAGA: w css_rendered.txt są nazwy spoza listy:", " ".join(bad)); sys.exit(1)
if len(parsed) < len(props) * 0.5 or len(rend) < len(props) * 0.5:
    print("PONIŻEJ PROGU 50%"); sys.exit(2)
