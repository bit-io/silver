(function (g) {
  'use strict';

  // ------------------------------------------------------------ console
  function fmt(v, depth) {
    depth = depth || 0;
    if (typeof v === 'string') return v;
    if (v === undefined) return 'undefined';
    if (v === null) return 'null';
    if (v instanceof Error) return (v.name + ': ' + v.message) + (v.stack ? '\n' + v.stack : '');
    if (typeof v === 'function') return '[Function ' + (v.name || 'anonymous') + ']';
    if (typeof v === 'object') {
      if (depth > 3) return '[Object]';
      try { return JSON.stringify(v); } catch (e) { return String(v); }
    }
    return String(v);
  }
  function logger(level) {
    return function () {
      var parts = [];
      for (var i = 0; i < arguments.length; i++) parts.push(fmt(arguments[i]));
      __silver_console(level, parts.join(' '));
    };
  }
  g.console = {
    log: logger('log'), info: logger('info'), warn: logger('warn'),
    error: logger('error'), debug: logger('debug'), trace: logger('trace'),
    assert: function (c) { if (!c) { var a = [].slice.call(arguments, 1); logger('error').apply(null, ['Assertion failed:'].concat(a)); } },
    table: function (v) { logger('log')(v); }
  };

  // ------------------------------------------------------------ timers
  var t0 = __silver_now();
  g.performance = { now: function () { return __silver_now() - t0; } };
  var timerSeq = 0;
  var timers = new Map();
  function addTimer(fn, ms, repeat, args) {
    if (typeof fn !== 'function') return 0;
    var id = ++timerSeq;
    timers.set(id, { fn: fn, args: args, repeat: repeat });
    __silver_timer_set(id, Math.max(0, Math.floor(+ms || 0)), repeat);
    return id;
  }
  g.setTimeout = function (fn, ms) { return addTimer(fn, ms, false, [].slice.call(arguments, 2)); };
  g.setInterval = function (fn, ms) { return addTimer(fn, ms, true, [].slice.call(arguments, 2)); };
  g.clearTimeout = g.clearInterval = function (id) { timers.delete(id); __silver_timer_clear(id); };
  g.requestAnimationFrame = function (fn) { return g.setTimeout(function () { fn(g.performance.now()); }, 16); };
  g.cancelAnimationFrame = g.clearTimeout;
  if (typeof g.queueMicrotask !== 'function') g.queueMicrotask = function (fn) { Promise.resolve().then(fn); };
  g.__silver_fire_timer = function (idStr) {
    var id = +idStr, t = timers.get(id);
    if (!t) return;
    if (!t.repeat) timers.delete(id);
    try { t.fn.apply(null, t.args); } catch (e) { console.error(e); }
  };

  // ------------------------------------------------------------ invoke
  var pending = new Map();
  var callSeq = 0;
  function invoke(cmd, args) {
    return new Promise(function (resolve, reject) {
      var id = 'c' + (++callSeq);
      pending.set(id, { resolve: resolve, reject: reject });
      var ok = __silver_native_invoke(id, String(cmd), JSON.stringify(args === undefined ? {} : args));
      if (!ok) { pending.delete(id); reject(new Error('silver: kolejka wywołań jest pełna')); }
    });
  }
  g.__silver_resolve = function (id, json) {
    var p = pending.get(id);
    if (!p) return;
    pending.delete(id);
    try { p.resolve(json === '' || json === 'null' ? null : JSON.parse(json)); }
    catch (e) { p.reject(e); }
  };
  g.__silver_reject = function (id, msg) {
    var p = pending.get(id);
    if (!p) return;
    pending.delete(id);
    p.reject(new Error(msg));
  };

  // ------------------------------------------------------------ zdarzenia natywne
  var listeners = new Map();
  function listen(name, cb) {
    if (!listeners.has(name)) listeners.set(name, new Set());
    listeners.get(name).add(cb);
    return function () { listeners.get(name).delete(cb); };
  }
  function once(name, cb) {
    var off = listen(name, function (p) { off(); cb(p); });
    return off;
  }
  var internal = {};
  g.__silver_dispatch_event = function (name, json) {
    var payload = null;
    try { payload = json === '' ? null : JSON.parse(json); } catch (e) { payload = json; }
    if (internal[name]) { try { internal[name](payload); } catch (e) { console.error(e); } return; }
    var set = listeners.get(name);
    if (!set) return;
    set.forEach(function (cb) { try { cb(payload); } catch (e) { console.error(e); } });
  };

  // ------------------------------------------------------------ silver.*
  function win(op, extra) {
    var a = { op: op };
    for (var k in extra) a[k] = extra[k];
    return invoke('__window', a);
  }
  var silver = {
    version: '0.2.0',
    invoke: invoke,
    listen: listen,
    once: once,
    window: {
      setTitle: function (t) { return win('title', { title: String(t) }); },
      setFullscreen: function (on) { return win('fullscreen', { on: !!on }); },
      setSize: function (w, h) { return win('size', { w: w | 0, h: h | 0 }); },
      setMinSize: function (w, h) { return win('min_size', { w: w | 0, h: h | 0 }); },
      setPosition: function (x, y) { return win('position', { x: x | 0, y: y | 0 }); },
      maximize: function () { return win('maximize'); },
      minimize: function () { return win('minimize'); },
      restore: function () { return win('restore'); },
      close: function () { return win('close'); },
      setIcon: function (p) { return win('icon', { path: String(p) }); },
      setOpacity: function (pct) { return win('opacity', { pct: pct | 0 }); },
      setAlwaysOnTop: function (on) { return win('always_on_top', { on: !!on }); },
      setDecorations: function (on) { return win('decorations', { on: !!on }); },
      notify: function (title, body) { return win('notify', { title: String(title), body: String(body || '') }); }
    },
    dialog: {
      open: function (o) { o = o || {}; return invoke('__dialog', { kind: 'open', title: o.title || '', desc: o.description || '', pattern: o.pattern || '', dir: o.dir || '' }).then(function (r) { return r.path || null; }); },
      openMultiple: function (o) { o = o || {}; return invoke('__dialog', { kind: 'open_multi', title: o.title || '', desc: o.description || '', pattern: o.pattern || '', dir: o.dir || '' }).then(function (r) { return r.paths ? r.paths.split('\n') : []; }); },
      save: function (o) { o = o || {}; return invoke('__dialog', { kind: 'save', title: o.title || '', name: o.name || '', desc: o.description || '', pattern: o.pattern || '' }).then(function (r) { return r.path || null; }); },
      folder: function (o) { o = o || {}; return invoke('__dialog', { kind: 'folder', title: o.title || '', dir: o.dir || '' }).then(function (r) { return r.path || null; }); },
      message: function (text, o) { o = o || {}; return invoke('__dialog', { kind: 'message', title: o.title || '', text: String(text), level: o.level || 'info' }).then(function () { }); },
      confirm: function (text, o) { o = o || {}; return invoke('__dialog', { kind: 'confirm', title: o.title || '', text: String(text) }).then(function (r) { return !!r.ok; }); }
    },
    clipboard: {
      readText: function () { return invoke('__clip', { op: 'get' }).then(function (r) { return r.text; }); },
      writeText: function (t) { return invoke('__clip', { op: 'set', text: String(t) }).then(function () { }); }
    }
  };
  g.silver = silver;
  g.alert = function (m) { return silver.dialog.message(m); };

  g.fetch = function (url, opts) {
    opts = opts || {};
    return invoke('__fetch', { url: String(url), method: String(opts.method || 'GET'), body: opts.body == null ? '' : String(opts.body) })
      .then(function (r) {
        return {
          ok: !!r.ok, status: r.status, url: String(url), statusText: r.ok ? 'OK' : '',
          text: function () { return Promise.resolve(r.body); },
          json: function () { return Promise.resolve().then(function () { return JSON.parse(r.body); }); }
        };
      });
  };

  var store = new Map();
  g.localStorage = {
    getItem: function (k) { return store.has(String(k)) ? store.get(String(k)) : null; },
    setItem: function (k, v) { store.set(String(k), String(v)); },
    removeItem: function (k) { store.delete(String(k)); },
    clear: function () { store.clear(); },
    key: function (i) { return Array.from(store.keys())[i] || null; },
    get length() { return store.size; }
  };

  // ------------------------------------------------------------ lustro DOM
  var recs = new Map();       // id -> rekord {i,p,t,x,k,a,l,el}
  var provSeq = 1000000;
  var ops = [];
  var flushQueued = false;
  var focusedId = -1;
  var winInfo = { w: 0, h: 0 };

  function queueOp(o) {
    ops.push(o);
    if (!flushQueued) { flushQueued = true; Promise.resolve().then(flush); }
  }
  function flush() {
    flushQueued = false;
    if (!ops.length) return;
    var batch = ops.splice(0, ops.length);
    invoke('__dom', { ops: batch }).catch(function (e) { console.error(e); });
  }

  function rec(id) { return recs.get(id); }
  function newRec(id, tag, text) {
    var r = { i: id, p: -1, t: tag, x: text || '', k: [], a: {}, l: null, el: null };
    recs.set(id, r);
    return r;
  }
  var VOID = { br: 1, hr: 1, img: 1, input: 1, meta: 1, link: 1, area: 1, base: 1, col: 1, embed: 1, source: 1, track: 1, wbr: 1 };

  // ---- selektory (podzbiór CSS) ----
  function splitTop(s, ch) {
    var out = [], depth = 0, cur = '', q = '';
    for (var i = 0; i < s.length; i++) {
      var c = s[i];
      if (q) { cur += c; if (c === q) q = ''; continue; }
      if (c === '"' || c === "'") { q = c; cur += c; continue; }
      if (c === '(' || c === '[') depth++;
      if (c === ')' || c === ']') depth--;
      if (c === ch && depth === 0) { out.push(cur); cur = ''; } else cur += c;
    }
    out.push(cur);
    return out;
  }
  function parseCompound(tok) {
    var c = { tag: '', id: '', cls: [], attrs: [], pseudos: [], nots: [] };
    var i = 0, n = tok.length;
    var m = /^[a-zA-Z][a-zA-Z0-9-]*|^\*/.exec(tok);
    if (m) { c.tag = m[0] === '*' ? '' : m[0].toLowerCase(); i = m[0].length; }
    while (i < n) {
      var ch = tok[i];
      if (ch === '.') { m = /^[\w-]+/.exec(tok.slice(i + 1)); c.cls.push(m[0]); i += 1 + m[0].length; }
      else if (ch === '#') { m = /^[\w-]+/.exec(tok.slice(i + 1)); c.id = m[0]; i += 1 + m[0].length; }
      else if (ch === '[') {
        var e = tok.indexOf(']', i);
        var inner = tok.slice(i + 1, e);
        var am = /^\s*([\w:-]+)\s*(?:([~|^$*]?=)\s*(?:"([^"]*)"|'([^']*)'|([^\s\]]*)))?\s*$/.exec(inner);
        if (am) c.attrs.push({ n: am[1].toLowerCase(), op: am[2] || '', v: am[3] !== undefined ? am[3] : (am[4] !== undefined ? am[4] : (am[5] || '')) });
        i = e + 1;
      } else if (ch === ':') {
        m = /^:([\w-]+)(?:\((.*)\))?/.exec(tok.slice(i));
        if (!m) { i++; continue; }
        if (m[1] === 'not') c.nots.push(m[2]); else c.pseudos.push(m[1] + (m[2] ? '(' + m[2] + ')' : ''));
        i += m[0].length;
      } else i++;
    }
    return c;
  }
  function tokenizePath(sel) {
    var toks = [], cur = '', depth = 0, comb = ' ';
    var pushTok = function () { if (cur) { toks.push({ c: parseCompound(cur), comb: comb }); cur = ''; comb = ' '; } };
    for (var i = 0; i < sel.length; i++) {
      var ch = sel[i];
      if (ch === '(' || ch === '[') depth++;
      if (ch === ')' || ch === ']') depth--;
      if (depth === 0 && (ch === '>' || ch === '+' || ch === '~')) { pushTok(); comb = ch; }
      else if (depth === 0 && /\s/.test(ch)) { if (cur) { pushTok(); } }
      else cur += ch;
    }
    pushTok();
    return toks;
  }
  function siblingsOf(r) {
    var p = rec(r.p);
    if (!p) return [r];
    var out = [];
    for (var i = 0; i < p.k.length; i++) { var s = rec(p.k[i]); if (s && s.t !== 'text') out.push(s); }
    return out;
  }
  function matchCompound(r, c) {
    if (r.t === 'text' || r.t === 'root') return false;
    if (c.tag && c.tag !== r.t) return false;
    if (c.id && c.id !== (r.a.id || '')) return false;
    if (c.cls.length) {
      var have = (r.a['class'] || '').split(/\s+/);
      for (var i = 0; i < c.cls.length; i++) if (have.indexOf(c.cls[i]) < 0) return false;
    }
    for (var j = 0; j < c.attrs.length; j++) {
      var at = c.attrs[j];
      if (!(at.n in r.a)) return false;
      var v = r.a[at.n];
      if (at.op === '=' && v !== at.v) return false;
      if (at.op === '^=' && !(at.v && v.indexOf(at.v) === 0)) return false;
      if (at.op === '$=' && !(at.v && v.slice(-at.v.length) === at.v)) return false;
      if (at.op === '*=' && !(at.v && v.indexOf(at.v) >= 0)) return false;
      if (at.op === '~=' && v.split(/\s+/).indexOf(at.v) < 0) return false;
      if (at.op === '|=' && !(v === at.v || v.indexOf(at.v + '-') === 0)) return false;
    }
    for (var k = 0; k < c.pseudos.length; k++) {
      var ps = c.pseudos[k], sib = siblingsOf(r), idx = sib.indexOf(r);
      if (ps === 'first-child' && idx !== 0) return false;
      else if (ps === 'last-child' && idx !== sib.length - 1) return false;
      else if (ps === 'only-child' && sib.length !== 1) return false;
      else if (ps === 'empty' && r.k.length) return false;
      else if (ps === 'checked' && !('checked' in r.a)) return false;
      else if (ps === 'disabled' && !('disabled' in r.a)) return false;
      else if (ps === 'enabled' && ('disabled' in r.a)) return false;
      else if (ps === 'focus' && r.i !== focusedId) return false;
      else if (/^nth-child\(/.test(ps)) {
        var ex = /\((.*)\)/.exec(ps)[1].trim(), ok = false, pos = idx + 1;
        if (ex === 'odd') ok = pos % 2 === 1; else if (ex === 'even') ok = pos % 2 === 0;
        else {
          var nm = /^([+-]?\d*)n\s*([+-]\s*\d+)?$/.exec(ex);
          if (nm) {
            var a = nm[1] === '' || nm[1] === '+' ? 1 : (nm[1] === '-' ? -1 : +nm[1]);
            var b = nm[2] ? +nm[2].replace(/\s+/g, '') : 0;
            ok = a === 0 ? pos === b : ((pos - b) % a === 0 && (pos - b) / a >= 0);
          } else ok = pos === +ex;
        }
        if (!ok) return false;
      }
    }
    for (var q = 0; q < c.nots.length; q++) if (matchesSelector(r, c.nots[q])) return false;
    return true;
  }
  function matchPath(r, toks, idx) {
    if (!matchCompound(r, toks[idx].c)) return false;
    if (idx === 0) return true;
    var comb = toks[idx].comb;
    if (comb === '>') { var p = rec(r.p); return !!p && matchPath(p, toks, idx - 1); }
    if (comb === '+' || comb === '~') {
      var sib = siblingsOf(r), pos = sib.indexOf(r);
      for (var i = pos - 1; i >= 0; i--) { if (matchPath(sib[i], toks, idx - 1)) return true; if (comb === '+') break; }
      return false;
    }
    var anc = rec(r.p);
    while (anc) { if (matchPath(anc, toks, idx - 1)) return true; anc = rec(anc.p); }
    return false;
  }
  function matchesSelector(r, sel) {
    var groups = splitTop(sel, ',');
    for (var i = 0; i < groups.length; i++) {
      var toks = tokenizePath(groups[i].trim());
      if (toks.length && matchPath(r, toks, toks.length - 1)) return true;
    }
    return false;
  }
  function walk(r, fn) {
    for (var i = 0; i < r.k.length; i++) {
      var c = rec(r.k[i]);
      if (!c) continue;
      if (fn(c) === false) return false;
      if (walk(c, fn) === false) return false;
    }
    return true;
  }
  function queryAll(root, sel, first) {
    var out = [];
    walk(root, function (c) {
      if (c.t !== 'text' && matchesSelector(c, sel)) { out.push(wrap(c)); if (first) return false; }
    });
    return out;
  }

  // ---- tekst / serializacja ----
  function textOf(r) {
    if (r.t === 'text') return r.x;
    var s = '';
    for (var i = 0; i < r.k.length; i++) { var c = rec(r.k[i]); if (c) s += textOf(c); }
    return s;
  }
  function esc(s) { return s.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;'); }
  function outerOf(r) {
    if (r.t === 'text') return esc(r.x);
    var s = '<' + r.t;
    for (var k in r.a) s += ' ' + k + '="' + String(r.a[k]).replace(/"/g, '&quot;') + '"';
    s += '>';
    if (VOID[r.t]) return s;
    return s + innerOf(r) + '</' + r.t + '>';
  }
  function innerOf(r) {
    var s = '';
    for (var i = 0; i < r.k.length; i++) { var c = rec(r.k[i]); if (c) s += outerOf(c); }
    return s;
  }

  // ---- struktura ----
  function detach(r) {
    var p = rec(r.p);
    if (p) { var i = p.k.indexOf(r.i); if (i >= 0) p.k.splice(i, 1); }
    r.p = -1;
  }
  function dropSubtree(r) {
    // usuwa rekordy potomków (po ustawieniu nowego innerHTML)
    for (var i = 0; i < r.k.length; i++) { var c = rec(r.k[i]); if (c) { dropSubtree(c); c.p = -1; } }
    r.k = [];
  }
  function insertRec(parent, child, before) {
    if (child.t === '#fragment') {
      var moving = child.k.slice();
      child.k = [];
      moving.forEach(function (cid) { var c = rec(cid); if (c) insertRec(parent, c, before); });
      return;
    }
    if (parent.t === '#fragment') {
      detach(child); child.p = parent.i;
      var bi0 = before ? parent.k.indexOf(before.i) : -1;
      if (bi0 >= 0) parent.k.splice(bi0, 0, child.i); else parent.k.push(child.i);
      return;
    }
    detach(child);
    child.p = parent.i;
    var bi = before ? parent.k.indexOf(before.i) : -1;
    if (bi >= 0) parent.k.splice(bi, 0, child.i); else parent.k.push(child.i);
    queueOp({ o: 'append', n: parent.i, c: child.i, b: bi >= 0 ? before.i : -1 });
  }
  function removeRec(r) {
    detach(r);
    queueOp({ o: 'remove', n: r.i });
  }


  // ---- lokalny parser HTML (synchroniczny innerHTML, <template>, DocumentFragment) ----
  var RAW = { script: 1, style: 1, textarea: 1, title: 1 };
  function decodeEnt(s) {
    return s.replace(/&(#x[0-9a-f]+|#\d+|[a-z]+);/gi, function (m, e) {
      if (e[0] === '#') return String.fromCodePoint(e[1] === 'x' || e[1] === 'X' ? parseInt(e.slice(2), 16) : parseInt(e.slice(1), 10));
      var t = { amp: '&', lt: '<', gt: '>', quot: '"', apos: "'", nbsp: '\u00a0' }[e.toLowerCase()];
      return t === undefined ? m : t;
    });
  }
  function parseHTML(html) {
    var root = { t: '#root', k: [] }, stack = [root], i = 0, n = html.length;
    while (i < n) {
      var top = stack[stack.length - 1];
      if (html[i] !== '<') {
        var e = html.indexOf('<', i); if (e < 0) e = n;
        var txt = html.slice(i, e);
        if (txt) top.k.push({ t: '#text', x: decodeEnt(txt) });
        i = e; continue;
      }
      if (html.substr(i, 4) === '<!--') { var ce = html.indexOf('-->', i + 4); var cm = html.slice(i + 4, ce < 0 ? n : ce); top.k.push({ t: '#comment', x: cm }); i = ce < 0 ? n : ce + 3; continue; }
      if (html[i + 1] === '!' || html[i + 1] === '?') { var de = html.indexOf('>', i); i = de < 0 ? n : de + 1; continue; }
      if (html[i + 1] === '/') {
        var ge = html.indexOf('>', i); var nm = html.slice(i + 2, ge < 0 ? n : ge).trim().toLowerCase();
        for (var j = stack.length - 1; j > 0; j--) if (stack[j].t === nm) { stack.length = j; break; }
        i = ge < 0 ? n : ge + 1; continue;
      }
      var m = /^<([a-zA-Z][a-zA-Z0-9-]*)/.exec(html.slice(i, i + 64));
      if (!m) { top.k.push({ t: '#text', x: '<' }); i++; continue; }
      var el = { t: m[1].toLowerCase(), a: {}, k: [] };
      i += m[0].length;
      var self = false;
      while (i < n) {
        while (i < n && /\s/.test(html[i])) i++;
        if (html[i] === '>') { i++; break; }
        if (html[i] === '/') { self = true; i++; continue; }
        var am = /^([^\s=>\/]+)(?:\s*=\s*(?:"([^"]*)"|'([^']*)'|([^\s>]*)))?/.exec(html.slice(i, i + 4096));
        if (!am) { i++; continue; }
        el.a[am[1].toLowerCase()] = decodeEnt(am[2] !== undefined ? am[2] : (am[3] !== undefined ? am[3] : (am[4] !== undefined ? am[4] : '')));
        i += am[0].length;
      }
      top.k.push(el);
      if (RAW[el.t] && !self) {
        var close = html.toLowerCase().indexOf('</' + el.t, i);
        var raw = html.slice(i, close < 0 ? n : close);
        if (raw) el.k.push({ t: '#text', x: raw });
        var ge2 = close < 0 ? n : html.indexOf('>', close);
        i = ge2 < 0 ? n : ge2 + 1;
      } else if (!VOID[el.t] && !self) stack.push(el);
    }
    return root.k;
  }
  function buildNodes(parent, nodes, before) {
    for (var i = 0; i < nodes.length; i++) {
      var nd = nodes[i], el;
      if (nd.t === '#text') el = document.createTextNode(nd.x);
      else if (nd.t === '#comment') el = document.createComment(nd.x);
      else {
        el = document.createElement(nd.t);
        for (var k in nd.a) setAttr(el.__r, k, nd.a[k]);
        buildNodes(el.__r, nd.k, null);
      }
      insertRec(parent, el.__r, before || null);
    }
  }

  // ---- klasa Element ----
  function Element(r) { this.__r = r; r.el = this; }
  function wrap(r) {
    if (!r) return null;
    return r.el || new Element(r);
  }
  function unwrap(el) { return el && el.__r; }
  var P = Element.prototype;
  function def(name, getter, setter) {
    Object.defineProperty(P, name, { get: getter, set: setter, configurable: true, enumerable: true });
  }
  function setAttr(r, k, v) {
    k = String(k).toLowerCase();
    var sv = String(v);
    if (k === 'disabled' || k === 'checked' || k === 'readonly') { if (v === false) { delete r.a[k]; queueOp({ o: 'attr', n: r.i, k: k, v: 'false' }); return; } sv = ''; }
    r.a[k] = sv;
    queueOp({ o: 'attr', n: r.i, k: k, v: sv });
  }
  function removeAttr(r, k) {
    k = String(k).toLowerCase();
    delete r.a[k];
    queueOp({ o: 'rattr', n: r.i, k: k });
  }
  def('nodeType', function () { var t = this.__r.t; return t === 'text' ? 3 : (t === '#comment' ? 8 : (t === '#fragment' ? 11 : 1)); });
  def('nodeName', function () { var t = this.__r.t; return t === 'text' ? '#text' : (t[0] === '#' ? t : t.toUpperCase()); });
  def('tagName', function () { return this.__r.t.toUpperCase(); });
  def('id', function () { return this.__r.a.id || ''; }, function (v) { setAttr(this.__r, 'id', v); });
  def('className', function () { return this.__r.a['class'] || ''; }, function (v) { setAttr(this.__r, 'class', v); });
  def('textContent', function () { return textOf(this.__r); }, function (v) {
    var r = this.__r;
    if (r.t === 'text') { r.x = String(v); queueOp({ o: 'text', n: r.i, v: r.x }); return; }
    dropSubtree(r);
    if (String(v) !== '') {
      var tr = newRec(++provSeq, 'text', String(v)); tr.p = r.i; r.k.push(tr.i);
    }
    queueOp({ o: 'text', n: r.i, v: String(v) });
  });
  def('innerText', function () { return textOf(this.__r); }, function (v) { this.textContent = v; });
  def('data', function () { return this.__r.x; }, function (v) { this.textContent = v; });
  def('nodeValue', function () { return this.__r.t === 'text' ? this.__r.x : null; }, function (v) { if (this.__r.t === 'text') this.textContent = v; });
  def('innerHTML', function () { return this.__r.t === 'template' && this.__r.tpl !== undefined ? this.__r.tpl : innerOf(this.__r); }, function (v) {
    var r = this.__r;
    if (r.t === 'template') { r.tpl = String(v); r.frag = null; return; }
    r.k.slice().forEach(function (cid) { var c = rec(cid); if (c) removeRec(c); });
    r.k = [];
    buildNodes(r, parseHTML(String(v)), null);
  });
  def('content', function () {
    var r = this.__r;
    if (r.t !== 'template') return undefined;
    if (!r.frag) { var f = document.createDocumentFragment(); buildNodes(f.__r, parseHTML(r.tpl || ''), null); r.frag = f; }
    return r.frag;
  });
  def('textContent_', function () { return ''; });
  def('outerHTML', function () { return outerOf(this.__r); });
  def('value', function () { return this.__r.a.value !== undefined ? this.__r.a.value : (this.__r.t === 'textarea' ? textOf(this.__r) : ''); },
    function (v) { this.__r.a.value = String(v); queueOp({ o: 'value', n: this.__r.i, v: String(v) }); });
  def('checked', function () { return 'checked' in this.__r.a; }, function (v) { setAttr(this.__r, 'checked', v ? '' : false); });
  def('disabled', function () { return 'disabled' in this.__r.a; }, function (v) { setAttr(this.__r, 'disabled', v ? '' : false); });
  def('hidden', function () { return 'hidden' in this.__r.a; }, function (v) {
    if (v) setAttr(this.__r, 'hidden', ''); else removeAttr(this.__r, 'hidden');
    var st = this.__r.a.style || '';
    st = st.replace(/(^|;)\s*display\s*:[^;]*/g, '$1').replace(/^;+/, '');
    setAttr(this.__r, 'style', v ? (st ? st + ';' : '') + 'display:none' : st);
  });
  def('type', function () { return this.__r.a.type || ''; }, function (v) { setAttr(this.__r, 'type', v); });
  def('name', function () { return this.__r.a.name || ''; }, function (v) { setAttr(this.__r, 'name', v); });
  def('href', function () { return this.__r.a.href || ''; }, function (v) { setAttr(this.__r, 'href', v); });
  def('src', function () { return this.__r.a.src || ''; }, function (v) { setAttr(this.__r, 'src', v); });
  def('placeholder', function () { return this.__r.a.placeholder || ''; }, function (v) { setAttr(this.__r, 'placeholder', v); });
  def('title', function () { return this.__r.a.title || ''; }, function (v) { setAttr(this.__r, 'title', v); });
  def('parentNode', function () { return wrap(rec(this.__r.p)); });
  def('parentElement', function () { var p = rec(this.__r.p); return p && p.t !== 'root' ? wrap(p) : null; });
  def('childNodes', function () { return this.__r.k.map(function (i) { return wrap(rec(i)); }); });
  def('children', function () { return this.__r.k.map(function (i) { return rec(i); }).filter(function (c) { return c && c.t !== 'text'; }).map(wrap); });
  def('childElementCount', function () { return this.children.length; });
  def('firstChild', function () { return wrap(rec(this.__r.k[0])); });
  def('lastChild', function () { return wrap(rec(this.__r.k[this.__r.k.length - 1])); });
  def('firstElementChild', function () { return this.children[0] || null; });
  def('lastElementChild', function () { var c = this.children; return c[c.length - 1] || null; });
  function sib(r, dir, elOnly) {
    var p = rec(r.p); if (!p) return null;
    var i = p.k.indexOf(r.i) + dir;
    while (i >= 0 && i < p.k.length) { var c = rec(p.k[i]); if (c && (!elOnly || c.t !== 'text')) return wrap(c); i += dir; }
    return null;
  }
  def('nextSibling', function () { return sib(this.__r, 1, false); });
  def('previousSibling', function () { return sib(this.__r, -1, false); });
  def('nextElementSibling', function () { return sib(this.__r, 1, true); });
  def('previousElementSibling', function () { return sib(this.__r, -1, true); });
  def('offsetWidth', function () { return 0; });
  def('offsetHeight', function () { return 0; });
  def('clientWidth', function () { return 0; });
  def('clientHeight', function () { return 0; });
  def('scrollHeight', function () { return 0; });
  def('isConnected', function () { var r = this.__r; while (r) { if (r.i === 0) return true; r = rec(r.p); } return false; });
  def('scrollTop', function () { return this.__scroll || 0; }, function (v) { this.__scroll = v | 0; queueOp({ o: 'scroll', n: this.__r.i, v: v | 0 }); });

  def('classList', function () {
    var r = this.__r;
    function list() { return (r.a['class'] || '').split(/\s+/).filter(Boolean); }
    function save(l) { setAttr(r, 'class', l.join(' ')); }
    return {
      add: function () { var l = list(); for (var i = 0; i < arguments.length; i++) if (l.indexOf(arguments[i]) < 0) l.push(arguments[i]); save(l); },
      remove: function () { var rm = [].slice.call(arguments); save(list().filter(function (c) { return rm.indexOf(c) < 0; })); },
      toggle: function (c, force) { var l = list(), has = l.indexOf(c) >= 0, want = force === undefined ? !has : !!force; if (want && !has) l.push(c); if (!want && has) l = l.filter(function (x) { return x !== c; }); save(l); return want; },
      contains: function (c) { return list().indexOf(c) >= 0; },
      replace: function (a, b) { var l = list(); var i = l.indexOf(a); if (i < 0) return false; l[i] = b; save(l); return true; },
      get length() { return list().length; },
      item: function (i) { return list()[i] || null; },
      toString: function () { return list().join(' '); }
    };
  });
  def('style', function () {
    var r = this.__r;
    function parse() { var o = {}; (r.a.style || '').split(';').forEach(function (d) { var i = d.indexOf(':'); if (i > 0) o[d.slice(0, i).trim()] = d.slice(i + 1).trim(); }); return o; }
    function save(o) { var s = Object.keys(o).map(function (k) { return k + ':' + o[k]; }).join(';'); setAttr(r, 'style', s); }
    function kebab(n) { return n.replace(/[A-Z]/g, function (m) { return '-' + m.toLowerCase(); }); }
    var api = {
      setProperty: function (k, v) { var o = parse(); o[kebab(k)] = v; save(o); },
      removeProperty: function (k) { var o = parse(); var old = o[kebab(k)]; delete o[kebab(k)]; save(o); return old || ''; },
      getPropertyValue: function (k) { return parse()[kebab(k)] || ''; }
    };
    Object.defineProperty(api, 'cssText', { get: function () { return r.a.style || ''; }, set: function (v) { setAttr(r, 'style', v); } });
    return new Proxy(api, {
      get: function (t, k) { if (k in t) return t[k]; if (typeof k !== 'string') return undefined; return parse()[kebab(k)] || ''; },
      set: function (t, k, v) { if (k === 'cssText') { t.cssText = v; return true; } var o = parse(); if (v === '' || v == null) delete o[kebab(String(k))]; else o[kebab(String(k))] = String(v); save(o); return true; }
    });
  });
  def('dataset', function () {
    var r = this.__r;
    function key(k) { return 'data-' + String(k).replace(/[A-Z]/g, function (m) { return '-' + m.toLowerCase(); }); }
    return new Proxy({}, {
      get: function (t, k) { return typeof k === 'string' ? r.a[key(k)] : undefined; },
      set: function (t, k, v) { setAttr(r, key(k), v); return true; },
      deleteProperty: function (t, k) { removeAttr(r, key(k)); return true; },
      has: function (t, k) { return key(k) in r.a; },
      ownKeys: function () { return Object.keys(r.a).filter(function (k) { return k.indexOf('data-') === 0; }).map(function (k) { return k.slice(5).replace(/-([a-z])/g, function (m, c) { return c.toUpperCase(); }); }); },
      getOwnPropertyDescriptor: function () { return { enumerable: true, configurable: true }; }
    });
  });

  P.getAttribute = function (k) { k = String(k).toLowerCase(); return k in this.__r.a ? this.__r.a[k] : null; };
  P.setAttribute = function (k, v) { setAttr(this.__r, k, v); };
  P.removeAttribute = function (k) { removeAttr(this.__r, k); };
  P.setAttributeNS = function (ns, k, v) { setAttr(this.__r, String(k).replace(/^.*:/, ''), v); };
  P.removeAttributeNS = function (ns, k) { removeAttr(this.__r, k); };
  P.getAttributeNS = function (ns, k) { return this.getAttribute(k); };
  P.hasAttribute = function (k) { return String(k).toLowerCase() in this.__r.a; };
  P.toggleAttribute = function (k, force) { var has = this.hasAttribute(k), want = force === undefined ? !has : !!force; if (want && !has) this.setAttribute(k, ''); if (!want && has) this.removeAttribute(k); return want; };
  P.matches = function (s) { return matchesSelector(this.__r, s); };
  P.closest = function (s) { var r = this.__r; while (r && r.t !== 'root') { if (matchesSelector(r, s)) return wrap(r); r = rec(r.p); } return null; };
  P.querySelector = function (s) { return queryAll(this.__r, s, true)[0] || null; };
  P.querySelectorAll = function (s) { return queryAll(this.__r, s, false); };
  P.getElementsByClassName = function (c) { return queryAll(this.__r, '.' + c.split(/\s+/).join('.'), false); };
  P.getElementsByTagName = function (t) { return queryAll(this.__r, t, false); };
  P.contains = function (o) { var r = unwrap(o); while (r) { if (r === this.__r) return true; r = rec(r.p); } return false; };
  P.appendChild = function (c) { insertRec(this.__r, unwrap(c), null); return c; };
  P.append = function () { for (var i = 0; i < arguments.length; i++) { var a = arguments[i]; this.appendChild(typeof a === 'object' ? a : document.createTextNode(String(a))); } };
  P.prepend = function () { var first = this.firstChild; for (var i = 0; i < arguments.length; i++) { var a = arguments[i]; this.insertBefore(typeof a === 'object' ? a : document.createTextNode(String(a)), first); } };
  P.insertBefore = function (c, ref) { insertRec(this.__r, unwrap(c), ref ? unwrap(ref) : null); return c; };
  P.removeChild = function (c) { removeRec(unwrap(c)); return c; };
  P.remove = function () { removeRec(this.__r); };
  P.replaceChild = function (n, o) { var ro = unwrap(o); insertRec(this.__r, unwrap(n), ro); removeRec(ro); return o; };
  P.replaceWith = function (n) { var p = rec(this.__r.p); if (!p) return; insertRec(p, unwrap(n), this.__r); removeRec(this.__r); };
  P.cloneNode = function (deep) {
    function clone(r) {
      var id = ++provSeq;
      if (r.t === 'text') { newRec(id, 'text', r.x); queueOp({ o: 'createtext', p: id, v: r.x }); return recs.get(id); }
      if (r.t === '#fragment') { var fr = newRec(id, '#fragment'); if (deep) r.k.forEach(function (cid) { var c = rec(cid); if (c) { var cc = clone(c); cc.p = id; fr.k.push(cc.i); } }); return fr; }
      var n = newRec(id, r.t);
      if (r.tpl !== undefined) n.tpl = r.tpl;
      queueOp({ o: 'create', p: id, t: r.t });
      for (var k in r.a) { n.a[k] = r.a[k]; queueOp({ o: 'attr', n: id, k: k, v: r.a[k] }); }
      if (deep) r.k.forEach(function (cid) { var c = rec(cid); if (c) { var cc = clone(c); cc.p = id; n.k.push(cc.i); queueOp({ o: 'append', n: id, c: cc.i, b: -1 }); } });
      return n;
    }
    return wrap(clone(this.__r));
  };
  P.getBoundingClientRect = function () { return { x: 0, y: 0, top: 0, left: 0, right: 0, bottom: 0, width: 0, height: 0 }; };
  P.getClientRects = function () { return []; };
  P.scrollIntoView = function () { };
  P.attachShadow = function () { return this; };
  P.after = function () { var p = rec(this.__r.p); if (!p) return; var nx = sib(this.__r, 1, false); for (var i = 0; i < arguments.length; i++) { var a = arguments[i]; insertRec(p, unwrap(typeof a === 'object' ? a : document.createTextNode(String(a))), nx ? nx.__r : null); } };
  P.before = function () { var p = rec(this.__r.p); if (!p) return; for (var i = 0; i < arguments.length; i++) { var a = arguments[i]; insertRec(p, unwrap(typeof a === 'object' ? a : document.createTextNode(String(a))), this.__r); } };
  P.focus = function () { queueOp({ o: 'focus', n: this.__r.i }); };
  P.blur = function () { queueOp({ o: 'blur', n: this.__r.i }); };
  P.click = function () { queueOp({ o: 'click', n: this.__r.i }); };
  P.addEventListener = function (type, fn, opts) { addL(this.__r, type, fn, opts); };
  P.removeEventListener = function (type, fn) { removeL(this.__r, type, fn); };
  P.dispatchEvent = function (ev) { ev.target = this; fire(this.__r, ev, true); return true; };

  // ---- nasłuchiwanie ----
  var winL = { l: null };
  function addL(r, type, fn, opts) {
    if (typeof fn !== 'function') return;
    if (!r.l) r.l = {};
    (r.l[type] = r.l[type] || []).push({ fn: fn, once: !!(opts && (opts === true ? false : opts.once)) });
  }
  function removeL(r, type, fn) {
    if (!r.l || !r.l[type]) return;
    r.l[type] = r.l[type].filter(function (e) { return e.fn !== fn; });
  }
  function callL(holder, ev) {
    var arr = holder.l && holder.l[ev.type];
    if (!arr) return;
    arr = arr.slice();
    for (var i = 0; i < arr.length; i++) {
      if (arr[i].once) removeL(holder, ev.type, arr[i].fn);
      try { arr[i].fn.call(holder.el || g, ev); } catch (e) { console.error(e); }
      if (ev.__stopImmediate) break;
    }
  }
  var NO_BUBBLE = { mouseenter: 1, mouseleave: 1, focus: 1, blur: 1, load: 1, DOMContentLoaded: 0 };
  function fire(target, ev, bubbles) {
    ev.currentTarget = null;
    var r = target;
    while (r) {
      ev.currentTarget = r.t === 'root' ? document : wrap(r);
      callL(r, ev);
      if (ev.__stop || !bubbles) return;
      r = rec(r.p);
    }
    if (!ev.__stop) { ev.currentTarget = g; callL(winL, ev); }
  }
  function makeEvent(type, d) {
    var ev = {
      type: type, bubbles: !NO_BUBBLE[type], cancelable: true, defaultPrevented: false, timeStamp: g.performance.now(),
      preventDefault: function () { this.defaultPrevented = true; },
      stopPropagation: function () { this.__stop = true; },
      stopImmediatePropagation: function () { this.__stop = true; this.__stopImmediate = true; }
    };
    for (var k in d) {
      if (k === 'type' || k === 'n') continue;
      ev[k] = d[k];
    }
    if ('x' in d) { ev.clientX = d.x; ev.clientY = d.y; ev.pageX = d.x; ev.pageY = d.y; }
    if ('ctrl' in d) { ev.ctrlKey = d.ctrl; ev.shiftKey = d.shift; ev.altKey = d.alt; ev.metaKey = d.meta; }
    return ev;
  }

  // ---- document ----
  var readyState = 'loading';
  var document = {
    get readyState() { return readyState; },
    get documentElement() { var r = rec(0); for (var i = 0; r && i < r.k.length; i++) { var c = rec(r.k[i]); if (c && c.t === 'html') return wrap(c); } return null; },
    get body() { var q = queryAll(rec(0), 'body', true)[0]; return q || null; },
    get head() { var q = queryAll(rec(0), 'head', true)[0]; return q || null; },
    get activeElement() { var r = rec(focusedId); return r ? wrap(r) : document.body; },
    get title() { var t = queryAll(rec(0), 'title', true)[0]; return t ? t.textContent : ''; },
    set title(v) { silver.window.setTitle(v); },
    get nodeType() { return 9; },
    getElementById: function (id) { var found = null; walk(rec(0), function (c) { if (c.a.id === id) { found = wrap(c); return false; } }); return found; },
    querySelector: function (s) { return queryAll(rec(0), s, true)[0] || null; },
    querySelectorAll: function (s) { return queryAll(rec(0), s, false); },
    getElementsByClassName: function (c) { return queryAll(rec(0), '.' + c.split(/\s+/).join('.'), false); },
    getElementsByTagName: function (t) { return queryAll(rec(0), t, false); },
    createElement: function (tag) {
      var id = ++provSeq, t = String(tag).toLowerCase();
      newRec(id, t);
      queueOp({ o: 'create', p: id, t: t });
      return wrap(recs.get(id));
    },
    createElementNS: function (ns, tag) { return document.createElement(String(tag).replace(/^.*:/, '')); },
    createTextNode: function (text) {
      var id = ++provSeq;
      newRec(id, 'text', String(text));
      queueOp({ o: 'createtext', p: id, v: String(text) });
      return wrap(recs.get(id));
    },
    createComment: function (text) {
      var id = ++provSeq;
      newRec(id, '#comment', String(text));
      queueOp({ o: 'create', p: id, t: '#comment' });
      return wrap(recs.get(id));
    },
    createDocumentFragment: function () { var id = ++provSeq; newRec(id, '#fragment'); return wrap(recs.get(id)); },
    importNode: function (n, deep) { return n.cloneNode(!!deep); },
    createEvent: function () { return new g.Event(''); },
    hasFocus: function () { return true; },
    addEventListener: function (type, fn, o) { var r = rec(0); if (r) addL(r, type, fn, o); },
    removeEventListener: function (type, fn) { var r = rec(0); if (r) removeL(r, type, fn); },
    dispatchEvent: function (ev) { var r = rec(0); if (r) fire(r, ev, true); return true; }
  };
  g.document = document;
  g.window = g;
  g.self = g;
  g.globalThis = g;
  g.Element = Element;
  g.HTMLElement = Element;
  g.Node = Element;
  ['Text', 'Comment', 'DocumentFragment', 'HTMLInputElement', 'HTMLTextAreaElement', 'HTMLSelectElement', 'HTMLTemplateElement',
   'HTMLButtonElement', 'HTMLAnchorElement', 'HTMLImageElement', 'HTMLFormElement', 'SVGElement', 'ShadowRoot', 'Document',
   'HTMLMediaElement', 'HTMLVideoElement', 'HTMLAudioElement', 'HTMLLabelElement', 'HTMLOptionElement', 'HTMLDivElement', 'HTMLSpanElement',
   'HTMLCanvasElement', 'HTMLIFrameElement', 'HTMLLIElement', 'HTMLUListElement', 'HTMLBodyElement', 'HTMLHtmlElement', 'HTMLStyleElement',
   'HTMLScriptElement', 'HTMLLinkElement', 'CharacterData', 'DOMTokenList', 'NodeList', 'HTMLCollection'].forEach(function (n) { g[n] = Element; });
  Element.ELEMENT_NODE = 1; Element.TEXT_NODE = 3; Element.COMMENT_NODE = 8; Element.DOCUMENT_NODE = 9; Element.DOCUMENT_FRAGMENT_NODE = 11;
  g.innerWidth = 0;
  g.innerHeight = 0;
  g.devicePixelRatio = 1;
  g.location = { href: 'silver://app', origin: 'silver://app', pathname: '/', search: '', hash: '', protocol: 'silver:' };
  g.navigator = { userAgent: 'Silver/0.2.0', platform: 'silver', language: 'pl-PL', onLine: true };
  g.matchMedia = function (q) {
    var dark = /prefers-color-scheme\s*:\s*dark/.test(q) ? winInfo.dark : /prefers-color-scheme\s*:\s*light/.test(q) ? !winInfo.dark : false;
    return { matches: !!dark, media: q, addEventListener: function () { }, removeEventListener: function () { }, addListener: function () { }, removeListener: function () { } };
  };
  g.getComputedStyle = function (el) { return el.style; };
  g.scrollTo = function (x, y) { queueOp({ o: 'scroll', n: 0, v: (typeof x === 'object' ? x.top : y) | 0 }); };
  g.addEventListener = function (type, fn, o) { addL(winL, type, fn, o); };
  g.removeEventListener = function (type, fn) { removeL(winL, type, fn); };
  g.dispatchEvent = function (ev) { ev.currentTarget = g; callL(winL, ev); return true; };

  // ---- zdarzenia z H# ----
  internal['__dom_snapshot'] = function (nodes) {
    recs.clear();
    for (var i = 0; i < nodes.length; i++) {
      var n = nodes[i];
      var r = newRec(n.i, n.t, n.x);
      r.p = n.p; r.k = n.k.slice(); r.a = n.a || {};
    }
  };
  internal['__dom_subtree'] = function (p) {
    var nodes = p.nodes;
    for (var i = 0; i < nodes.length; i++) {
      var n = nodes[i];
      var r = recs.get(n.i) || newRec(n.i, n.t, n.x);
      r.t = n.t; r.x = n.x || ''; r.p = n.p; r.k = n.k.slice(); r.a = n.a || {};
    }
  };
  internal['__dom_remap'] = function (p) {
    var map = p.map;
    for (var m = 0; m < map.length; m++) {
      var prov = map[m][0], real = map[m][1];
      var r = recs.get(prov);
      if (!r) continue;
      recs.delete(prov);
      r.i = real;
      recs.set(real, r);
      recs.forEach(function (o) {
        if (o.p === prov) o.p = real;
        for (var j = 0; j < o.k.length; j++) if (o.k[j] === prov) o.k[j] = real;
      });
    }
  };
  internal['__window_info'] = function (p) { winInfo = p; g.innerWidth = p.w; g.innerHeight = p.h; g.devicePixelRatio = (p.scale || 100) / 100; };
  internal['__dom_event'] = function (d) {
    var type = d.type;
    if (type === 'focus') focusedId = d.n;
    if (type === 'blur' && focusedId === d.n) focusedId = -1;
    if (type === 'resize') { g.innerWidth = d.w; g.innerHeight = d.h; g.devicePixelRatio = (d.scale || 100) / 100; }
    if (type === 'DOMContentLoaded') readyState = 'interactive';
    if (type === 'load') readyState = 'complete';
    var r = rec(d.n);
    if (r) {
      if (type === 'input' && d.value !== undefined) r.a.value = d.value;
      if (type === 'change') {
        if (d.checked !== undefined) { if (d.checked) r.a.checked = ''; else delete r.a.checked; }
        if (d.value !== undefined) r.a.value = d.value;
      }
    }
    var ev = makeEvent(type, d);
    if (type === 'DOMContentLoaded' || type === 'load' || type === 'resize') {
      var root = rec(0);
      ev.target = type === 'resize' ? g : document;
      if (root) callL(root, ev);
      ev.currentTarget = g;
      callL(winL, ev);
      return;
    }
    if (!r) { ev.target = g; ev.currentTarget = g; callL(winL, ev); return; }
    ev.target = r.t === 'root' ? document : wrap(r);
    fire(r, ev, ev.bubbles);
  };
})(globalThis);
