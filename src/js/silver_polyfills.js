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
  function abortErr(name, msg) { var e = new Error(msg); e.name = name; e.code = name === 'TimeoutError' ? 23 : 20; return e; }
  if (typeof g.AbortController !== 'function') {
    g.AbortSignal = function AbortSignal() { g.EventTarget.call(this); this.aborted = false; this.reason = undefined; };
    g.AbortSignal.prototype = Object.create(g.EventTarget.prototype);
    g.AbortSignal.prototype.throwIfAborted = function () { if (this.aborted) throw this.reason; };
    g.AbortSignal.abort = function (r) { var s = new g.AbortSignal(); s.aborted = true; s.reason = r === undefined ? abortErr('AbortError', 'This operation was aborted') : r; return s; };
    g.AbortSignal.timeout = function (ms) { var c = new g.AbortController(); setTimeout(function () { c.abort(abortErr('TimeoutError', 'The operation timed out')); }, ms); return c.signal; };
    g.AbortController = function AbortController() { this.signal = new g.AbortSignal(); };
    g.AbortController.prototype.abort = function (reason) {
      if (this.signal.aborted) return;
      this.signal.aborted = true;
      this.signal.reason = reason === undefined ? abortErr('AbortError', 'This operation was aborted') : reason;
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

  // ---- Intl (0.3): podzbiór ECMA-402 — pl/en/de/fr/es/it, bez pełnych danych CLDR ----
  if (typeof g.Intl !== 'object' || typeof g.Intl.NumberFormat !== 'function') {
    var LOC = {
      en: { dec: '.', grp: ',', months: ['January', 'February', 'March', 'April', 'May', 'June', 'July', 'August', 'September', 'October', 'November', 'December'],
        days: ['Sunday', 'Monday', 'Tuesday', 'Wednesday', 'Thursday', 'Friday', 'Saturday'], date: 'M/D/Y', cur: 'before', and: 'and', am: 'AM', pm: 'PM' },
      pl: { dec: ',', grp: '\u00a0', months: ['stycznia', 'lutego', 'marca', 'kwietnia', 'maja', 'czerwca', 'lipca', 'sierpnia', 'września', 'października', 'listopada', 'grudnia'],
        days: ['niedziela', 'poniedziałek', 'wtorek', 'środa', 'czwartek', 'piątek', 'sobota'], date: 'D.M.Y', cur: 'after', and: 'i', am: 'AM', pm: 'PM' },
      de: { dec: ',', grp: '.', months: ['Januar', 'Februar', 'März', 'April', 'Mai', 'Juni', 'Juli', 'August', 'September', 'Oktober', 'November', 'Dezember'],
        days: ['Sonntag', 'Montag', 'Dienstag', 'Mittwoch', 'Donnerstag', 'Freitag', 'Samstag'], date: 'D.M.Y', cur: 'after', and: 'und', am: 'AM', pm: 'PM' },
      fr: { dec: ',', grp: '\u202f', months: ['janvier', 'février', 'mars', 'avril', 'mai', 'juin', 'juillet', 'août', 'septembre', 'octobre', 'novembre', 'décembre'],
        days: ['dimanche', 'lundi', 'mardi', 'mercredi', 'jeudi', 'vendredi', 'samedi'], date: 'D/M/Y', cur: 'after', and: 'et', am: 'AM', pm: 'PM' },
      es: { dec: ',', grp: '.', months: ['enero', 'febrero', 'marzo', 'abril', 'mayo', 'junio', 'julio', 'agosto', 'septiembre', 'octubre', 'noviembre', 'diciembre'],
        days: ['domingo', 'lunes', 'martes', 'miércoles', 'jueves', 'viernes', 'sábado'], date: 'D/M/Y', cur: 'after', and: 'y', am: 'a. m.', pm: 'p. m.' },
      it: { dec: ',', grp: '.', months: ['gennaio', 'febbraio', 'marzo', 'aprile', 'maggio', 'giugno', 'luglio', 'agosto', 'settembre', 'ottobre', 'novembre', 'dicembre'],
        days: ['domenica', 'lunedì', 'martedì', 'mercoledì', 'giovedì', 'venerdì', 'sabato'], date: 'D/M/Y', cur: 'after', and: 'e', am: 'AM', pm: 'PM' }
    };
    var SYMS = { USD: '$', EUR: '€', GBP: '£', PLN: 'zł', JPY: '¥', CHF: 'CHF', CZK: 'Kč', UAH: '₴', CAD: 'CA$', AUD: 'A$' };
    var DEFLOC = 'pl-PL';
    function pickLocale(l) {
      if (Array.isArray(l)) l = l[0];
      l = String(l || DEFLOC);
      var base = l.split('-')[0].toLowerCase();
      return { tag: l, data: LOC[base] || LOC.en };
    }
    function groupInt(s, sep, minGroup) {
      if (s.length < (minGroup || 4)) return s;
      var out = '';
      for (var i = 0; i < s.length; i++) { if (i && (s.length - i) % 3 === 0) out += sep; out += s[i]; }
      return out;
    }
    function NumberFormat(locales, opts) {
      if (!(this instanceof NumberFormat)) return new NumberFormat(locales, opts);
      opts = opts || {};
      var L = pickLocale(locales);
      this._L = L; this._o = {
        style: opts.style || 'decimal', currency: opts.currency, currencyDisplay: opts.currencyDisplay || 'symbol',
        minFD: opts.minimumFractionDigits, maxFD: opts.maximumFractionDigits, useGrouping: opts.useGrouping !== false,
        notation: opts.notation || 'standard', signDisplay: opts.signDisplay || 'auto', unit: opts.unit, minID: opts.minimumIntegerDigits || 1
      };
      var o = this._o;
      var dMin = o.style === 'currency' ? 2 : 0, dMax = o.style === 'currency' ? 2 : (o.style === 'percent' ? 0 : 3);
      if (o.minFD !== undefined && o.maxFD === undefined) dMax = Math.max(dMax, o.minFD);
      if (o.maxFD !== undefined && o.minFD === undefined) dMin = Math.min(dMin, o.maxFD);
      o.minFD = o.minFD === undefined ? dMin : o.minFD; o.maxFD = o.maxFD === undefined ? dMax : o.maxFD;
    }
    NumberFormat.prototype.format = function (n) {
      var o = this._o, d = this._L.data;
      n = Number(n);
      if (isNaN(n)) return 'NaN';
      if (!isFinite(n)) return (n < 0 ? '-' : '') + '∞';
      var neg = n < 0 || (n === 0 && 1 / n < 0 && false);
      var v = Math.abs(n);
      if (o.style === 'percent') v *= 100;
      var suffix = '';
      if (o.notation === 'compact') {
        var units = [[1e12, 'T'], [1e9, 'B'], [1e6, 'M'], [1e3, 'K']];
        for (var u = 0; u < units.length; u++) if (v >= units[u][0]) { v = v / units[u][0]; suffix = units[u][1]; break; }
        if (suffix) { var fd = v < 100 ? 1 : 0; var tmp = Number(v.toFixed(fd)); v = tmp; }
      }
      var fixed = suffix && o.notation === 'compact' ? String(v) : v.toFixed(o.maxFD);
      if (!suffix || o.notation !== 'compact') {
        var parts = fixed.split('.'); var frac = parts[1] || '';
        while (frac.length > o.minFD && frac[frac.length - 1] === '0') frac = frac.slice(0, -1);
        fixed = parts[0] + (frac ? '.' + frac : '');
      }
      var ip = fixed.split('.')[0], fp = fixed.split('.')[1] || '';
      while (ip.length < o.minID) ip = '0' + ip;
      var ipg = o.useGrouping ? groupInt(ip, d.grp, this._L.tag.indexOf('es') === 0 || this._L.tag.indexOf('pl') === 0 ? 5 : 4) : ip;
      var num = ipg + (fp ? d.dec + fp : '') + suffix;
      var sign = neg ? '-' : (o.signDisplay === 'always' || (o.signDisplay === 'exceptZero' && v !== 0) ? '+' : '');
      if (o.style === 'percent') return sign + num + (d.cur === 'after' ? '\u00a0%' : '%');
      if (o.style === 'currency') {
        var code = String(o.currency || 'USD').toUpperCase();
        var sym = o.currencyDisplay === 'code' ? code : (SYMS[code] || code);
        return d.cur === 'before' ? sign + sym + num : sign + num + '\u00a0' + sym;
      }
      if (o.style === 'unit' && o.unit) return sign + num + ' ' + o.unit;
      return sign + num;
    };
    NumberFormat.prototype.formatToParts = function (n) { return [{ type: 'literal', value: this.format(n) }]; };
    NumberFormat.prototype.resolvedOptions = function () {
      return { locale: this._L.tag, numberingSystem: 'latn', style: this._o.style, minimumFractionDigits: this._o.minFD, maximumFractionDigits: this._o.maxFD, useGrouping: this._o.useGrouping };
    };
    NumberFormat.supportedLocalesOf = function (l) { return [].concat(l || []); };

    function pad2(n) { return n < 10 ? '0' + n : String(n); }
    function DateTimeFormat(locales, opts) {
      if (!(this instanceof DateTimeFormat)) return new DateTimeFormat(locales, opts);
      this._L = pickLocale(locales); this._o = opts || {};
    }
    DateTimeFormat.prototype.format = function (date) {
      var o = this._o, d = this._L.data;
      var dt = date === undefined ? new Date() : (date instanceof Date ? date : new Date(date));
      if (isNaN(dt.getTime())) return 'Invalid Date';
      var utc = o.timeZone === 'UTC';
      var Y = utc ? dt.getUTCFullYear() : dt.getFullYear(), Mo = utc ? dt.getUTCMonth() : dt.getMonth(), D = utc ? dt.getUTCDate() : dt.getDate();
      var W = utc ? dt.getUTCDay() : dt.getDay(), H = utc ? dt.getUTCHours() : dt.getHours(), Mi = utc ? dt.getUTCMinutes() : dt.getMinutes(), S = utc ? dt.getUTCSeconds() : dt.getSeconds();
      var hasDate = o.year || o.month || o.day || o.weekday || o.dateStyle, hasTime = o.hour || o.minute || o.second || o.timeStyle;
      if (!hasDate && !hasTime) { o = { year: 'numeric', month: 'numeric', day: 'numeric' }; hasDate = true; }
      var h12 = o.hour12 !== undefined ? o.hour12 : (this._L.tag.indexOf('en') === 0);
      var parts = [];
      var ds = o.dateStyle, ts = o.timeStyle;
      function numeric() { return d.date.replace('D', o.day === '2-digit' || ds === 'short' && d !== LOC.en ? pad2(D) : D).replace('M', o.month === '2-digit' || (d !== LOC.en && ds === 'short') ? pad2(Mo + 1) : Mo + 1).replace('Y', o.year === '2-digit' ? pad2(Y % 100) : Y); }
      if (hasDate) {
        var ds2;
        if (ds === 'full') ds2 = d.days[W] + ', ' + (d === LOC.en ? d.months[Mo] + ' ' + D + ', ' + Y : D + ' ' + d.months[Mo] + ' ' + Y);
        else if (ds === 'long') ds2 = d === LOC.en ? d.months[Mo] + ' ' + D + ', ' + Y : D + ' ' + d.months[Mo] + ' ' + Y;
        else if (ds === 'medium') ds2 = d === LOC.en ? d.months[Mo].slice(0, 3) + ' ' + D + ', ' + Y : D + ' ' + d.months[Mo].slice(0, 3) + ' ' + Y;
        else if (ds === 'short') ds2 = numeric();
        else if (o.month === 'long' || o.month === 'short') {
          var mn = o.month === 'long' ? d.months[Mo] : d.months[Mo].slice(0, 3);
          var segs = [];
          if (d === LOC.en) { segs.push(mn); if (o.day) segs.push(D); ds2 = segs.join(' ') + (o.year ? (o.day ? ', ' : ' ') + Y : ''); }
          else { if (o.day) segs.push(D); segs.push(mn); if (o.year) segs.push(Y); ds2 = segs.join(' '); }
          if (o.weekday) ds2 = (o.weekday === 'long' ? d.days[W] : d.days[W].slice(0, 3)) + ', ' + ds2;
        } else if (o.year || o.month || o.day) {
          var oo = { D: o.day, M: o.month, Y: o.year };
          var fmt = d.date, outp = [];
          fmt.split(/([DMY])/).forEach(function (t) {
            if (t === 'D') { if (oo.D) outp.push(oo.D === '2-digit' ? pad2(D) : D); }
            else if (t === 'M') { if (oo.M) outp.push(oo.M === '2-digit' ? pad2(Mo + 1) : Mo + 1); }
            else if (t === 'Y') { if (oo.Y) outp.push(oo.Y === '2-digit' ? pad2(Y % 100) : Y); }
            else if (t && outp.length && !/^[DMY]$/.test(t)) outp.push('\u0000' + t);
          });
          ds2 = outp.join('').replace(/\u0000/g, '').replace(/^[^\d]+|[^\d]+$/g, '');
          var sepch = fmt.replace(/[DMY]/g, '')[0] || '/';
          ds2 = outp.filter(function (x) { return String(x)[0] !== '\u0000'; }).join(sepch);
          if (o.weekday) ds2 = (o.weekday === 'long' ? d.days[W] : d.days[W].slice(0, 3)) + (ds2 ? ', ' + ds2 : '');
        } else if (o.weekday) ds2 = o.weekday === 'long' ? d.days[W] : d.days[W].slice(0, 3);
        parts.push(ds2);
      }
      if (hasTime) {
        var withSec = o.second || ts === 'medium' || ts === 'long' || ts === 'full';
        var hh = H;
        var suffix = '';
        if (h12) { suffix = ' ' + (H >= 12 ? d.pm : d.am); hh = H % 12 || 12; }
        var hs = (o.hour === '2-digit' || (!h12 && !o.hour === false) || (!h12 && ts)) ? pad2(hh) : String(hh);
        if (!h12 && !o.hour) hs = pad2(hh);
        var t = hs + ':' + pad2(Mi) + (withSec ? ':' + pad2(S) : '') + suffix;
        parts.push(t);
      }
      return parts.join(hasDate && hasTime ? (ds === 'full' || ds === 'long' ? ' at ' : ', ') : '');
    };
    DateTimeFormat.prototype.formatToParts = function (dt) { return [{ type: 'literal', value: this.format(dt) }]; };
    DateTimeFormat.prototype.formatRange = function (a, b) { return this.format(a) + ' – ' + this.format(b); };
    DateTimeFormat.prototype.resolvedOptions = function () { return { locale: this._L.tag, calendar: 'gregory', numberingSystem: 'latn', timeZone: this._o.timeZone || 'UTC' }; };
    DateTimeFormat.supportedLocalesOf = function (l) { return [].concat(l || []); };

    function PluralRules(locales, opts) {
      if (!(this instanceof PluralRules)) return new PluralRules(locales, opts);
      this._lang = pickLocale(locales).tag.split('-')[0].toLowerCase(); this._type = (opts && opts.type) || 'cardinal';
    }
    PluralRules.prototype.select = function (n) {
      n = Math.abs(Number(n));
      var lang = this._lang;
      if (this._type === 'ordinal') {
        if (lang !== 'en') return 'other';
        var m10 = n % 10, m100 = n % 100;
        if (m10 === 1 && m100 !== 11) return 'one';
        if (m10 === 2 && m100 !== 12) return 'two';
        if (m10 === 3 && m100 !== 13) return 'few';
        return 'other';
      }
      if (lang === 'pl') {
        if (n === 1) return 'one';
        var i10 = n % 10, i100 = n % 100;
        if (Number.isInteger(n) && i10 >= 2 && i10 <= 4 && !(i100 >= 12 && i100 <= 14)) return 'few';
        if (Number.isInteger(n)) return 'many';
        return 'other';
      }
      if (lang === 'fr') return n >= 0 && n < 2 ? 'one' : 'other';
      return n === 1 ? 'one' : 'other';
    };
    PluralRules.prototype.resolvedOptions = function () { return { locale: this._lang, type: this._type }; };

    function collKey(s) {
      var map = { 'ą': 'a~', 'ć': 'c~', 'ę': 'e~', 'ł': 'l~', 'ń': 'n~', 'ó': 'o~', 'ś': 's~', 'ź': 'z~', 'ż': 'z~~', 'ä': 'a~', 'ö': 'o~', 'ü': 'u~', 'ß': 'ss' };
      return String(s).toLowerCase().replace(/[ąćęłńóśźżäöüß]/g, function (c) { return map[c]; });
    }
    function Collator(locales, opts) {
      if (!(this instanceof Collator)) return new Collator(locales, opts);
      this._o = opts || {}; this._L = pickLocale(locales);
      var self = this;
      this.compare = function (a, b) {
        a = String(a); b = String(b);
        if (self._o.numeric) {
          var ra = a.split(/(\d+)/), rb = b.split(/(\d+)/);
          for (var i = 0; i < Math.min(ra.length, rb.length); i++) {
            if (i % 2) { var d = parseInt(ra[i], 10) - parseInt(rb[i], 10); if (d) return d < 0 ? -1 : 1; }
            else { var c = self._cmp(ra[i], rb[i]); if (c) return c; }
          }
          return ra.length - rb.length < 0 ? -1 : (ra.length > rb.length ? 1 : 0);
        }
        return self._cmp(a, b);
      };
    }
    Collator.prototype._cmp = function (a, b) {
      var sens = this._o.sensitivity;
      var ka = collKey(a), kb = collKey(b);
      if (sens === 'base') { ka = ka.replace(/~/g, ''); kb = kb.replace(/~/g, ''); }
      if (ka === kb) {
        if (sens === 'base' || sens === 'accent') return 0;
        return a === b ? 0 : (a < b ? 1 : -1);
      }
      return ka < kb ? -1 : 1;
    };
    Collator.prototype.resolvedOptions = function () { return { locale: this._L.tag, usage: 'sort', sensitivity: this._o.sensitivity || 'variant' }; };

    function ListFormat(locales, opts) {
      if (!(this instanceof ListFormat)) return new ListFormat(locales, opts);
      this._L = pickLocale(locales); this._o = opts || {};
    }
    ListFormat.prototype.format = function (list) {
      var a = Array.from(list).map(String), word = this._o.type === 'disjunction' ? (this._L.data === LOC.pl ? 'lub' : 'or') : this._L.data.and;
      if (a.length <= 1) return a.join('');
      if (a.length === 2) return a[0] + ' ' + word + ' ' + a[1];
      var oxford = this._L.data === LOC.en ? ', ' : ' ';
      return a.slice(0, -1).join(', ') + (this._L.data === LOC.en ? ', ' : ' ') + word + ' ' + a[a.length - 1];
    };

    function RelativeTimeFormat(locales, opts) {
      if (!(this instanceof RelativeTimeFormat)) return new RelativeTimeFormat(locales, opts);
      this._L = pickLocale(locales); this._o = opts || {};
    }
    RelativeTimeFormat.prototype.format = function (v, unit) {
      unit = String(unit).replace(/s$/, ''); v = Number(v);
      var pl = this._L.data === LOC.pl, abs = Math.abs(v);
      if (this._o.numeric === 'auto') {
        if (unit === 'day' && v === 0) return pl ? 'dzisiaj' : 'today';
        if (unit === 'day' && v === 1) return pl ? 'jutro' : 'tomorrow';
        if (unit === 'day' && v === -1) return pl ? 'wczoraj' : 'yesterday';
      }
      if (pl) {
        var names = { second: ['sekundę', 'sekundy', 'sekund'], minute: ['minutę', 'minuty', 'minut'], hour: ['godzinę', 'godziny', 'godzin'],
          day: ['dzień', 'dni', 'dni'], week: ['tydzień', 'tygodnie', 'tygodni'], month: ['miesiąc', 'miesiące', 'miesięcy'], year: ['rok', 'lata', 'lat'] };
        var f = names[unit] || [unit, unit, unit];
        var form = abs === 1 ? f[0] : ((abs % 10 >= 2 && abs % 10 <= 4 && !(abs % 100 >= 12 && abs % 100 <= 14)) ? f[1] : f[2]);
        return v < 0 ? abs + ' ' + form + ' temu' : 'za ' + abs + ' ' + form;
      }
      var u = abs === 1 ? unit : unit + 's';
      return v < 0 ? abs + ' ' + u + ' ago' : 'in ' + abs + ' ' + u;
    };

    function Segmenter(locales, opts) { this._g = (opts && opts.granularity) || 'grapheme'; }
    Segmenter.prototype.segment = function (str) {
      str = String(str); var out = [];
      if (this._g === 'word') { var re = /\S+|\s+/g, m; while ((m = re.exec(str))) out.push({ segment: m[0], index: m.index, isWordLike: /\S/.test(m[0]) }); }
      else { var idx = 0; Array.from(str).forEach(function (ch) { out.push({ segment: ch, index: idx }); idx += ch.length; }); }
      return out;
    };

    g.Intl = {
      NumberFormat: NumberFormat, DateTimeFormat: DateTimeFormat, PluralRules: PluralRules, Collator: Collator,
      ListFormat: ListFormat, RelativeTimeFormat: RelativeTimeFormat, Segmenter: Segmenter,
      getCanonicalLocales: function (l) { return [].concat(l || []); },
      supportedValuesOf: function () { return []; }
    };
    Number.prototype.toLocaleString = function (loc, opts) { return new NumberFormat(loc, opts).format(this.valueOf()); };
    Date.prototype.toLocaleDateString = function (loc, opts) {
      var o = Object.assign({}, opts || {});
      if (!o.year && !o.month && !o.day && !o.weekday && !o.dateStyle) { o.year = 'numeric'; o.month = 'numeric'; o.day = 'numeric'; }
      return new DateTimeFormat(loc, o).format(this);
    };
    Date.prototype.toLocaleTimeString = function (loc, opts) {
      var o = Object.assign({}, opts || {});
      if (!o.hour && !o.minute && !o.second && !o.timeStyle) { o.hour = 'numeric'; o.minute = '2-digit'; o.second = '2-digit'; }
      return new DateTimeFormat(loc, o).format(this);
    };
    Date.prototype.toLocaleString = function (loc, opts) {
      var o = Object.assign({}, opts || {});
      if (!o.year && !o.month && !o.day && !o.weekday && !o.dateStyle && !o.hour && !o.minute && !o.second && !o.timeStyle) {
        o.year = 'numeric'; o.month = 'numeric'; o.day = 'numeric'; o.hour = 'numeric'; o.minute = '2-digit'; o.second = '2-digit';
      }
      return new DateTimeFormat(loc, o).format(this);
    };
    var nativeLC = String.prototype.localeCompare;
    String.prototype.localeCompare = function (other, loc, opts) { return new Collator(loc, opts).compare(String(this), String(other)); };
    Array.prototype.toLocaleString = Array.prototype.toLocaleString;
  }
})(globalThis);
