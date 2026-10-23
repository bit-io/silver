(function (g) {
  'use strict';

  // ------------------------------------------------------------ console
  function fmt(v, depth) {
    depth = depth || 0;
    if (typeof v === 'string') return v;
    if (v === undefined) return 'undefined';
    if (v === null) return 'null';
    if (v instanceof Error) return (v.name + ': ' + v.message) + (v.stack ? '\n' + (g.__silver_format_error ? g.__silver_format_error(String(v.stack)) : v.stack) : '');
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
    if (internal[name]) { try { return internal[name](payload); } catch (e) { console.error(e); } return; }
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
    version: '0.3.0',
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
    if (parent.shadowRec !== undefined && child.t !== '#fragment' && !child.isShadowBox) { lightInsert(parent, child, before); return; }
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
    var wasConnected = isConnectedRec(child);
    var oldParent = rec(child.p);
    detach(child);
    child.p = parent.i;
    var bi = before ? parent.k.indexOf(before.i) : -1;
    var prevSib = null;
    if (bi >= 0) { parent.k.splice(bi, 0, child.i); prevSib = bi > 0 ? rec(parent.k[bi - 1]) : null; }
    else { prevSib = parent.k.length ? rec(parent.k[parent.k.length - 1]) : null; parent.k.push(child.i); }
    queueOp({ o: 'append', n: parent.i, c: child.i, b: bi >= 0 ? before.i : -1 });
    if (oldParent && oldParent !== parent) mutNotify({ type: 'childList', target: oldParent, removed: [child] });
    mutNotify({ type: 'childList', target: parent, added: [child], prev: prevSib, next: before || null });
    if (!wasConnected && isConnectedRec(child)) customConnected(child, true);
  }
  function removeRec(r) {
    var par = rec(r.p);
    var wasConn = isConnectedRec(r);
    if (r.slotHost !== undefined) {
      var hh = rec(r.slotHost);
      if (hh && hh.light) { var li = hh.light.indexOf(r.i); if (li >= 0) hh.light.splice(li, 1); }
      par = hh || par;
      r.slotHost = undefined;
    }
    detach(r);
    queueOp({ o: 'remove', n: r.i });
    if (par) mutNotify({ type: 'childList', target: par, removed: [r] });
    if (wasConn) customConnected(r, false);
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
      if (html[i + 1] === '!' || html[i + 1] === '?') {
        // BUGFIX: `<!>` (kotwice szablonów Svelte 5) i inne "bogus comments" to w HTML węzły komentarza
        // (`<!>` = pusty komentarz). Wcześniej parser je pomijał, więc brakowało węzłów rodzeństwa i
        // Svelte dostawał null z nextSibling (TypeError: cannot read property 'events' of null).
        // Pomijamy tylko deklaracje <!DOCTYPE ...>.
        var de = html.indexOf('>', i);
        var decl = html.slice(i + 2, de < 0 ? n : de);
        if (!(html[i + 1] === '!' && /^doctype/i.test(decl))) top.k.push({ t: '#comment', x: decl });
        i = de < 0 ? n : de + 1; continue;
      }
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
  function Element(r) { if (!r && pendingRec) r = pendingRec; if (!r) throw new TypeError('Illegal constructor'); this.__r = r; r.el = this; }
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
    var oldV = k in r.a ? r.a[k] : null;
    r.a[k] = sv;
    queueOp({ o: 'attr', n: r.i, k: k, v: sv });
    mutNotify({ type: 'attributes', target: r, attributeName: k, oldValue: oldV });
    if (r.customEl && r.customEl.observed[k]) customAttr(r, k, oldV, sv);
  }
  function removeAttr(r, k) {
    k = String(k).toLowerCase();
    var oldV = k in r.a ? r.a[k] : null;
    delete r.a[k];
    queueOp({ o: 'rattr', n: r.i, k: k });
    mutNotify({ type: 'attributes', target: r, attributeName: k, oldValue: oldV });
    if (r.customEl && r.customEl.observed[k]) customAttr(r, k, oldV, null);
  }
  def('nodeType', function () { var t = this.__r.t; return t === 'text' ? 3 : (t === '#comment' ? 8 : (t === '#fragment' ? 11 : 1)); });
  def('nodeName', function () { var t = this.__r.t; return t === 'text' ? '#text' : (t[0] === '#' ? t : t.toUpperCase()); });
  def('tagName', function () { return this.__r.t.toUpperCase(); });
  def('id', function () { return this.__r.a.id || ''; }, function (v) { setAttr(this.__r, 'id', v); });
  def('className', function () { return this.__r.a['class'] || ''; }, function (v) { setAttr(this.__r, 'class', v); });
  def('textContent', function () { return textOf(this.__r); }, function (v) {
    var r = this.__r;
    if (r.t === 'text') { var oldX = r.x; r.x = String(v); queueOp({ o: 'text', n: r.i, v: r.x }); mutNotify({ type: 'characterData', target: r, oldValue: oldX }); return; }
    var oldKids = r.k.map(function (i) { return rec(i); }).filter(Boolean);
    dropSubtree(r);
    if (String(v) !== '') {
      var tr = newRec(++provSeq, 'text', String(v)); tr.p = r.i; r.k.push(tr.i);
    }
    queueOp({ o: 'text', n: r.i, v: String(v) });
    var newKids = r.k.map(function (i) { return rec(i); }).filter(Boolean);
    if (oldKids.length || newKids.length) mutNotify({ type: 'childList', target: r, added: newKids, removed: oldKids });
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
  def('type', function () {
    var r = this.__r, t = r.a.type;
    if (r.t === 'input') return t ? String(t).toLowerCase() : 'text';
    if (r.t === 'button') return t ? String(t).toLowerCase() : 'submit';
    if (r.t === 'textarea') return 'textarea';
    if (r.t === 'select') return 'a' in r && 'multiple' in r.a ? 'select-multiple' : 'select-one';
    return t || '';
  }, function (v) { setAttr(this.__r, 'type', v); });
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
  P.scrollIntoView = function () { queueOp({ o: 'scrollinto', n: this.__r.i }); };
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
  // currentTarget bywa przedefiniowany przez biblioteki (Solid) jako getter bez settera
  function setCT(ev, v) {
    try { ev.currentTarget = v; } catch (e) {
      try { Object.defineProperty(ev, 'currentTarget', { value: v, configurable: true, writable: true }); } catch (e2) { }
    }
  }
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
    // window.event: React/Vue wybierają priorytet aktualizacji na podstawie bieżącego zdarzenia
    var prevEvent = g.event;
    g.event = ev;
    try { fireInner(target, ev, bubbles); } finally { g.event = prevEvent; }
  }
  function fireInner(target, ev, bubbles) {
    setCT(ev, null);
    var r = target;
    while (r) {
      setCT(ev, r.t === 'root' ? document : wrap(r));
      callL(r, ev);
      if (ev.__stop || !bubbles) return;
      r = rec(r.p);
    }
    if (!ev.__stop) { setCT(ev, g); callL(winL, ev); }
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
  var curTitle = '';
  var document = {
    get readyState() { return readyState; },
    get documentElement() { var r = rec(0); for (var i = 0; r && i < r.k.length; i++) { var c = rec(r.k[i]); if (c && c.t === 'html') return wrap(c); } return null; },
    get body() { var q = queryAll(rec(0), 'body', true)[0]; return q || null; },
    get head() { var q = queryAll(rec(0), 'head', true)[0]; return q || null; },
    get activeElement() { var r = rec(focusedId); return r ? wrap(r) : document.body; },
    get title() { var t = queryAll(rec(0), 'title', true)[0]; return t ? t.textContent : curTitle; },
    set title(v) { curTitle = String(v); silver.window.setTitle(curTitle); },
    get defaultView() { return g; },
    get ownerDocument() { return null; },
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
  // BUGFIX: dawniej wszystkie te klasy były aliasem na Element, więc `x instanceof Comment` było prawdziwe dla
  // KAŻDEGO węzła. Svelte 5 (first_child: `node instanceof Comment && node.data === ""`) pomijał przez to
  // pierwszy element szablonu i wszystkie przejścia po rodzeństwie były przesunięte o jeden węzeł
  // (TypeError: cannot read property 'events' of null). Teraz każda klasa ma własne `instanceof`.
  function mkClass(name, test) {
    var C = function () {};
    C.prototype = Element.prototype;   // wspólny prototyp (Svelte czyta deskryptory z Node/Text/Element.prototype)
    try {
      Object.defineProperty(C, Symbol.hasInstance, { value: function (v) { return !!v && typeof v === 'object' && !!test(v); } });
    } catch (e) { /* bez Symbol.hasInstance: zostaje zwykły instanceof po prototypie */ }
    g[name] = C;
  }
  function isTag(tag) { return function (v) { return v.nodeType === 1 && v.tagName === tag; }; }
  mkClass('Text', function (v) { return v.nodeType === 3; });
  mkClass('Comment', function (v) { return v.nodeType === 8; });
  mkClass('CharacterData', function (v) { return v.nodeType === 3 || v.nodeType === 8; });
  mkClass('DocumentFragment', function (v) { return v.nodeType === 11; });
  mkClass('Document', function (v) { return v === document; });
  mkClass('ShadowRoot', function () { return false; });
  mkClass('SVGElement', function () { return false; });
  mkClass('HTMLMediaElement', function (v) { return v.nodeType === 1 && (v.tagName === 'AUDIO' || v.tagName === 'VIDEO'); });
  [['HTMLInputElement', 'INPUT'], ['HTMLTextAreaElement', 'TEXTAREA'], ['HTMLSelectElement', 'SELECT'],
   ['HTMLTemplateElement', 'TEMPLATE'], ['HTMLButtonElement', 'BUTTON'], ['HTMLAnchorElement', 'A'],
   ['HTMLImageElement', 'IMG'], ['HTMLFormElement', 'FORM'], ['HTMLVideoElement', 'VIDEO'], ['HTMLAudioElement', 'AUDIO'],
   ['HTMLLabelElement', 'LABEL'], ['HTMLOptionElement', 'OPTION'], ['HTMLDivElement', 'DIV'], ['HTMLSpanElement', 'SPAN'],
   ['HTMLCanvasElement', 'CANVAS'], ['HTMLIFrameElement', 'IFRAME'], ['HTMLLIElement', 'LI'], ['HTMLUListElement', 'UL'],
   ['HTMLBodyElement', 'BODY'], ['HTMLHtmlElement', 'HTML'], ['HTMLStyleElement', 'STYLE'], ['HTMLScriptElement', 'SCRIPT'],
   ['HTMLLinkElement', 'LINK']].forEach(function (p) { mkClass(p[0], isTag(p[1])); });
  ['DOMTokenList', 'NodeList', 'HTMLCollection'].forEach(function (n) { g[n] = Element; });
  Element.ELEMENT_NODE = 1; Element.TEXT_NODE = 3; Element.COMMENT_NODE = 8; Element.DOCUMENT_NODE = 9; Element.DOCUMENT_FRAGMENT_NODE = 11;
  g.innerWidth = 0;
  g.innerHeight = 0;
  g.devicePixelRatio = 1;
  g.location = { href: 'silver://app', origin: 'silver://app', pathname: '/', search: '', hash: '', protocol: 'silver:' };
  g.navigator = { userAgent: 'Silver/0.3.0', platform: 'silver', language: 'pl-PL', onLine: true };
  g.matchMedia = function (q) {
    var dark = /prefers-color-scheme\s*:\s*dark/.test(q) ? winInfo.dark : /prefers-color-scheme\s*:\s*light/.test(q) ? !winInfo.dark : false;
    return { matches: !!dark, media: q, addEventListener: function () { }, removeEventListener: function () { }, addListener: function () { }, removeListener: function () { } };
  };
  g.getComputedStyle = function (el) { return el.style; };
  g.scrollTo = function (x, y) { queueOp({ o: 'scroll', n: 0, v: (typeof x === 'object' ? x.top : y) | 0 }); };
  g.addEventListener = function (type, fn, o) { addL(winL, type, fn, o); };
  g.removeEventListener = function (type, fn) { removeL(winL, type, fn); };
  g.dispatchEvent = function (ev) { setCT(ev, g); callL(winL, ev); return true; };

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
    if (type === 'scroll' && d.n === 0) { layout.sy = d.y | 0; g.scrollY = g.pageYOffset = layout.sy; runObservers(); }
    if (type === 'DOMContentLoaded' || type === 'load' || type === 'resize') {
      var root = rec(0);
      ev.target = type === 'resize' ? g : document;
      if (root) callL(root, ev);
      setCT(ev, g);
      callL(winL, ev);
      if (type === 'resize') { layout.vw = d.w; layout.vh = d.h; runObservers(); }
      return ev.defaultPrevented ? 1 : 0;
    }
    if (!r) { ev.target = g; setCT(ev, g); callL(winL, ev); return ev.defaultPrevented ? 1 : 0; }
    ev.target = r.t === 'root' ? document : wrap(r);
    fire(r, ev, ev.bubbles);
    // Synchroniczna wymiana: H# odczytuje wynik i pomija domyślną akcję (klik, submit, klawisz…)
    return ev.defaultPrevented ? 1 : 0;
  };

  // =====================================================================
  // 0.3: rozszerzenia — układ (rects), obserwatory, Shadow DOM, Custom Elements, storage,
  //      fetch z nagłówkami, Worker, WebSocket, source mapy
  // =====================================================================


  // ---------- właściwości on* (onclick, oninput, …) ----------
  // Biblioteki (React) wykrywają wsparcie zdarzeń przez `'oninput' in element`.
  ['click', 'dblclick', 'mousedown', 'mouseup', 'mousemove', 'mouseover', 'mouseout', 'mouseenter', 'mouseleave', 'contextmenu',
   'keydown', 'keyup', 'keypress', 'input', 'change', 'submit', 'reset', 'focus', 'blur', 'focusin', 'focusout', 'scroll', 'wheel',
   'select', 'invalid', 'load', 'error', 'abort', 'resize', 'touchstart', 'touchend', 'touchmove', 'touchcancel',
   'pointerdown', 'pointerup', 'pointermove', 'pointerenter', 'pointerleave', 'pointercancel', 'pointerover', 'pointerout',
   'dragstart', 'drag', 'dragend', 'dragenter', 'dragleave', 'dragover', 'drop', 'copy', 'cut', 'paste', 'toggle', 'animationend',
   'animationstart', 'animationiteration', 'transitionend', 'transitionstart', 'compositionstart', 'compositionend', 'compositionupdate',
   'beforeinput', 'selectionchange', 'play', 'pause', 'ended', 'canplay', 'loadeddata', 'timeupdate', 'volumechange'
  ].forEach(function (evName) {
    var prop = 'on' + evName;
    if (Object.getOwnPropertyDescriptor(P, prop)) return;
    Object.defineProperty(P, prop, { configurable: true, enumerable: true,
      get: function () {
        var r0 = this.__r, h = r0.oh;
        if (h && h[evName]) return h[evName];
        // atrybut on<zdarzenie>="kod" kompiluje się do funkcji (wykrywanie wsparcia zdarzeń w React)
        if (typeof r0.a[prop] === 'string') { try { this[prop] = new Function('event', r0.a[prop]); return r0.oh[evName]; } catch (e) { return null; } }
        return null;
      },
      set: function (fn) {
        var r = this.__r;
        r.oh = r.oh || {};
        var first = !r.oh.hasOwnProperty(evName);
        r.oh[evName] = typeof fn === 'function' ? fn : null;
        if (first) {
          var self = this;
          addL(r, evName, function (e) { var f = r.oh[evName]; if (typeof f === 'function') return f.call(self, e); });
        }
      } });
  });
  // document.on* (React: 'oninput' in document)
  ['click', 'input', 'change', 'keydown', 'keyup', 'keypress', 'focus', 'blur', 'submit', 'scroll', 'select', 'selectionchange', 'wheel',
   'mousedown', 'mouseup', 'mousemove', 'pointerdown', 'pointerup', 'touchstart', 'touchend', 'compositionstart', 'compositionend', 'beforeinput'
  ].forEach(function (evName) {
    var prop = 'on' + evName, stored = null;
    Object.defineProperty(document, prop, { configurable: true, enumerable: true,
      get: function () { return stored; },
      set: function (fn) {
        var first = stored === null && !document.__ons;
        document.__ons = document.__ons || {};
        var had = document.__ons[evName];
        document.__ons[evName] = true;
        stored = typeof fn === 'function' ? fn : null;
        if (!had) document.addEventListener(evName, function (e) { if (typeof stored === 'function') return stored.call(document, e); });
      } });
  });

  // ---------- układ z H# (cache ostatniej klatki) ----------
  var layout = { r: new Map(), sy: 0, vw: 0, vh: 0, has: false };
  var layoutWatch = false;
  var computedCache = {};
  var watchedStyle = {};
  function wantLayout() {
    if (!layoutWatch) { layoutWatch = true; queueOp({ o: 'layoutwatch' }); }
  }
  internal['__layout'] = function (p) {
    layout.sy = p.sy | 0; layout.vw = p.vw | 0; layout.vh = p.vh | 0; layout.has = true;
    g.scrollY = g.pageYOffset = layout.sy;
    var m = new Map();
    var arr = p.r || [];
    for (var i = 0; i < arr.length; i++) if (!m.has(arr[i][0])) m.set(arr[i][0], arr[i]);
    layout.r = m;
    computedCache = p.s || {};
    runObservers();
  };
  function lrect(r) { if (!r) return null; wantLayout(); return layout.r.get(r.i) || null; }

  function DOMRectImpl(x, y, w, h) {
    this.x = x; this.y = y; this.width = w; this.height = h;
    this.top = h < 0 ? y + h : y; this.left = w < 0 ? x + w : x;
    this.right = w < 0 ? x : x + w; this.bottom = h < 0 ? y : y + h;
  }
  DOMRectImpl.prototype.toJSON = function () {
    return { x: this.x, y: this.y, width: this.width, height: this.height, top: this.top, right: this.right, bottom: this.bottom, left: this.left };
  };
  if (typeof g.DOMRect !== 'function') {
    g.DOMRect = function DOMRect(x, y, w, h) { return new DOMRectImpl(x || 0, y || 0, w || 0, h || 0); };
    g.DOMRect.fromRect = function (o) { o = o || {}; return new DOMRectImpl(o.x || 0, o.y || 0, o.width || 0, o.height || 0); };
    g.DOMRectReadOnly = g.DOMRect;
  }
  function clientRectOf(r) {
    var t = lrect(r);
    if (!t) return new DOMRectImpl(0, 0, 0, 0);
    return new DOMRectImpl(t[1], t[2] - layout.sy, t[3], t[4]);
  }
  P.getBoundingClientRect = function () { return clientRectOf(this.__r); };
  P.getClientRects = function () { var t = lrect(this.__r); return t ? [clientRectOf(this.__r)] : []; };
  def('ownerDocument', function () { return document; });
  def('offsetWidth', function () { var t = lrect(this.__r); return t ? t[3] : 0; });
  def('offsetHeight', function () { var t = lrect(this.__r); return t ? t[4] : 0; });
  def('offsetLeft', function () {
    var t = lrect(this.__r); if (!t) return 0;
    var pt = lrect(rec(this.__r.p)); return t[1] - (pt ? pt[1] : 0);
  });
  def('offsetTop', function () {
    var t = lrect(this.__r); if (!t) return 0;
    var pt = lrect(rec(this.__r.p)); return t[2] - (pt ? pt[2] : 0);
  });
  def('offsetParent', function () { var p = rec(this.__r.p); return p && p.t !== 'root' ? wrap(p) : null; });
  def('clientWidth', function () { var t = lrect(this.__r); return t ? Math.max(0, t[3] - t[5] - t[6]) : 0; });
  def('clientHeight', function () { var t = lrect(this.__r); return t ? Math.max(0, t[4] - t[7] - t[8]) : 0; });
  def('clientLeft', function () { var t = lrect(this.__r); return t ? t[5] : 0; });
  def('clientTop', function () { var t = lrect(this.__r); return t ? t[7] : 0; });
  def('scrollWidth', function () { return this.clientWidth; });
  def('scrollHeight', function () { var t = lrect(this.__r); return t ? Math.max(t[9] || 0, t[4] - t[7] - t[8]) : 0; });

  // ---------- getComputedStyle ----------
  function kebab(n) { return String(n).replace(/[A-Z]/g, function (c) { return '-' + c.toLowerCase(); }); }
  function camel(n) { return String(n).replace(/-([a-z])/g, function (m, c) { return c.toUpperCase(); }); }
  var STYLE_DEFAULTS = { display: 'block', position: 'static', visibility: 'visible', opacity: '1', 'z-index': 'auto', overflow: 'visible',
    'font-size': '16px', 'font-weight': '400', 'font-style': 'normal', color: 'rgb(0, 0, 0)', 'background-color': 'rgba(0, 0, 0, 0)',
    'text-align': 'start', cursor: 'auto', 'pointer-events': 'auto', transform: 'none', filter: 'none' };
  g.getComputedStyle = function (el) {
    var r = el && el.__r;
    if (r && !watchedStyle[r.i]) { watchedStyle[r.i] = 1; wantLayout(); queueOp({ o: 'watchstyle', n: r.i }); }
    var inline = el && el.style;
    var cs = r ? (computedCache[r.i] || {}) : {};
    function read(name) {
      var k = kebab(name);
      if (k === 'float') k = 'float';
      if (cs[k] !== undefined) return String(cs[k]);
      var t = r ? layout.r.get(r.i) : null;
      if (k === 'width' && t) return t[3] + 'px';
      if (k === 'height' && t) return t[4] + 'px';
      var iv = inline && typeof inline.getPropertyValue === 'function' ? inline.getPropertyValue(k) : '';
      if (iv) return iv;
      return STYLE_DEFAULTS[k] !== undefined ? STYLE_DEFAULTS[k] : '';
    }
    var target = {
      getPropertyValue: function (n) { return read(n); },
      getPropertyPriority: function () { return ''; },
      item: function (i) { return Object.keys(cs)[i] || ''; },
      get length() { return Object.keys(cs).length; },
      get cssText() { return Object.keys(cs).map(function (k) { return k + ': ' + cs[k] + ';'; }).join(' '); }
    };
    return new Proxy(target, {
      get: function (t, prop) {
        if (prop in t) return t[prop];
        if (typeof prop === 'symbol') return undefined;
        return read(prop);
      },
      has: function () { return true; }
    });
  };

  // ---------- MutationObserver ----------
  var mutObservers = [];
  function isDescendantRec(r, anc) {
    var c = r, n = 0;
    while (c && n++ < 1000) { if (c === anc) return true; c = rec(c.p); }
    return false;
  }
  function isConnectedRec(r) {
    var c = r, n = 0;
    while (c && n++ < 1000) { if (c.t === 'root') return true; c = rec(c.p) || (c.slotHost !== undefined ? rec(c.slotHost) : null); }
    return false;
  }
  function mutNotify(m) {
    var host = shadowOwner(m.target);
    if (host) scheduleSlots(host);
    if (!mutObservers.length) return;
    for (var oi = 0; oi < mutObservers.length; oi++) {
      var ob = mutObservers[oi];
      for (var ti = 0; ti < ob.targets.length; ti++) {
        var t = ob.targets[ti], o = t.opts;
        if (!(m.target === t.r || (o.subtree && isDescendantRec(m.target, t.r)))) continue;
        if (m.type === 'attributes') {
          if (!o.attributes) continue;
          if (o.attributeFilter && o.attributeFilter.indexOf(m.attributeName) < 0) continue;
        } else if (m.type === 'characterData') { if (!o.characterData) continue; }
        else if (!o.childList) continue;
        var rc = {
          type: m.type, target: wrap(m.target), addedNodes: (m.added || []).map(wrap), removedNodes: (m.removed || []).map(wrap),
          previousSibling: m.prev ? wrap(m.prev) : null, nextSibling: m.next ? wrap(m.next) : null,
          attributeName: m.attributeName || null, attributeNamespace: null,
          oldValue: (m.type === 'attributes' && o.attributeOldValue) || (m.type === 'characterData' && o.characterDataOldValue) ? (m.oldValue === undefined ? null : m.oldValue) : null
        };
        ob.records.push(rc);
        if (!ob.queued) { ob.queued = true; (function (obs) { Promise.resolve().then(function () { deliverMutations(obs); }); })(ob); }
        break;
      }
    }
  }
  function deliverMutations(ob) {
    ob.queued = false;
    if (!ob.records.length) return;
    var recs2 = ob.records; ob.records = [];
    try { ob.cb.call(ob.wrapper, recs2, ob.wrapper); } catch (e) { console.error(e); }
  }
  g.MutationObserver = function MutationObserver(cb) {
    if (typeof cb !== 'function') throw new TypeError('MutationObserver: callback musi być funkcją');
    this.__o = { cb: cb, targets: [], records: [], queued: false, wrapper: this };
  };
  g.MutationObserver.prototype.observe = function (node, opts) {
    opts = opts || {};
    var o = { childList: !!opts.childList, attributes: !!opts.attributes || !!opts.attributeOldValue || !!opts.attributeFilter,
      characterData: !!opts.characterData || !!opts.characterDataOldValue, subtree: !!opts.subtree,
      attributeOldValue: !!opts.attributeOldValue, characterDataOldValue: !!opts.characterDataOldValue, attributeFilter: opts.attributeFilter || null };
    if (!o.childList && !o.attributes && !o.characterData) throw new TypeError('MutationObserver.observe: podaj childList, attributes lub characterData');
    var ob = this.__o, r = node && node.__r;
    if (!r) return;
    for (var i = 0; i < ob.targets.length; i++) if (ob.targets[i].r === r) { ob.targets[i].opts = o; return; }
    ob.targets.push({ r: r, opts: o });
    if (mutObservers.indexOf(ob) < 0) mutObservers.push(ob);
  };
  g.MutationObserver.prototype.disconnect = function () {
    var ob = this.__o; ob.targets = []; ob.records = [];
    var i = mutObservers.indexOf(ob); if (i >= 0) mutObservers.splice(i, 1);
  };
  g.MutationObserver.prototype.takeRecords = function () { var ob = this.__o, r = ob.records; ob.records = []; return r; };

  // ---------- ResizeObserver / IntersectionObserver ----------
  var roList = [], ioList = [];
  g.ResizeObserver = function ResizeObserver(cb) {
    if (typeof cb !== 'function') throw new TypeError('ResizeObserver: callback musi być funkcją');
    this.__cb = cb; this.__t = []; roList.push(this);
  };
  g.ResizeObserver.prototype.observe = function (el) {
    if (!el || !el.__r) return;
    for (var i = 0; i < this.__t.length; i++) if (this.__t[i].el === el) return;
    wantLayout(); this.__t.push({ el: el, last: null });
  };
  g.ResizeObserver.prototype.unobserve = function (el) { this.__t = this.__t.filter(function (t) { return t.el !== el; }); };
  g.ResizeObserver.prototype.disconnect = function () { this.__t = []; };
  g.IntersectionObserver = function IntersectionObserver(cb, opts) {
    if (typeof cb !== 'function') throw new TypeError('IntersectionObserver: callback musi być funkcją');
    opts = opts || {};
    this.__cb = cb; this.__t = []; this.root = opts.root || null;
    var th = opts.threshold === undefined ? [0] : (Array.isArray(opts.threshold) ? opts.threshold.slice() : [opts.threshold]);
    this.thresholds = th.sort(function (a, b) { return a - b; });
    var mg = String(opts.rootMargin || '0px').split(/\s+/).map(function (v) { return parseFloat(v) || 0; });
    this.__margin = [mg[0] || 0, mg[1] === undefined ? mg[0] || 0 : mg[1], mg[2] === undefined ? mg[0] || 0 : mg[2], mg[3] === undefined ? (mg[1] === undefined ? mg[0] || 0 : mg[1]) : mg[3]];
    this.rootMargin = String(opts.rootMargin || '0px');
    ioList.push(this);
  };
  g.IntersectionObserver.prototype.observe = function (el) {
    if (!el || !el.__r) return;
    for (var i = 0; i < this.__t.length; i++) if (this.__t[i].el === el) return;
    wantLayout(); this.__t.push({ el: el, last: -2, lastIn: null });
  };
  g.IntersectionObserver.prototype.unobserve = function (el) { this.__t = this.__t.filter(function (t) { return t.el !== el; }); };
  g.IntersectionObserver.prototype.disconnect = function () { this.__t = []; };
  g.IntersectionObserver.prototype.takeRecords = function () { return []; };

  function runObservers() {
    if (!layout.has) return;
    var i, j;
    for (i = 0; i < roList.length; i++) {
      var ro = roList[i], entries = [];
      for (j = 0; j < ro.__t.length; j++) {
        var t = ro.__t[j], q = layout.r.get(t.el.__r.i);
        if (!q) continue;
        var w = Math.max(0, q[3] - q[5] - q[6]), h = Math.max(0, q[4] - q[7] - q[8]);
        if (!t.last || t.last.w !== w || t.last.h !== h) {
          t.last = { w: w, h: h };
          entries.push({ target: t.el, contentRect: new DOMRectImpl(q[5], q[7], w, h),
            contentBoxSize: [{ inlineSize: w, blockSize: h }], borderBoxSize: [{ inlineSize: q[3], blockSize: q[4] }],
            devicePixelContentBoxSize: [{ inlineSize: w, blockSize: h }] });
        }
      }
      if (entries.length) { try { ro.__cb.call(ro, entries, ro); } catch (e) { console.error(e); } }
    }
    for (i = 0; i < ioList.length; i++) {
      var io = ioList[i], out = [];
      var rb;
      if (io.root && io.root.__r) { var rr = layout.r.get(io.root.__r.i); rb = rr ? { l: rr[1], t: rr[2] - layout.sy, r: rr[1] + rr[3], b: rr[2] - layout.sy + rr[4] } : null; }
      else rb = { l: 0, t: 0, r: layout.vw, b: layout.vh };
      if (!rb) continue;
      var m = io.__margin;
      var root = { l: rb.l - m[3], t: rb.t - m[0], r: rb.r + m[1], b: rb.b + m[2] };
      for (j = 0; j < io.__t.length; j++) {
        var tg = io.__t[j], tq = layout.r.get(tg.el.__r.i);
        if (!tq) continue;
        var tl = tq[1], tt = tq[2] - layout.sy, tr = tl + tq[3], tb = tt + tq[4];
        var il = Math.max(tl, root.l), it = Math.max(tt, root.t), ir = Math.min(tr, root.r), ib = Math.min(tb, root.b);
        var iw = Math.max(0, ir - il), ih = Math.max(0, ib - it);
        var area = tq[3] * tq[4];
        var ratio = area > 0 ? (iw * ih) / area : (iw >= 0 && ir >= il && ib >= it && tl <= root.r && tr >= root.l && tt <= root.b && tb >= root.t ? 1 : 0);
        var inter = area > 0 ? (iw > 0 && ih > 0) : ratio > 0;
        var idx = -1;
        for (var k = 0; k < io.thresholds.length; k++) if (ratio >= io.thresholds[k] && (ratio > 0 || io.thresholds[k] === 0)) idx = k;
        if (!inter) idx = -1;
        if (tg.last === -2 || tg.lastIn !== inter || (inter && tg.last !== idx)) {
          var first = tg.last === -2;
          tg.last = idx; tg.lastIn = inter;
          if (first && !inter && false) continue;
          out.push({ time: g.performance ? g.performance.now() : Date.now(), target: tg.el, isIntersecting: inter,
            intersectionRatio: ratio, boundingClientRect: new DOMRectImpl(tl, tt, tq[3], tq[4]),
            intersectionRect: new DOMRectImpl(il, it, iw, ih), rootBounds: new DOMRectImpl(root.l, root.t, root.r - root.l, root.b - root.t) });
        }
      }
      if (out.length) { try { io.__cb.call(io, out, io); } catch (e) { console.error(e); } }
    }
  }

  // ---------- Shadow DOM (spłaszczony: slotowanie fizycznie przenosi węzły light DOM) ----------
  var slotTimers = {};
  function shadowOwner(r) {
    var c = r, n = 0;
    while (c && n++ < 200) { if (c.shadowOf !== undefined) return rec(c.shadowOf); c = rec(c.p); }
    return null;
  }
  function scheduleSlots(host) {
    if (!host || slotTimers[host.i]) return;
    slotTimers[host.i] = true;
    Promise.resolve().then(function () { slotTimers[host.i] = false; assignSlots(host); });
  }
  function collectSlots(r, out) {
    for (var i = 0; i < r.k.length; i++) {
      var c = rec(r.k[i]); if (!c) continue;
      if (c.t === 'slot') out.push(c);
      collectSlots(c, out);
    }
    return out;
  }
  function assignSlots(h) {
    var sc = rec(h.shadowRec); if (!sc) return;
    var slots = collectSlots(sc, []);
    var desired = new Map();
    h.light.forEach(function (id) {
      var c = rec(id); if (!c) return;
      var nm = c.t === 'text' || c.t === '#comment' ? '' : (c.a.slot || '');
      var tgt = null;
      for (var i = 0; i < slots.length; i++) if ((slots[i].a.name || '') === nm) { tgt = slots[i]; break; }
      if (tgt) { if (!desired.has(tgt.i)) desired.set(tgt.i, []); desired.get(tgt.i).push(c); }
      else if (c.p !== -1) { detach(c); queueOp({ o: 'remove', n: c.i }); }
    });
    slots.forEach(function (sl) {
      var want = desired.get(sl.i) || [];
      var same = want.length === sl.k.length;
      for (var i = 0; same && i < want.length; i++) if (sl.k[i] !== want[i].i) same = false;
      if (same) return;
      want.forEach(function (c) { var holder = c.slotHost; c.slotHost = undefined; insertRecRaw(sl, c); c.slotHost = holder; });
    });
  }
  function insertRecRaw(parent, child) {
    detach(child);
    child.p = parent.i;
    parent.k.push(child.i);
    queueOp({ o: 'append', n: parent.i, c: child.i, b: -1 });
  }
  function lightInsert(host, child, before) {
    var oldHost = child.slotHost !== undefined ? rec(child.slotHost) : null;
    if (oldHost && oldHost.light) { var oi = oldHost.light.indexOf(child.i); if (oi >= 0) oldHost.light.splice(oi, 1); }
    if (child.p !== -1) { var op = rec(child.p); detach(child); queueOp({ o: 'remove', n: child.i }); if (op) mutNotify({ type: 'childList', target: op, removed: [child] }); }
    var bi = before ? host.light.indexOf(before.i) : -1;
    if (bi >= 0) host.light.splice(bi, 0, child.i); else host.light.push(child.i);
    child.slotHost = host.i;
    assignSlots(host);
    mutNotify({ type: 'childList', target: host, added: [child], next: before || null });
  }
  function lightKids(r) { return r.light.map(function (i) { return rec(i); }).filter(Boolean); }
  [['childNodes', function (r) { return lightKids(r).map(wrap); }],
   ['children', function (r) { return lightKids(r).filter(function (c) { return c.t !== 'text' && c.t !== '#comment'; }).map(wrap); }],
   ['firstChild', function (r) { var k = lightKids(r); return k.length ? wrap(k[0]) : null; }],
   ['lastChild', function (r) { var k = lightKids(r); return k.length ? wrap(k[k.length - 1]) : null; }]
  ].forEach(function (d) {
    var orig = Object.getOwnPropertyDescriptor(P, d[0]);
    Object.defineProperty(P, d[0], { configurable: true, enumerable: true, get: function () {
      return this.__r.light !== undefined && this.__r.shadowRec !== undefined ? d[1](this.__r) : orig.get.call(this);
    } });
  });
  var origParentNode = Object.getOwnPropertyDescriptor(P, 'parentNode');
  Object.defineProperty(P, 'parentNode', { configurable: true, enumerable: true, get: function () {
    if (this.__r.slotHost !== undefined) return wrap(rec(this.__r.slotHost));
    return origParentNode.get.call(this);
  } });
  var origParentEl = Object.getOwnPropertyDescriptor(P, 'parentElement');
  Object.defineProperty(P, 'parentElement', { configurable: true, enumerable: true, get: function () {
    if (this.__r.slotHost !== undefined) return wrap(rec(this.__r.slotHost));
    return origParentEl.get.call(this);
  } });
  P.attachShadow = function (init) {
    var h = this.__r;
    if (h.shadowRec !== undefined) throw new Error('NotSupportedError: element już ma shadow root');
    init = init || {};
    var el = document.createElement('div');
    var c = el.__r;
    c.isShadowBox = true;
    c.shadowOf = h.i;
    setAttr(c, 'data-silver-shadow', '');
    // dotychczasowe dzieci stają się light DOM
    var existing = h.k.slice();
    h.light = existing.slice();
    existing.forEach(function (id) { var cc = rec(id); if (cc) { cc.slotHost = h.i; detach(cc); queueOp({ o: 'remove', n: cc.i }); } });
    h.shadowRec = c.i;
    var before = h.k.length ? rec(h.k[0]) : null;
    insertRecRaw(h, c);
    var root = el;
    Object.defineProperty(root, 'host', { value: wrap(h), configurable: true });
    Object.defineProperty(root, 'mode', { value: init.mode === 'closed' ? 'closed' : 'open', configurable: true });
    Object.defineProperty(root, 'nodeType', { value: 11, configurable: true });
    Object.defineProperty(root, 'nodeName', { value: '#document-fragment', configurable: true });
    root.adoptedStyleSheets = [];
    root.__isShadowRoot = true;
    h.shadowEl = root;
    if (init.mode !== 'closed') Object.defineProperty(this, 'shadowRoot', { get: function () { return root; }, configurable: true });
    return root;
  };
  if (!Object.getOwnPropertyDescriptor(P, 'shadowRoot')) def('shadowRoot', function () { var r = this.__r; return r.shadowEl && r.shadowEl.mode === 'open' ? r.shadowEl : null; });
  P.assignedNodes = function () { return this.__r.k.map(function (i) { return wrap(rec(i)); }).filter(function (n) { return n && n.__r.slotHost !== undefined; }); };
  P.assignedElements = function () { return this.assignedNodes().filter(function (n) { return n.nodeType === 1; }); };
  g.ShadowRoot = function ShadowRoot() { throw new TypeError('Illegal constructor'); };
  Object.defineProperty(g.ShadowRoot, Symbol.hasInstance, { value: function (v) { return !!(v && v.__isShadowRoot); } });

  // ---------- Custom Elements ----------
  var ceRegistry = {};
  var pendingRec = null;
  var ceWaiters = {};
  function customAttr(r, k, oldV, newV) {
    var ce = r.customEl;
    if (ce && typeof r.el.attributeChangedCallback === 'function') {
      try { r.el.attributeChangedCallback(k, oldV, newV); } catch (e) { console.error(e); }
    }
  }
  function customConnected(r, connected) {
    var stack = [r];
    while (stack.length) {
      var c = stack.pop();
      if (c.customEl && c.el) {
        var fn = connected ? c.el.connectedCallback : c.el.disconnectedCallback;
        if (typeof fn === 'function') { try { fn.call(c.el); } catch (e) { console.error(e); } }
      }
      for (var i = 0; i < c.k.length; i++) { var kk = rec(c.k[i]); if (kk) stack.push(kk); }
      if (c.light) c.light.forEach(function (id) { var kk = rec(id); if (kk && kk.p === -1) stack.push(kk); });
    }
  }
  function constructCustom(def, r) {
    pendingRec = r;
    var inst;
    try { inst = Reflect.construct(def.ctor, [], def.ctor); } finally { pendingRec = null; }
    r.el = inst; r.customEl = { observed: def.observed };
    return inst;
  }
  g.customElements = {
    define: function (name, ctor) {
      name = String(name).toLowerCase();
      if (ceRegistry[name]) throw new Error('NotSupportedError: element "' + name + '" jest już zdefiniowany');
      if (name.indexOf('-') < 0) throw new Error('SyntaxError: nazwa custom elementu musi zawierać "-"');
      var obs = {};
      (ctor.observedAttributes || []).forEach(function (a) { obs[String(a).toLowerCase()] = 1; });
      var def = { ctor: ctor, observed: obs };
      ceRegistry[name] = def;
      // upgrade istniejących elementów
      recs.forEach(function (r) {
        if (r.t === name && !r.customEl) {
          var old = r.el;
          constructCustom(def, r);
          if (old) { /* referencje do starego wrappera pozostają ważne dla kodu, który go trzyma */ }
          for (var k in r.a) customAttr(r, k, null, r.a[k]);
          if (isConnectedRec(r)) customConnected(r, true);
        }
      });
      if (ceWaiters[name]) { ceWaiters[name].forEach(function (f) { f(ctor); }); delete ceWaiters[name]; }
    },
    get: function (name) { var d = ceRegistry[String(name).toLowerCase()]; return d ? d.ctor : undefined; },
    whenDefined: function (name) {
      name = String(name).toLowerCase();
      if (ceRegistry[name]) return Promise.resolve(ceRegistry[name].ctor);
      return new Promise(function (res) { (ceWaiters[name] = ceWaiters[name] || []).push(res); });
    },
    upgrade: function () { }
  };
  var origCreate = document.createElement;
  document.createElement = function (tag) {
    var t = String(tag).toLowerCase();
    var def = ceRegistry[t];
    if (!def) return origCreate.call(document, tag);
    var id = ++provSeq;
    var r = newRec(id, t);
    queueOp({ o: 'create', p: id, t: t });
    return constructCustom(def, r);
  };

  // ---------- localStorage / sessionStorage ----------
  var persistStore = new Map();
  var sessionStore = new Map();
  function makeStorage(map, persist) {
    var api = {
      getItem: function (k) { k = String(k); return map.has(k) ? map.get(k) : null; },
      setItem: function (k, v) {
        k = String(k); v = String(v);
        map.set(k, v);
        if (persist) invoke('__storage', { op: 'set', k: k, v: v }).catch(function () { });
      },
      removeItem: function (k) { k = String(k); map.delete(k); if (persist) invoke('__storage', { op: 'remove', k: k }).catch(function () { }); },
      clear: function () { map.clear(); if (persist) invoke('__storage', { op: 'clear' }).catch(function () { }); },
      key: function (i) { return Array.from(map.keys())[i] || null; }
    };
    return new Proxy(api, {
      get: function (t, p) {
        if (p === 'length') return map.size;
        if (typeof p === 'string' && p in t) return t[p];
        if (typeof p === 'string' && map.has(p)) return map.get(p);
        return undefined;
      },
      set: function (t, p, v) { t.setItem(p, v); return true; },
      deleteProperty: function (t, p) { t.removeItem(p); return true; },
      has: function (t, p) { return p in t || map.has(p); },
      ownKeys: function () { return Array.from(map.keys()); },
      getOwnPropertyDescriptor: function (t, p) { return map.has(p) ? { value: map.get(p), enumerable: true, configurable: true, writable: true } : undefined; }
    });
  }
  g.localStorage = makeStorage(persistStore, true);
  g.sessionStorage = makeStorage(sessionStore, false);
  internal['__storage_init'] = function (p) {
    var loc = p && p.local ? p.local : {};
    for (var k in loc) if (!persistStore.has(k)) persistStore.set(k, String(loc[k]));
  };

  // ---------- Headers / Request / Response / fetch ----------
  if (typeof g.Headers !== 'function') {
    g.Headers = function Headers(init) {
      this._m = new Map();
      var self = this;
      if (init instanceof g.Headers) init.forEach(function (v, k) { self.append(k, v); });
      else if (Array.isArray(init)) init.forEach(function (e) { self.append(e[0], e[1]); });
      else if (init && typeof init === 'object') Object.keys(init).forEach(function (k) { self.append(k, init[k]); });
    };
    var HP = g.Headers.prototype;
    HP.append = function (k, v) { k = String(k).toLowerCase(); v = String(v); this._m.set(k, this._m.has(k) ? this._m.get(k) + ', ' + v : v); };
    HP.set = function (k, v) { this._m.set(String(k).toLowerCase(), String(v)); };
    HP.get = function (k) { k = String(k).toLowerCase(); return this._m.has(k) ? this._m.get(k) : null; };
    HP.has = function (k) { return this._m.has(String(k).toLowerCase()); };
    HP.delete = function (k) { this._m.delete(String(k).toLowerCase()); };
    HP.forEach = function (fn, th) { this._m.forEach(function (v, k) { fn.call(th, v, k); }); };
    HP.keys = function () { return this._m.keys(); };
    HP.values = function () { return this._m.values(); };
    HP.entries = function () { return this._m.entries(); };
    HP[Symbol.iterator] = HP.entries;
  }
  var blobUrls = {}, blobSeq = 0;
  if (typeof g.URL === 'function' && typeof g.URL.createObjectURL !== 'function') {
    g.URL.createObjectURL = function (b) { var u = 'blob:silver/' + (++blobSeq); blobUrls[u] = b; return u; };
    g.URL.revokeObjectURL = function (u) { delete blobUrls[u]; };
  }
  function bodyToString(b) {
    if (b == null) return '';
    if (typeof b === 'string') return b;
    if (typeof g.URLSearchParams === 'function' && b instanceof g.URLSearchParams) return b.toString();
    if (b && typeof b._s === 'string') return b._s;
    if (b instanceof ArrayBuffer) return new g.TextDecoder().decode(b);
    if (b && b.buffer instanceof ArrayBuffer) return new g.TextDecoder().decode(b);
    return String(b);
  }
  function makeResponse(r, url) {
    var headers = new g.Headers(r.headers || {});
    var used = false;
    function take() { if (used) return Promise.reject(new TypeError('Body już został odczytany')); used = true; return Promise.resolve(r.body === undefined ? '' : r.body); }
    var resp = {
      ok: !!r.ok, status: r.status | 0, url: String(url), statusText: r.statusText || (r.ok ? 'OK' : ''), headers: headers, redirected: false, type: 'basic',
      get bodyUsed() { return used; },
      text: function () { return take(); },
      json: function () { return take().then(function (t) { return JSON.parse(t); }); },
      arrayBuffer: function () { return take().then(function (t) { return new g.TextEncoder().encode(t).buffer; }); },
      blob: function () { return take().then(function (t) { return new g.Blob([t], { type: headers.get('content-type') || '' }); }); },
      clone: function () { return makeResponse(r, url); }
    };
    return resp;
  }
  g.Response = function Response(body, init) {
    init = init || {};
    var st = init.status === undefined ? 200 : init.status;
    var hd = {}; new g.Headers(init.headers).forEach(function (v, k) { hd[k] = v; });
    return makeResponse({ ok: st >= 200 && st < 300, status: st, statusText: init.statusText || '', body: bodyToString(body), headers: hd }, '');
  };
  g.Request = function Request(url, init) {
    init = init || {};
    this.url = typeof url === 'object' && url && url.url ? url.url : String(url);
    this.method = String(init.method || (url && url.method) || 'GET').toUpperCase();
    this.headers = new g.Headers(init.headers || (url && url.headers));
    this.body = init.body === undefined ? (url && url.body) : init.body;
    this.signal = init.signal || null;
  };
  g.fetch = function (input, opts) {
    var req = input instanceof g.Request ? input : null;
    var url = req ? req.url : (typeof input === 'object' && input && input.href ? input.href : String(input));
    opts = opts || {};
    var method = String(opts.method || (req && req.method) || 'GET');
    var headers = {};
    new g.Headers(opts.headers || (req && req.headers)).forEach(function (v, k) { headers[k] = v; });
    var body = bodyToString(opts.body !== undefined ? opts.body : (req && req.body));
    var signal = opts.signal || (req && req.signal);
    if (signal && signal.aborted) return Promise.reject(signal.reason !== undefined ? signal.reason : Object.assign(new Error('This operation was aborted'), { name: 'AbortError' }));
    if (blobUrls[url]) {
      var bl = blobUrls[url];
      return Promise.resolve(makeResponse({ ok: true, status: 200, body: bodyToString(bl), headers: { 'content-type': bl.type || '' } }, url));
    }
    var p = invoke('__fetch', { url: url, method: method, body: body, headers: headers })
      .then(function (r) { return makeResponse(r, url); });
    if (!signal) return p;
    return new Promise(function (resolve, reject) {
      signal.addEventListener('abort', function () { reject(signal.reason !== undefined ? signal.reason : Object.assign(new Error('This operation was aborted'), { name: 'AbortError' })); });
      p.then(resolve, reject);
    });
  };

  // ---------- Worker (w tym samym wątku, bez izolacji) ----------
  var workerSeq = 0;
  g.Worker = function Worker(url, opts) {
    if (!(this instanceof g.Worker)) throw new TypeError("Worker: użyj 'new'");
    g.EventTarget.call(this);
    var self = this, id = ++workerSeq;
    this.onmessage = null; this.onerror = null; this.onmessageerror = null;
    var dead = false, ready = false, queue = [];
    var inner = new g.EventTarget();
    var scope = Object.create(g);
    scope.onmessage = null; scope.onerror = null;
    scope.self = scope; scope.name = (opts && opts.name) || '';
    scope.postMessage = function (data) {
      var copy = g.structuredClone(data);
      setTimeout(function () {
        if (dead) return;
        var ev = new g.Event('message'); ev.data = copy; ev.target = self;
        if (typeof self.onmessage === 'function') { try { self.onmessage(ev); } catch (e) { console.error(e); } }
        self.dispatchEvent(ev);
      }, 0);
    };
    scope.addEventListener = function (t, f, o) { inner.addEventListener(t, f, o); };
    scope.removeEventListener = function (t, f) { inner.removeEventListener(t, f); };
    scope.close = function () { dead = true; };
    scope.importScripts = function () { throw new Error('importScripts nie jest dostępne (Worker działa w tym samym wątku) — użyj bundla'); };
    function deliver(data) {
      var ev = new g.Event('message'); ev.data = data; ev.target = scope;
      if (typeof scope.onmessage === 'function') { try { scope.onmessage(ev); } catch (e) { reportErr(e); } }
      inner.dispatchEvent(ev);
    }
    function reportErr(e) {
      var ev = new g.Event('error'); ev.message = String(e && e.message || e); ev.error = e;
      if (typeof self.onerror === 'function') self.onerror(ev);
      self.dispatchEvent(ev);
      console.error(e);
    }
    this.postMessage = function (data) {
      if (dead) return;
      var copy = g.structuredClone(data);
      if (!ready) { queue.push(copy); return; }
      setTimeout(function () { if (!dead) deliver(copy); }, 0);
    };
    this.terminate = function () { dead = true; queue = []; };
    g.fetch(String(url)).then(function (r) { return r.text(); }).then(function (code) {
      if (dead) return;
      try { new Function('scope', 'with (scope) {\n' + code + '\n}').call(scope, scope); }
      catch (e) { reportErr(e); return; }
      ready = true;
      queue.splice(0).forEach(function (d) { setTimeout(function () { if (!dead) deliver(d); }, 0); });
    }).catch(reportErr);
  };
  g.Worker.prototype = Object.create(g.EventTarget.prototype);
  g.Worker.prototype.constructor = g.Worker;

  // ---------- WebSocket (natywny klient ws://) ----------
  var wsSeq = 0, wsMap = new Map();
  g.WebSocket = function WebSocket(url, protocols) {
    if (!(this instanceof g.WebSocket)) throw new TypeError("WebSocket: użyj 'new'");
    g.EventTarget.call(this);
    this.url = String(url); this.readyState = 0; this.bufferedAmount = 0; this.extensions = ''; this.protocol = '';
    this.binaryType = 'blob';
    this.onopen = null; this.onmessage = null; this.onerror = null; this.onclose = null;
    this.__id = ++wsSeq;
    wsMap.set(this.__id, this);
    var self = this;
    var ok = typeof __silver_ws_open === 'function' ? __silver_ws_open(this.__id, this.url) : false;
    if (!ok) setTimeout(function () { g.__silver_ws_event(self.__id, 'error', 'nie można otworzyć połączenia'); g.__silver_ws_event(self.__id, 'close', '1006'); }, 0);
  };
  g.WebSocket.prototype = Object.create(g.EventTarget.prototype);
  g.WebSocket.prototype.constructor = g.WebSocket;
  g.WebSocket.CONNECTING = g.WebSocket.prototype.CONNECTING = 0;
  g.WebSocket.OPEN = g.WebSocket.prototype.OPEN = 1;
  g.WebSocket.CLOSING = g.WebSocket.prototype.CLOSING = 2;
  g.WebSocket.CLOSED = g.WebSocket.prototype.CLOSED = 3;
  g.WebSocket.prototype.send = function (data) {
    if (this.readyState !== 1) throw new Error('InvalidStateError: WebSocket nie jest otwarty');
    __silver_ws_send(this.__id, bodyToString(data));
  };
  g.WebSocket.prototype.close = function (code, reason) {
    if (this.readyState >= 2) return;
    this.readyState = 2;
    __silver_ws_close(this.__id, code === undefined ? 1000 : code);
  };
  g.__silver_ws_event = function (id, type, data) {
    var ws = wsMap.get(+id);
    if (!ws) return;
    var ev;
    if (type === 'open') { ws.readyState = 1; ev = new g.Event('open'); ev.target = ws; if (typeof ws.onopen === 'function') ws.onopen(ev); ws.dispatchEvent(ev); }
    else if (type === 'message' || type === 'binary') {
      ev = new g.Event('message'); ev.target = ws;
      ev.data = type === 'binary' ? new g.TextEncoder().encode(data).buffer : data;
      ev.origin = ws.url; ev.lastEventId = ''; ev.ports = [];
      if (typeof ws.onmessage === 'function') ws.onmessage(ev); ws.dispatchEvent(ev);
    } else if (type === 'error') {
      ev = new g.Event('error'); ev.target = ws; ev.message = data;
      if (typeof ws.onerror === 'function') ws.onerror(ev); ws.dispatchEvent(ev);
    } else if (type === 'close') {
      ws.readyState = 3; wsMap.delete(+id);
      ev = new g.Event('close'); ev.target = ws; ev.code = parseInt(data, 10) || 1005; ev.reason = ''; ev.wasClean = ev.code === 1000;
      if (typeof ws.onclose === 'function') ws.onclose(ev); ws.dispatchEvent(ev);
    }
  };

  // ---------- source mapy (TypeScript → czytelne stosy błędów) ----------
  var B64V = {};
  'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/'.split('').forEach(function (c, i) { B64V[c] = i; });
  var smaps = [];
  function decodeMappings(str) {
    var lines = [], segs = [], i = 0, gc = 0, si = 0, sl = 0, sc = 0, ni = 0;
    function vlq() {
      var result = 0, shift = 0, c, digit;
      do { c = str[i++]; digit = B64V[c]; result += (digit & 31) << shift; shift += 5; } while (digit & 32);
      return result & 1 ? -(result >> 1) : result >> 1;
    }
    while (i < str.length) {
      var ch = str[i];
      if (ch === ';') { lines.push(segs); segs = []; gc = 0; i++; continue; }
      if (ch === ',') { i++; continue; }
      gc += vlq();
      if (i >= str.length || str[i] === ',' || str[i] === ';') { segs.push([gc]); continue; }
      si += vlq(); sl += vlq(); sc += vlq();
      var seg = [gc, si, sl, sc];
      if (i < str.length && str[i] !== ',' && str[i] !== ';') { ni += vlq(); seg.push(ni); }
      segs.push(seg);
    }
    lines.push(segs);
    return lines;
  }
  g.__silver_register_map = function (name, payload, isB64) {
    try {
      var json = isB64 === '1' ? (function () {
        var bin = g.atob(payload), bytes = new Uint8Array(bin.length);
        for (var i = 0; i < bin.length; i++) bytes[i] = bin.charCodeAt(i);
        return new g.TextDecoder().decode(bytes);
      })() : payload;
      var m = JSON.parse(json);
      smaps.push({ name: String(name), sources: m.sources || [], names: m.names || [], lines: decodeMappings(m.mappings || ''), root: m.sourceRoot || '' });
    } catch (e) { console.error('silver: błędna source mapa dla ' + name + ': ' + (e && e.message)); }
  };
  function mapPos(file, line, col) {
    for (var i = 0; i < smaps.length; i++) {
      var sm = smaps[i];
      if (!(file === sm.name || file.slice(-sm.name.length) === sm.name || sm.name.slice(-file.length) === file)) continue;
      var segs = sm.lines[line - 1]; if (!segs) return null;
      var best = null;
      for (var j = 0; j < segs.length; j++) { if (segs[j][0] <= col - 1) best = segs[j]; else break; }
      if (!best || best.length < 4) return null;
      return { source: (sm.root ? sm.root.replace(/\/?$/, '/') : '') + sm.sources[best[1]], line: best[2] + 1, col: best[3] + 1, name: best.length > 4 ? sm.names[best[4]] : '' };
    }
    return null;
  }
  g.__silver_format_error = function (stack) {
    if (!smaps.length) return stack;
    return String(stack).replace(/\(?([^\s()]+?):(\d+):(\d+)\)?/g, function (m, file, ln, cl) {
      var p = mapPos(file, +ln, +cl);
      if (!p) return m;
      var txt = p.source + ':' + p.line + ':' + p.col;
      return m[0] === '(' ? '(' + txt + ')' : txt;
    });
  };
  silver.registerSourceMap = function (name, json) { g.__silver_register_map(name, typeof json === 'string' ? json : JSON.stringify(json), '0'); };
  silver.version = '0.3.0';
  g.navigator.userAgent = 'Silver/0.3.0';

})(globalThis);
