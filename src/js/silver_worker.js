(function (g) {
  'use strict';
  // Silver 0.5 — środowisko globalne Workera. Wykonywane w OSOBNYM wątku z własnym JSRuntime/JSContext
  // (prawdziwa izolacja i równoległość; komunikacja wyłącznie przez postMessage).
  // Natywne funkcje (silver_quickjs_shim.c): __w_post, __w_timer_set/clear, __w_log, __w_read_text,
  // __w_fetch (synchroniczny w obrębie wątku Workera — nie blokuje wątku głównego), __w_close, __w_now, __w_error.

  var base = '';
  var t0 = __w_now();
  var et = new g.EventTarget();

  g.self = g;
  g.window = undefined;
  g.name = '';
  g.onmessage = null; g.onerror = null; g.onmessageerror = null;
  g.addEventListener = function (t, f, o) { et.addEventListener(t, f, o); };
  g.removeEventListener = function (t, f, o) { et.removeEventListener(t, f, o); };
  g.dispatchEvent = function (ev) { return et.dispatchEvent(ev); };
  g.postMessage = function (data) { __w_post(g.__silver_ser(data)); };
  g.close = function () { __w_close(); };
  g.isSecureContext = false;
  g.crossOriginIsolated = false;
  g.navigator = { userAgent: 'Silver/0.5 (Worker)', language: 'pl-PL', languages: ['pl-PL', 'pl', 'en'], hardwareConcurrency: 4, onLine: true };
  g.location = { href: '', origin: 'null', protocol: 'file:', host: '', hostname: '', pathname: '', search: '', hash: '', toString: function () { return g.location.href; } };
  g.performance = { now: function () { return __w_now() - t0; }, timeOrigin: Date.now(), mark: function () { }, measure: function () { } };

  var C = { log: 'log', info: 'info', warn: 'warn', error: 'error', debug: 'debug', trace: 'debug' };
  function fmt(a) {
    return Array.prototype.map.call(a, function (x) {
      if (typeof x === 'string') return x;
      if (x instanceof Error) return x.stack || (x.name + ': ' + x.message);
      try { return typeof x === 'object' && x !== null ? JSON.stringify(x) : String(x); } catch (e) { return String(x); }
    }).join(' ');
  }
  g.console = {};
  Object.keys(C).forEach(function (k) { g.console[k] = function () { __w_log(C[k], fmt(arguments)); }; });
  g.console.assert = function (c) { if (!c) __w_log('error', 'Assertion failed: ' + fmt(Array.prototype.slice.call(arguments, 1))); };
  g.console.table = g.console.dir = g.console.group = g.console.groupEnd = g.console.time = g.console.timeEnd = function () { };

  // ---- timery ----
  var tid = 0, timers = {};
  function addTimer(fn, ms, repeat, args) {
    if (typeof fn !== 'function') return 0;
    var id = ++tid;
    timers[id] = { fn: fn, args: args, repeat: repeat };
    __w_timer_set(id, Math.max(0, +ms || 0), repeat);
    return id;
  }
  g.setTimeout = function (fn, ms) { return addTimer(fn, ms, false, Array.prototype.slice.call(arguments, 2)); };
  g.setInterval = function (fn, ms) { return addTimer(fn, ms, true, Array.prototype.slice.call(arguments, 2)); };
  g.clearTimeout = g.clearInterval = function (id) { if (timers[id]) { delete timers[id]; __w_timer_clear(id); } };
  g.queueMicrotask = function (fn) { Promise.resolve().then(fn); };
  g.requestAnimationFrame = undefined;
  g.__w_fire_timer = function (id) {
    var t = timers[id];
    if (!t) return;
    if (!t.repeat) delete timers[id];
    try { t.fn.apply(g, t.args); } catch (e) { report(e); }
  };

  function report(e) {
    var msg = String(e && e.message !== undefined ? e.message : e);
    var stack = e && e.stack ? String(e.stack) : '';
    var handled = false;
    try {
      var ev = new g.Event('error', { cancelable: true });
      ev.message = msg; ev.error = e; ev.filename = base; ev.lineno = 0; ev.colno = 0;
      if (typeof g.onerror === 'function') { try { g.onerror(ev); } catch (x) { } }
      et.dispatchEvent(ev);
      handled = ev.defaultPrevented;
    } catch (x) { }
    __w_error(msg + (stack ? '\n' + stack : ''), handled ? 1 : 0);
  }
  g.__w_report = report;

  // ---- komunikaty z wątku głównego ----
  g.__w_deliver = function (text) {
    var data;
    try { data = g.__silver_deser(text); }
    catch (e) {
      var me = new g.Event('messageerror'); me.data = null;
      if (typeof g.onmessageerror === 'function') g.onmessageerror(me);
      et.dispatchEvent(me);
      return;
    }
    var ev = new g.Event('message');
    ev.data = data; ev.origin = ''; ev.lastEventId = ''; ev.source = null; ev.ports = [];
    try {
      if (typeof g.onmessage === 'function') g.onmessage(ev);
      et.dispatchEvent(ev);
    } catch (e) { report(e); }
  };

  g.__w_init = function (name, baseUrl) {
    g.name = name || '';
    base = baseUrl || '';
    g.location.href = base;
    var m = /^(https?:)\/\/([^\/?#]*)/i.exec(base);
    if (m) { g.location.protocol = m[1]; g.location.host = m[2]; g.location.hostname = m[2].split(':')[0]; g.location.origin = m[1] + '//' + m[2]; }
  };

  // ---- adresy względne i importScripts ----
  function resolve(u) {
    u = String(u);
    if (/^[a-z][a-z0-9+.-]*:/i.test(u)) return u;
    if (/^https?:\/\//i.test(base)) {
      var o = /^(https?:\/\/[^\/?#]*)(\/[^?#]*)?/i.exec(base);
      if (u.charAt(0) === '/') return o[1] + u;
      var dir = (o[2] || '/').replace(/[^\/]*$/, '');
      return o[1] + normPath(dir + u);
    }
    if (u.charAt(0) === '/') return u;
    var d = base.replace(/^file:\/\//, '').replace(/[^\/]*$/, '');
    return normPath(d + u);
  }
  function normPath(p) {
    var abs = p.charAt(0) === '/', out = [];
    p.split('/').forEach(function (s) {
      if (s === '' || s === '.') return;
      if (s === '..') { if (out.length && out[out.length - 1] !== '..') out.pop(); else if (!abs) out.push('..'); return; }
      out.push(s);
    });
    return (abs ? '/' : '') + out.join('/');
  }
  var indirectEval = eval;
  g.importScripts = function () {
    for (var i = 0; i < arguments.length; i++) {
      var url = resolve(arguments[i]);
      var code = __w_read_text(url);
      if (code === null || code === undefined) {
        var e = new Error("Failed to execute 'importScripts': nie można pobrać " + url);
        e.name = 'NetworkError';
        throw e;
      }
      indirectEval(code + '\n//# sourceURL=' + url);
    }
  };

  // ---- fetch (synchroniczny w wątku Workera, ale zwraca Promise) ----
  function Headers(init) {
    this._m = new Map();
    var self = this;
    if (init && typeof init.forEach === 'function' && !Array.isArray(init)) init.forEach(function (v, k) { self.append(k, v); });
    else if (Array.isArray(init)) init.forEach(function (e) { self.append(e[0], e[1]); });
    else if (init && typeof init === 'object') Object.keys(init).forEach(function (k) { self.append(k, init[k]); });
  }
  Headers.prototype.append = function (k, v) { k = String(k).toLowerCase(); v = String(v); this._m.set(k, this._m.has(k) ? this._m.get(k) + ', ' + v : v); };
  Headers.prototype.set = function (k, v) { this._m.set(String(k).toLowerCase(), String(v)); };
  Headers.prototype.get = function (k) { k = String(k).toLowerCase(); return this._m.has(k) ? this._m.get(k) : null; };
  Headers.prototype.has = function (k) { return this._m.has(String(k).toLowerCase()); };
  Headers.prototype.delete = function (k) { this._m.delete(String(k).toLowerCase()); };
  Headers.prototype.forEach = function (fn, th) { this._m.forEach(function (v, k) { fn.call(th, v, k); }); };
  Headers.prototype.entries = function () { return this._m.entries(); };
  Headers.prototype.keys = function () { return this._m.keys(); };
  Headers.prototype.values = function () { return this._m.values(); };
  Headers.prototype[Symbol.iterator] = Headers.prototype.entries;
  if (typeof g.Headers !== 'function') g.Headers = Headers;

  function bodyBytes(b) {
    if (b == null) return '';
    if (typeof b === 'string') return b;
    if (b instanceof ArrayBuffer) return b;
    if (ArrayBuffer.isView(b)) return b.buffer.slice(b.byteOffset, b.byteOffset + b.byteLength);
    if (typeof g.URLSearchParams === 'function' && b instanceof g.URLSearchParams) return b.toString();
    return String(b);
  }
  function mkResponse(r, url) {
    var used = false, buf = r.body;
    function take() {
      if (used) return Promise.reject(new TypeError('Body already used'));
      used = true; return Promise.resolve(buf);
    }
    var headers = new g.Headers();
    String(r.headers || '').split('\n').forEach(function (l) { var i = l.indexOf(':'); if (i > 0) headers.append(l.slice(0, i), l.slice(i + 1).trim()); });
    return {
      ok: r.status >= 200 && r.status < 300, status: r.status, statusText: r.statusText || '', url: r.url || url, redirected: !!r.redirected,
      type: 'basic', headers: headers,
      get bodyUsed() { return used; },
      arrayBuffer: function () { return take(); },
      text: function () { return take().then(function (b) { return new g.TextDecoder().decode(b); }); },
      json: function () { return take().then(function (b) { return JSON.parse(new g.TextDecoder().decode(b)); }); },
      clone: function () { return mkResponse({ status: r.status, statusText: r.statusText, headers: r.headers, url: r.url, redirected: r.redirected, body: buf.slice(0) }, url); }
    };
  }
  g.fetch = function (input, init) {
    try {
      init = init || {};
      var url = typeof input === 'object' && input !== null && input.url ? input.url : String(input);
      url = resolve(url);
      var method = String(init.method || (input && input.method) || 'GET').toUpperCase();
      var hs = [];
      new g.Headers(init.headers || (input && input.headers)).forEach(function (v, k) { hs.push(k + ': ' + String(v).replace(/[\r\n]+/g, ' ')); });
      var signal = init.signal;
      if (signal && signal.aborted) return Promise.reject(Object.assign(new Error('This operation was aborted'), { name: 'AbortError' }));
      var redirects = init.redirect === 'manual' || init.redirect === 'error' ? 0 : 20;
      var res = __w_fetch(method, url, hs.join('\n'), bodyBytes(init.body !== undefined ? init.body : (input && input.body)), 30000, redirects);
      if (res.error) return Promise.reject(new TypeError('Failed to fetch: ' + res.error));
      if (init.redirect === 'error' && res.status >= 300 && res.status < 400) return Promise.reject(new TypeError('Failed to fetch: redirect'));
      return Promise.resolve(mkResponse(res, url));
    } catch (e) { return Promise.reject(e); }
  };
  g.XMLHttpRequest = undefined;
})(globalThis);
