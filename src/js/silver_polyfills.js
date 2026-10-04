(function (g) {
  'use strict';

  // ---- Event / CustomEvent / EventTarget ----
  if (typeof g.Event !== 'function') {
    g.Event = function Event(type, init) {
      init = init || {};
      this.type = String(type);
      this.bubbles = !!init.bubbles;
      this.cancelable = !!init.cancelable;
      this.defaultPrevented = false;
      this.target = null;
      this.currentTarget = null;
      this.timeStamp = Date.now();
    };
    g.Event.prototype.preventDefault = function () { if (this.cancelable) this.defaultPrevented = true; };
    g.Event.prototype.stopPropagation = function () { this.__stop = true; };
    g.Event.prototype.stopImmediatePropagation = function () { this.__stop = true; this.__stopImmediate = true; };
  }
  if (typeof g.CustomEvent !== 'function') {
    g.CustomEvent = function CustomEvent(type, init) {
      g.Event.call(this, type, init);
      this.detail = init && init.detail !== undefined ? init.detail : null;
    };
    g.CustomEvent.prototype = Object.create(g.Event.prototype);
    g.CustomEvent.prototype.constructor = g.CustomEvent;
  }
  if (typeof g.EventTarget !== 'function') {
    g.EventTarget = function EventTarget() { Object.defineProperty(this, '__l', { value: {}, enumerable: false }); };
    g.EventTarget.prototype.addEventListener = function (t, fn, o) {
      if (typeof fn !== 'function' && !(fn && typeof fn.handleEvent === 'function')) return;
      (this.__l[t] = this.__l[t] || []).push({ fn: fn, once: !!(o && typeof o === 'object' && o.once) });
    };
    g.EventTarget.prototype.removeEventListener = function (t, fn) {
      if (this.__l[t]) this.__l[t] = this.__l[t].filter(function (e) { return e.fn !== fn; });
    };
    g.EventTarget.prototype.dispatchEvent = function (ev) {
      ev.target = this; ev.currentTarget = this;
      var arr = (this.__l[ev.type] || []).slice();
      for (var i = 0; i < arr.length; i++) {
        if (arr[i].once) this.removeEventListener(ev.type, arr[i].fn);
        try { typeof arr[i].fn === 'function' ? arr[i].fn.call(this, ev) : arr[i].fn.handleEvent(ev); } catch (e) { console.error(e); }
        if (ev.__stopImmediate) break;
      }
      return !ev.defaultPrevented;
    };
  }

  // ---- AbortController ----
  if (typeof g.AbortController !== 'function') {
    g.AbortSignal = function AbortSignal() { g.EventTarget.call(this); this.aborted = false; this.reason = undefined; };
    g.AbortSignal.prototype = Object.create(g.EventTarget.prototype);
    g.AbortSignal.prototype.throwIfAborted = function () { if (this.aborted) throw this.reason; };
    g.AbortSignal.abort = function (r) { var s = new g.AbortSignal(); s.aborted = true; s.reason = r; return s; };
    g.AbortSignal.timeout = function (ms) { var c = new g.AbortController(); setTimeout(function () { c.abort(new Error('TimeoutError')); }, ms); return c.signal; };
    g.AbortController = function AbortController() { this.signal = new g.AbortSignal(); };
    g.AbortController.prototype.abort = function (reason) {
      if (this.signal.aborted) return;
      this.signal.aborted = true;
      this.signal.reason = reason === undefined ? new Error('AbortError') : reason;
      var ev = new g.Event('abort');
      if (typeof this.signal.onabort === 'function') this.signal.onabort(ev);
      this.signal.dispatchEvent(ev);
    };
  }

  // ---- TextEncoder / TextDecoder (UTF-8) ----
  if (typeof g.TextEncoder !== 'function') {
    g.TextEncoder = function TextEncoder() { this.encoding = 'utf-8'; };
    g.TextEncoder.prototype.encode = function (s) {
      s = String(s === undefined ? '' : s);
      var out = [];
      for (var i = 0; i < s.length; i++) {
        var c = s.codePointAt(i);
        if (c > 0xffff) i++;
        if (c < 0x80) out.push(c);
        else if (c < 0x800) out.push(0xc0 | (c >> 6), 0x80 | (c & 63));
        else if (c < 0x10000) out.push(0xe0 | (c >> 12), 0x80 | ((c >> 6) & 63), 0x80 | (c & 63));
        else out.push(0xf0 | (c >> 18), 0x80 | ((c >> 12) & 63), 0x80 | ((c >> 6) & 63), 0x80 | (c & 63));
      }
      return new Uint8Array(out);
    };
  }
  if (typeof g.TextDecoder !== 'function') {
    g.TextDecoder = function TextDecoder(enc) { this.encoding = (enc || 'utf-8').toLowerCase(); };
    g.TextDecoder.prototype.decode = function (buf) {
      var b = buf instanceof ArrayBuffer ? new Uint8Array(buf) : (buf && buf.buffer ? new Uint8Array(buf.buffer, buf.byteOffset, buf.byteLength) : new Uint8Array(0));
      var s = '', i = 0;
      while (i < b.length) {
        var c = b[i++], cp;
        if (c < 0x80) cp = c;
        else if (c < 0xe0) cp = ((c & 31) << 6) | (b[i++] & 63);
        else if (c < 0xf0) { cp = ((c & 15) << 12) | ((b[i++] & 63) << 6); cp |= b[i++] & 63; }
        else { cp = ((c & 7) << 18) | ((b[i++] & 63) << 12); cp |= (b[i++] & 63) << 6; cp |= b[i++] & 63; }
        s += String.fromCodePoint(cp);
      }
      return s;
    };
  }

  // ---- base64 ----
  var B64 = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';
  if (typeof g.btoa !== 'function') {
    g.btoa = function (s) {
      s = String(s); var o = '';
      for (var i = 0; i < s.length; i += 3) {
        var a = s.charCodeAt(i), b = s.charCodeAt(i + 1), c = s.charCodeAt(i + 2);
        o += B64[a >> 2] + B64[((a & 3) << 4) | ((b || 0) >> 4)] + (isNaN(b) ? '=' : B64[((b & 15) << 2) | ((c || 0) >> 6)]) + (isNaN(c) ? '=' : B64[c & 63]);
      }
      return o;
    };
    g.atob = function (s) {
      s = String(s).replace(/[=\s]+$/g, ''); var o = '', bits = 0, n = 0;
      for (var i = 0; i < s.length; i++) {
        bits = (bits << 6) | B64.indexOf(s[i]); n += 6;
        if (n >= 8) { n -= 8; o += String.fromCharCode((bits >> n) & 255); }
      }
      return o;
    };
  }

  // ---- URL / URLSearchParams ----
  if (typeof g.URLSearchParams !== 'function') {
    g.URLSearchParams = function URLSearchParams(init) {
      this._p = [];
      if (typeof init === 'string') {
        var self = this;
        init.replace(/^\?/, '').split('&').forEach(function (kv) {
          if (!kv) return;
          var i = kv.indexOf('=');
          self._p.push([decodeURIComponent((i < 0 ? kv : kv.slice(0, i)).replace(/\+/g, ' ')), decodeURIComponent(i < 0 ? '' : kv.slice(i + 1).replace(/\+/g, ' '))]);
        });
      } else if (init && typeof init === 'object') {
        for (var k in init) this._p.push([k, String(init[k])]);
      }
    };
    var UP = g.URLSearchParams.prototype;
    UP.get = function (k) { for (var i = 0; i < this._p.length; i++) if (this._p[i][0] === k) return this._p[i][1]; return null; };
    UP.getAll = function (k) { return this._p.filter(function (e) { return e[0] === k; }).map(function (e) { return e[1]; }); };
    UP.has = function (k) { return this.get(k) !== null; };
    UP.set = function (k, v) { this.delete(k); this._p.push([k, String(v)]); };
    UP.append = function (k, v) { this._p.push([k, String(v)]); };
    UP.delete = function (k) { this._p = this._p.filter(function (e) { return e[0] !== k; }); };
    UP.forEach = function (fn) { this._p.forEach(function (e) { fn(e[1], e[0]); }); };
    UP.toString = function () { return this._p.map(function (e) { return encodeURIComponent(e[0]) + '=' + encodeURIComponent(e[1]); }).join('&'); };
  }
  if (typeof g.URL !== 'function') {
    g.URL = function URL(u, base) {
      u = String(u);
      var m = /^([a-zA-Z][a-zA-Z0-9+.-]*:)\/\/([^\/?#]*)([^?#]*)(\?[^#]*)?(#.*)?$/.exec(u);
      if (!m && base) { var b = new g.URL(base); u = b.origin + (u[0] === '/' ? '' : b.pathname.replace(/[^\/]*$/, '')) + u; m = /^([a-zA-Z][a-zA-Z0-9+.-]*:)\/\/([^\/?#]*)([^?#]*)(\?[^#]*)?(#.*)?$/.exec(u); }
      if (!m) throw new TypeError('Invalid URL: ' + u);
      this.protocol = m[1]; this.host = m[2]; this.hostname = m[2].replace(/:\d+$/, '');
      var pm = /:(\d+)$/.exec(m[2]); this.port = pm ? pm[1] : '';
      this.pathname = m[3] || '/'; this.search = m[4] || ''; this.hash = m[5] || '';
      this.origin = this.protocol + '//' + this.host;
      this.searchParams = new g.URLSearchParams(this.search);
      this.href = this.origin + this.pathname + this.search + this.hash;
    };
    g.URL.prototype.toString = function () { return this.href; };
  }

  // ---- różne ----
  if (typeof g.structuredClone !== 'function') g.structuredClone = function (v) { return v === undefined ? v : JSON.parse(JSON.stringify(v)); };
  if (typeof g.process === 'undefined') g.process = { env: { NODE_ENV: 'production' }, platform: 'silver', nextTick: function (f) { Promise.resolve().then(f); }, versions: {} };
  if (typeof g.Blob !== 'function') g.Blob = function Blob(parts, o) { this._s = (parts || []).join(''); this.size = this._s.length; this.type = (o && o.type) || ''; this.text = function () { return Promise.resolve(this._s); }; };

  function Observer() { }
  Observer.prototype.observe = function () { };
  Observer.prototype.unobserve = function () { };
  Observer.prototype.disconnect = function () { };
  Observer.prototype.takeRecords = function () { return []; };
  if (typeof g.MutationObserver !== 'function') g.MutationObserver = function MutationObserver(cb) { this.cb = cb; };
  if (typeof g.ResizeObserver !== 'function') g.ResizeObserver = function ResizeObserver(cb) { this.cb = cb; };
  if (typeof g.IntersectionObserver !== 'function') g.IntersectionObserver = function IntersectionObserver(cb) { this.cb = cb; };
  [g.MutationObserver, g.ResizeObserver, g.IntersectionObserver].forEach(function (C) { C.prototype = Object.create(Observer.prototype); });
})(globalThis);
