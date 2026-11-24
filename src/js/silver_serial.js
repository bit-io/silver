(function (g) {
  'use strict';
  // Silver 0.5 — serializator „structured clone” do tekstu JSON.
  // Używany przez postMessage między głównym kontekstem JS a Workerami (osobne wątki i środowiska QuickJS)
  // oraz przez structuredClone(). Obsługuje: undefined, NaN/±Infinity/-0, BigInt, Date, RegExp, Map, Set,
  // ArrayBuffer, tablice typowane, DataView, Error, obiekty i tablice z cyklami oraz współdzielonymi referencjami.
  // Funkcje i symbole rzucają DataCloneError (jak w przeglądarce). Instancje klas tracą prototyp (jak w przeglądarce).
  //
  // Format: wartości proste są zwykłym JSON-em; każda wartość złożona to tablica JSON z tagiem na początku
  // (["o", {...}], ["a", [...]], ["d", ms], ["r", id] …), więc nie ma niejednoznaczności z danymi użytkownika.

  var B64 = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';
  var B64R = null;

  function bytesToB64(u8) {
    var out = [], i = 0, n = u8.length;
    for (; i + 2 < n; i += 3) {
      var v = (u8[i] << 16) | (u8[i + 1] << 8) | u8[i + 2];
      out.push(B64[v >> 18] + B64[(v >> 12) & 63] + B64[(v >> 6) & 63] + B64[v & 63]);
    }
    if (i < n) {
      var a = u8[i], b = i + 1 < n ? u8[i + 1] : 0, w = (a << 16) | (b << 8);
      out.push(B64[w >> 18] + B64[(w >> 12) & 63] + (i + 1 < n ? B64[(w >> 6) & 63] : '=') + '=');
    }
    return out.join('');
  }

  function b64ToBytes(s) {
    if (!B64R) { B64R = {}; for (var k = 0; k < 64; k++) B64R[B64[k]] = k; }
    var pad = s.length && s[s.length - 1] === '=' ? (s[s.length - 2] === '=' ? 2 : 1) : 0;
    var n = (s.length / 4) * 3 - pad, u8 = new Uint8Array(n), o = 0;
    for (var i = 0; i < s.length; i += 4) {
      var v = (B64R[s[i]] << 18) | (B64R[s[i + 1]] << 12) | ((B64R[s[i + 2]] || 0) << 6) | (B64R[s[i + 3]] || 0);
      if (o < n) u8[o++] = v >> 16;
      if (o < n) u8[o++] = (v >> 8) & 255;
      if (o < n) u8[o++] = v & 255;
    }
    return u8;
  }

  function cloneError(what) {
    var e = new Error(what + ' could not be cloned.');
    e.name = 'DataCloneError';
    e.code = 25;
    return e;
  }

  var TYPED = ['Int8Array', 'Uint8Array', 'Uint8ClampedArray', 'Int16Array', 'Uint16Array', 'Int32Array', 'Uint32Array',
    'Float32Array', 'Float64Array', 'BigInt64Array', 'BigUint64Array'];

  function serialize(value) {
    var seen = new Map(), next = 0;
    function enc(v) {
      var t = typeof v;
      if (v === null) return null;
      if (t === 'string' || t === 'boolean') return v;
      if (t === 'undefined') return ['u'];
      if (t === 'number') {
        if (v !== v) return ['n', 'NaN'];
        if (v === Infinity) return ['n', 'Infinity'];
        if (v === -Infinity) return ['n', '-Infinity'];
        if (v === 0 && 1 / v < 0) return ['n', '-0'];
        return v;
      }
      if (t === 'bigint') return ['b', v.toString()];
      if (t === 'function') throw cloneError(String(v).slice(0, 40));
      if (t === 'symbol') throw cloneError('Symbol');
      if (seen.has(v)) return ['r', seen.get(v)];
      seen.set(v, next++);
      if (Array.isArray(v)) {
        var arr = new Array(v.length);
        for (var i = 0; i < v.length; i++) arr[i] = enc(v[i]);
        return ['a', arr];
      }
      if (v instanceof Date) return ['d', v.getTime()];
      if (v instanceof RegExp) return ['x', v.source, v.flags];
      if (v instanceof Map) {
        var me = [];
        v.forEach(function (val, key) { me.push(enc(key)); me.push(enc(val)); });
        return ['m', me];
      }
      if (v instanceof Set) {
        var se = [];
        v.forEach(function (val) { se.push(enc(val)); });
        return ['s', se];
      }
      if (v instanceof ArrayBuffer) return ['ab', bytesToB64(new Uint8Array(v))];
      if (ArrayBuffer.isView(v)) {
        var name = v instanceof DataView ? 'DataView' : Object.prototype.toString.call(v).slice(8, -1);
        var buf = v.buffer;
        return ['ta', name, v.byteOffset, v instanceof DataView ? v.byteLength : v.length, enc(buf)];
      }
      if (v instanceof Error) {
        return ['e', String(v.name), String(v.message), v.stack === undefined ? null : String(v.stack)];
      }
      if (typeof g.Blob === 'function' && v instanceof g.Blob) throw cloneError('Blob');
      var out = {}, keys = Object.keys(v);
      for (var j = 0; j < keys.length; j++) {
        var val2 = v[keys[j]];
        if (typeof val2 === 'function' || typeof val2 === 'symbol') throw cloneError(typeof val2 === 'symbol' ? 'Symbol' : String(val2).slice(0, 40));
        Object.defineProperty(out, keys[j], { value: enc(val2), enumerable: true, writable: true, configurable: true });
      }
      return ['o', out];
    }
    return JSON.stringify(enc(value));
  }

  function deserialize(text) {
    var table = [];
    function put(o) { table.push(o); return o; }
    function dec(e) {
      if (e === null || typeof e !== 'object') return e;
      switch (e[0]) {
        case 'u': return undefined;
        case 'n': return e[1] === 'NaN' ? NaN : e[1] === 'Infinity' ? Infinity : e[1] === '-Infinity' ? -Infinity : -0;
        case 'b': return BigInt(e[1]);
        case 'r': return table[e[1]];
        case 'a': { var a = put([]); var src = e[1]; for (var i = 0; i < src.length; i++) a.push(dec(src[i])); return a; }
        case 'd': return put(new Date(e[1]));
        case 'x': return put(new RegExp(e[1], e[2]));
        case 'm': { var m = put(new Map()); var me = e[1]; for (var k = 0; k < me.length; k += 2) { var key = dec(me[k]); m.set(key, dec(me[k + 1])); } return m; }
        case 's': { var s = put(new Set()); var se = e[1]; for (var q = 0; q < se.length; q++) s.add(dec(se[q])); return s; }
        case 'ab': return put(b64ToBytes(e[1]).buffer);
        case 'ta': {
          var idx = table.length; table.push(null);
          var buf = dec(e[4]);
          var C = e[1] === 'DataView' ? g.DataView : g[e[1]];
          var view = e[1] === 'DataView' ? new C(buf, e[2], e[3]) : new C(buf, e[2], e[3]);
          table[idx] = view;
          return view;
        }
        case 'e': {
          var Ctor = g[e[1]] && /Error$/.test(e[1]) ? g[e[1]] : Error;
          var err = put(new Ctor(e[2]));
          if (Ctor === Error && e[1] !== 'Error') err.name = e[1];
          if (e[3] !== null) { try { err.stack = e[3]; } catch (x) { } }
          return err;
        }
        case 'o': {
          var o = put({}); var props = e[1], ks = Object.keys(props);
          for (var j = 0; j < ks.length; j++)
            Object.defineProperty(o, ks[j], { value: dec(props[ks[j]]), enumerable: true, writable: true, configurable: true });
          return o;
        }
      }
      return undefined;
    }
    return dec(JSON.parse(text));
  }

  g.__silver_ser = serialize;
  g.__silver_deser = deserialize;
  // structuredClone: prawdziwa semantyka (cykle, Date, Map, Set, bufory…) zamiast kopii przez JSON
  g.structuredClone = function structuredClone(v) { return deserialize(serialize(v)); };
})(globalThis);
