(function (g) {
  'use strict';
  // Silver 0.5 — Intl oparty o dane CLDR (wygenerowane z ICU przez tools/gen_intl_data.js).
  // Plik src/js/silver_intl.js jest GENEROWANY z tego szablonu — edytuj szablon, potem uruchom generator.
  // Zakres: NumberFormat, DateTimeFormat (strefy IANA z regułami DST), PluralRules, RelativeTimeFormat, ListFormat,
  // Collator, DisplayNames, Locale, Segmenter, getCanonicalLocales, supportedValuesOf; ~34 wariantów regionalnych.
  var DATA = /*__SILVER_INTL_DATA__*/null;
  if (!DATA) return;
  var DEFLOC = 'pl-PL';
  function PL(x) { return typeof x === 'number' ? DATA.pool[x] : x; }
  var LANGDEF = { en: 'en-US', pl: 'pl-PL', de: 'de-DE', fr: 'fr-FR', es: 'es-ES', it: 'it-IT', pt: 'pt-BR', nl: 'nl-NL', cs: 'cs-CZ', sk: 'sk-SK', uk: 'uk-UA', ru: 'ru-RU',
    sv: 'sv-SE', da: 'da-DK', nb: 'nb-NO', no: 'nb-NO', nn: 'nb-NO', fi: 'fi-FI', tr: 'tr-TR', ja: 'ja-JP', zh: 'zh-CN', ko: 'ko-KR', hu: 'hu-HU', ro: 'ro-RO',
    el: 'el-GR', bg: 'bg-BG', hr: 'hr-HR', lt: 'lt-LT', lv: 'lv-LV', sl: 'sl-SI' };
  var SCRIPT = { ru: 'Cyrl', uk: 'Cyrl', bg: 'Cyrl', sr: 'Cyrl', el: 'Grek', zh: 'Hans', ja: 'Jpan', ko: 'Kore', ar: 'Arab', he: 'Hebr' };
  var EN_GB_REGIONS = { GB: 1, AU: 1, NZ: 1, IE: 1, IN: 1, ZA: 1, SG: 1, HK: 1 };

  function rangeErr(m) { return new RangeError(m); }

  // ---------------------------------------------------------------- lokalizacja
  function canonTag(tag) {
    var s = String(tag);
    if (!/^[A-Za-z]{2,3}([-_][A-Za-z0-9]{2,8})*$/.test(s)) throw rangeErr('Incorrect locale information provided');
    var p = s.replace(/_/g, '-').split('-'), out = [p[0].toLowerCase()], ext = false;
    for (var i = 1; i < p.length; i++) {
      var x = p[i];
      if (x.length === 1) { ext = true; out.push(x.toLowerCase()); }
      else if (ext) out.push(x.toLowerCase());
      else if (x.length === 4 && /^[A-Za-z]+$/.test(x)) out.push(x[0].toUpperCase() + x.slice(1).toLowerCase());
      else if (x.length === 2 && /^[A-Za-z]+$/.test(x) || /^\d{3}$/.test(x)) out.push(x.toUpperCase());
      else out.push(x.toLowerCase());
    }
    return out.join('-');
  }
  function parseTag(tag) {
    var p = canonTag(tag).split('-'), r = { language: p[0], script: undefined, region: undefined, ext: {}, tag: '' }, i = 1;
    if (p[i] && p[i].length === 4 && /^[A-Z]/.test(p[i])) r.script = p[i++];
    if (p[i] && (p[i].length === 2 && /^[A-Z]/.test(p[i]) || /^\d{3}$/.test(p[i]))) r.region = p[i++];
    for (; i < p.length; i++) {
      if (p[i] === 'u') { i++; var key = null; for (; i < p.length && p[i].length > 1 || (key && i < p.length); i++) { if (p[i].length === 2) { key = p[i]; r.ext[key] = 'true'; } else if (key) r.ext[key] = r.ext[key] === 'true' ? p[i] : r.ext[key] + '-' + p[i]; else break; } i--; }
    }
    r.base = r.language + (r.script ? '-' + r.script : '') + (r.region ? '-' + r.region : '');
    r.tag = canonTag(tag);
    return r;
  }
  function dataKey(pt) {
    var l = pt.language, r = pt.region;
    if (r && DATA.loc[l + '-' + r]) return l + '-' + r;
    if (l === 'en' && r && EN_GB_REGIONS[r]) return 'en-GB';
    if (l === 'es' && r && r !== 'ES') return 'es-MX';
    if (l === 'pt' && r && r !== 'BR') return r === 'PT' || r === 'AO' || r === 'MZ' ? 'pt-PT' : 'pt-BR';
    if (l === 'de' && (r === 'AT' || r === 'CH' || r === 'LI')) return r === 'AT' ? 'de-AT' : 'de-CH';
    if (l === 'fr' && r === 'CA') return 'fr-CA';
    if (LANGDEF[l]) return LANGDEF[l];
    return 'en-US';
  }
  function supported(tag) { var l = String(tag).split(/[-_]/)[0].toLowerCase(); return !!LANGDEF[l]; }
  function canonList(locales) {
    if (locales === undefined) return [];
    if (typeof locales === 'string') return [canonTag(locales)];
    if (locales && locales.__isLocale) return [locales.toString()];
    var out = [];
    Array.prototype.forEach.call(locales, function (l) {
      if (typeof l !== 'string' && !(l && l.__isLocale) && !(l && typeof l === 'object')) throw new TypeError('Incorrect locale information provided');
      var t = l && l.__isLocale ? l.toString() : canonTag(l);
      if (out.indexOf(t) < 0) out.push(t);
    });
    return out;
  }
  function resolveLocale(locales) {
    var list = canonList(locales), pick = null;
    for (var i = 0; i < list.length && !pick; i++) if (supported(list[i])) pick = list[i];
    var tag = pick || (list.length ? 'en-US' : DEFLOC);
    var pt = parseTag(tag);
    return { tag: tag.replace(/-u-.*$/, ''), full: tag, pt: pt, key: dataKey(pt), data: DATA.loc[dataKey(pt)], lang: pt.language };
  }
  function getOpt(o, k, allowed, def) {
    var v = o[k];
    if (v === undefined) return def;
    if (typeof v !== 'string' && typeof v !== 'boolean' && typeof v !== 'number') v = String(v);
    v = typeof v === 'string' ? v : String(v);
    if (allowed && allowed.indexOf(v) < 0) throw rangeErr('Value ' + v + ' out of range for Intl options property ' + k);
    return v;
  }
  function getNumOpt(o, k, min, max, def) {
    var v = o[k];
    if (v === undefined) return def;
    v = Number(v);
    if (isNaN(v) || v < min || v > max) throw rangeErr(k + ' value is out of range.');
    return Math.floor(v);
  }
  function toOpts(o) { if (o === undefined) return {}; if (o === null) throw new TypeError('Cannot convert undefined or null to object'); return Object(o); }

  // ---------------------------------------------------------------- reguły liczby mnogiej (CLDR)
  function operands(x) {
    var s = typeof x === 'string' ? x.trim() : String(Math.abs(Number(x)));
    if (/e/i.test(s)) { var nn = Math.abs(Number(s)); s = nn.toFixed(20).replace(/\.?0+$/, ''); }
    s = s.replace(/^[+-]/, '');
    var ip = s.split('.')[0] || '0', fp = s.split('.')[1] || '';
    var t = fp.replace(/0+$/, '');
    return { n: Number(s), i: parseInt(ip, 10), iStr: ip, v: fp.length, f: fp ? parseInt(fp, 10) : 0, t: t ? parseInt(t, 10) : 0, e: 0 };
  }
  function inR(x, a, b) { return x >= a && x <= b; }
  var PLURAL = {
    en: function (o) { return o.i === 1 && o.v === 0 ? 'one' : 'other'; },
    pl: function (o) {
      if (o.v) return 'other';
      var i = o.i, m10 = i % 10, m100 = i % 100;
      if (i === 1) return 'one';
      if (inR(m10, 2, 4) && !inR(m100, 12, 14)) return 'few';
      if ((i !== 1 && inR(m10, 0, 1)) || inR(m10, 5, 9) || inR(m100, 12, 14)) return 'many';
      return 'other';
    },
    cs: function (o) { if (o.v) return 'many'; if (o.i === 1) return 'one'; if (inR(o.i, 2, 4)) return 'few'; return 'other'; },
    ru: function (o) {
      if (o.v) return 'other';
      var m10 = o.i % 10, m100 = o.i % 100;
      if (m10 === 1 && m100 !== 11) return 'one';
      if (inR(m10, 2, 4) && !inR(m100, 12, 14)) return 'few';
      return 'many';
    },
    hr: function (o) {
      var m10 = o.i % 10, m100 = o.i % 100, f10 = o.f % 10, f100 = o.f % 100;
      if ((!o.v && m10 === 1 && m100 !== 11) || (f10 === 1 && f100 !== 11)) return 'one';
      if ((!o.v && inR(m10, 2, 4) && !inR(m100, 12, 14)) || (inR(f10, 2, 4) && !inR(f100, 12, 14))) return 'few';
      return 'other';
    },
    sl: function (o) {
      var m100 = o.i % 100;
      if (!o.v && m100 === 1) return 'one';
      if (!o.v && m100 === 2) return 'two';
      if ((!o.v && inR(m100, 3, 4)) || o.v) return 'few';
      return 'other';
    },
    lt: function (o) {
      var m10 = o.i % 10, m100 = o.i % 100;
      if (o.f) return 'many';
      if (m10 === 1 && !inR(m100, 11, 19)) return 'one';
      if (inR(m10, 2, 9) && !inR(m100, 11, 19)) return 'few';
      return 'other';
    },
    lv: function (o) {
      var m10 = o.i % 10, m100 = o.i % 100, f10 = o.f % 10, f100 = o.f % 100;
      if (m10 === 0 || inR(m100, 11, 19) || (o.v === 2 && inR(f100, 11, 19))) return 'zero';
      if ((m10 === 1 && m100 !== 11) || (o.v === 2 && f10 === 1 && f100 !== 11) || (o.v !== 2 && f10 === 1)) return 'one';
      return 'other';
    },
    ro: function (o) { if (o.i === 1 && !o.v) return 'one'; if (o.v || o.n === 0 || (o.n !== 1 && inR(o.n % 100, 1, 19))) return 'few'; return 'other'; },
    fr: function (o) { if (o.i === 0 || o.i === 1) return 'one'; if (o.i !== 0 && o.i % 1000000 === 0 && !o.v) return 'many'; return 'other'; },
    es: function (o) { if (o.n === 1 && !o.v) return 'one'; if (o.i !== 0 && o.i % 1000000 === 0 && !o.v) return 'many'; return 'other'; },
    it: function (o) { if (o.i === 1 && !o.v) return 'one'; if (o.i !== 0 && o.i % 1000000 === 0 && !o.v) return 'many'; return 'other'; },
    pt: function (o) { if (o.i === 0 || o.i === 1) return 'one'; if (o.i !== 0 && o.i % 1000000 === 0 && !o.v) return 'many'; return 'other'; },
    da: function (o) { return o.n === 1 || (o.t && (o.i === 0 || o.i === 1)) ? 'one' : 'other'; },
    one: function (o) { return o.n === 1 ? 'one' : 'other'; },
    none: function () { return 'other'; }
  };
  var PLURAL_LANG = { en: 'en', de: 'en', nl: 'en', sv: 'en', nb: 'en', no: 'en', nn: 'en', fi: 'en', et: 'en', ca: 'en', pl: 'pl', cs: 'cs', sk: 'cs', ru: 'ru', uk: 'ru', bg: 'one', hr: 'hr', sr: 'hr', bs: 'hr',
    sl: 'sl', lt: 'lt', lv: 'lv', ro: 'ro', fr: 'fr', es: 'es', it: 'it', pt: 'pt', da: 'da', tr: 'one', hu: 'one', el: 'one', ja: 'none', zh: 'none', ko: 'none', vi: 'none', th: 'none', id: 'none' };
  function pluralOf(lang, region, type, x) {
    var o = operands(x), rule = PLURAL[PLURAL_LANG[lang] || 'en'];
    if (lang === 'pt' && region === 'PT') rule = function (q) { return q.i === 1 && !q.v ? 'one' : (q.i !== 0 && q.i % 1000000 === 0 && !q.v ? 'many' : 'other'); };
    if (type === 'ordinal') {
      if (o.v) return 'other';
      var i = o.i, m10 = i % 10, m100 = i % 100;
      if (lang === 'en') return m10 === 1 && m100 !== 11 ? 'one' : m10 === 2 && m100 !== 12 ? 'two' : m10 === 3 && m100 !== 13 ? 'few' : 'other';
      if (lang === 'sv') return (m10 === 1 || m10 === 2) && m100 !== 11 && m100 !== 12 ? 'one' : 'other';
      if (lang === 'fr' || lang === 'ro') return o.n === 1 ? 'one' : 'other';
      if (lang === 'it') return i === 11 || i === 8 || i === 80 || i === 800 ? 'many' : 'other';
      if (lang === 'hu') return i === 1 || i === 5 ? 'one' : 'other';
      if (lang === 'uk') return m10 === 3 && m100 !== 13 ? 'few' : 'other';
      return 'other';
    }
    return rule(o);
  }
  var CATS = { en: ['one', 'other'], pl: ['one', 'few', 'many', 'other'], cs: ['one', 'few', 'many', 'other'], ru: ['one', 'few', 'many', 'other'], hr: ['one', 'few', 'other'], sl: ['one', 'two', 'few', 'other'],
    lt: ['one', 'few', 'many', 'other'], lv: ['zero', 'one', 'other'], ro: ['one', 'few', 'other'], fr: ['one', 'many', 'other'], es: ['one', 'many', 'other'], it: ['one', 'many', 'other'], pt: ['one', 'many', 'other'], da: ['one', 'other'], one: ['one', 'other'], none: ['other'] };

  function PluralRules(locales, options) {
    if (!(this instanceof PluralRules)) throw new TypeError("Constructor Intl.PluralRules requires 'new'");
    var o = toOpts(options);
    this._L = resolveLocale(locales);
    this._type = getOpt(o, 'type', ['cardinal', 'ordinal'], 'cardinal');
    this._nf = new NumberFormat(locales, { minimumIntegerDigits: getNumOpt(o, 'minimumIntegerDigits', 1, 21, 1), minimumFractionDigits: o.minimumFractionDigits, maximumFractionDigits: o.maximumFractionDigits,
      minimumSignificantDigits: o.minimumSignificantDigits, maximumSignificantDigits: o.maximumSignificantDigits, useGrouping: false });
  }
  PluralRules.prototype.select = function (n) {
    var s = this._nf.format(typeof n === 'bigint' ? n : Number(n));
    s = s.replace(this._nf._L.data.n.dec, '.').replace(/[^\d.]/g, '');
    return pluralOf(this._L.lang, this._L.pt.region, this._type, s || '0');
  };
  PluralRules.prototype.selectRange = function (a, b) { return this.select(b); };
  PluralRules.prototype.resolvedOptions = function () {
    var cats = this._type === 'ordinal' ? (this._L.lang === 'en' ? ['few', 'one', 'two', 'other'] : ['other']) : (CATS[PLURAL_LANG[this._L.lang] || 'en']).slice();
    var r = this._nf.resolvedOptions();
    return { locale: this._L.tag, type: this._type, minimumIntegerDigits: r.minimumIntegerDigits, minimumFractionDigits: r.minimumFractionDigits, maximumFractionDigits: r.maximumFractionDigits, pluralCategories: cats, roundingIncrement: 1, roundingMode: 'halfExpand', roundingPriority: 'auto', trailingZeroDisplay: 'auto' };
  };
  PluralRules.supportedLocalesOf = function (l) { return canonList(l).filter(supported); };

  // ---------------------------------------------------------------- arytmetyka dziesiętna na napisach
  function toDec(x) {
    var neg = false, s;
    if (typeof x === 'bigint') { s = x.toString(); }
    else if (typeof x === 'string') {
      s = x.trim();
      if (!/^[+-]?(\d+\.?\d*|\.\d+)(e[+-]?\d+)?$/i.test(s) && !/^[+-]?(Infinity|NaN)$/.test(s)) { var nn = Number(s); if (isNaN(nn) && s !== 'NaN') return { nan: true }; s = String(nn); }
    } else {
      var n = Number(x);
      if (n !== n) return { nan: true };
      if (n === Infinity || n === -Infinity) return { inf: true, neg: n < 0 };
      neg = n < 0 || (n === 0 && 1 / n < 0);
      s = String(Math.abs(n));
    }
    if (/^[+-]?NaN$/.test(s)) return { nan: true };
    if (/^[+-]?Infinity$/.test(s)) return { inf: true, neg: s[0] === '-' };
    if (s[0] === '-') { neg = true; s = s.slice(1); } else if (s[0] === '+') s = s.slice(1);
    var m = /^(\d*)\.?(\d*)(?:e([+-]?\d+))?$/i.exec(s);
    var ip = m[1] || '', fp = m[2] || '', ex = m[3] ? parseInt(m[3], 10) : 0;
    if (ex > 0) { while (fp.length < ex) fp += '0'; ip += fp.slice(0, ex); fp = fp.slice(ex); }
    else if (ex < 0) { var k = -ex; while (ip.length < k) ip = '0' + ip; fp = ip.slice(ip.length - k) + fp; ip = ip.slice(0, ip.length - k); }
    ip = ip.replace(/^0+(?=\d)/, ''); if (ip === '') ip = '0';
    return { neg: neg, i: ip, f: fp };
  }
  function shiftDec(d, k) {          // mnoży przez 10^k
    var all = d.i + d.f, pt = d.i.length + k;
    if (pt <= 0) { all = new Array(1 - pt + 1).join('0') + all; pt = 1; }
    else if (pt > all.length) all += new Array(pt - all.length + 1).join('0');
    var i = all.slice(0, pt).replace(/^0+(?=\d)/, ''), f = all.slice(pt);
    return { neg: d.neg, i: i || '0', f: f };
  }
  function isZeroDec(d) { return /^0*$/.test(d.i + d.f); }
  function roundDec(d, maxFD, mode) {
    mode = mode || 'halfExpand';
    var digs = d.i + d.f, pos = d.i.length + maxFD;            // zostaje pos cyfr
    if (pos >= digs.length) return d;
    var keep, rest;
    if (pos < 0) { keep = ''; rest = new Array(-pos + 1).join('0') + digs; pos = 0; } else { keep = digs.slice(0, pos); rest = digs.slice(pos); }
    var first = rest.charCodeAt(0) - 48, tail = /[1-9]/.test(rest.slice(1));
    var cmp = first > 5 || (first === 5 && tail) ? 1 : (first === 5 ? 0 : -1);   // względem połowy
    var anyRest = first > 0 || tail;
    var lastOdd = keep.length ? (keep.charCodeAt(keep.length - 1) - 48) % 2 === 1 : false;
    var up;
    switch (mode) {
      case 'ceil': up = anyRest && !d.neg; break;
      case 'floor': up = anyRest && d.neg; break;
      case 'expand': up = anyRest; break;
      case 'trunc': up = false; break;
      case 'halfCeil': up = cmp > 0 || (cmp === 0 && !d.neg); break;
      case 'halfFloor': up = cmp > 0 || (cmp === 0 && d.neg); break;
      case 'halfTrunc': up = cmp > 0; break;
      case 'halfEven': up = cmp > 0 || (cmp === 0 && lastOdd); break;
      default: up = cmp >= 0;
    }
    var arr = keep.split('');
    if (up) {
      var j = arr.length - 1;
      while (j >= 0) { if (arr[j] === '9') { arr[j] = '0'; j--; } else { arr[j] = String.fromCharCode(arr[j].charCodeAt(0) + 1); break; } }
      if (j < 0) arr.unshift('1');
    }
    var out = arr.join('');
    var ilen = out.length - maxFD;
    if (maxFD < 0) { out += new Array(-maxFD + 1).join('0'); ilen = out.length; }
    var ip = ilen > 0 ? out.slice(0, ilen) : '0', fp = ilen > 0 ? out.slice(ilen) : new Array(-ilen + 1).join('0') + out;
    ip = ip.replace(/^0+(?=\d)/, '') || '0';
    return { neg: d.neg, i: ip, f: fp.slice(0, Math.max(0, maxFD)) };
  }
  function magnitude(d) {            // wykładnik dziesiętny pierwszej cyfry znaczącej (0 → 0)
    if (d.i !== '0') return d.i.length - 1;
    var m = /[1-9]/.exec(d.f);
    return m ? -(m.index + 1) : 0;
  }
  function roundSig(d, maxSD, mode) {
    if (isZeroDec(d)) return d;
    return roundDec(d, maxSD - 1 - magnitude(d), mode);
  }

  // ---------------------------------------------------------------- NumberFormat
  var CUR_DIGITS = { JPY: 0, KRW: 0, VND: 0, CLP: 0, ISK: 0, HUF: 2, BHD: 3, KWD: 3, OMR: 3, JOD: 3, TND: 3 };
  function currencyDigits(c, L) { if (L.data.n.cdig && L.data.n.cdig[c] !== undefined) return L.data.n.cdig[c]; return CUR_DIGITS[c] !== undefined ? CUR_DIGITS[c] : 2; }
  function NumberFormat(locales, options) {
    if (!(this instanceof NumberFormat)) return new NumberFormat(locales, options);
    var o = toOpts(options);
    var L = this._L = resolveLocale(locales);
    var r = this._r = {};
    r.locale = L.tag; r.numberingSystem = 'latn';
    r.style = getOpt(o, 'style', ['decimal', 'percent', 'currency', 'unit'], 'decimal');
    var cur = getOpt(o, 'currency', null, undefined);
    if (cur !== undefined && !/^[A-Za-z]{3}$/.test(cur)) throw rangeErr('Invalid currency code : ' + cur);
    if (r.style === 'currency' && cur === undefined) throw new TypeError('Currency code is required with currency style.');
    if (r.style === 'currency') { r.currency = cur.toUpperCase(); r.currencyDisplay = getOpt(o, 'currencyDisplay', ['code', 'symbol', 'narrowSymbol', 'name'], 'symbol'); r.currencySign = getOpt(o, 'currencySign', ['standard', 'accounting'], 'standard'); }
    var unit = getOpt(o, 'unit', null, undefined);
    if (r.style === 'unit') { if (unit === undefined) throw new TypeError('Unit is required with unit style.'); r.unit = unit; r.unitDisplay = getOpt(o, 'unitDisplay', ['short', 'narrow', 'long'], 'short'); }
    r.notation = getOpt(o, 'notation', ['standard', 'scientific', 'engineering', 'compact'], 'standard');
    var mnid = getNumOpt(o, 'minimumIntegerDigits', 1, 21, 1);
    var cd = r.style === 'currency' ? currencyDigits(r.currency, L) : 0;
    var mnfdDef = r.style === 'currency' && r.notation === 'standard' ? cd : 0;
    var mxfdDef = r.style === 'currency' && r.notation === 'standard' ? cd : (r.style === 'percent' ? 0 : 3);
    var mnfd = o.minimumFractionDigits, mxfd = o.maximumFractionDigits, mnsd = o.minimumSignificantDigits, mxsd = o.maximumSignificantDigits;
    r.minimumIntegerDigits = mnid;
    if (mnsd !== undefined || mxsd !== undefined) {
      r.minimumSignificantDigits = getNumOpt(o, 'minimumSignificantDigits', 1, 21, 1);
      r.maximumSignificantDigits = getNumOpt(o, 'maximumSignificantDigits', r.minimumSignificantDigits, 21, 21);
      this._sig = true;
    } else if (mnfd !== undefined || mxfd !== undefined) {
      var a = getNumOpt(o, 'minimumFractionDigits', 0, 100, undefined), b = getNumOpt(o, 'maximumFractionDigits', 0, 100, undefined);
      if (a === undefined) a = Math.min(mnfdDef, b);
      else if (b === undefined) b = Math.max(mxfdDef, a);
      else if (a > b) throw rangeErr('maximumFractionDigits value is out of range.');
      r.minimumFractionDigits = a; r.maximumFractionDigits = b;
    } else if (r.notation === 'compact') { this._compactDefault = true; r.minimumFractionDigits = 0; r.maximumFractionDigits = 0; }
    else { r.minimumFractionDigits = mnfdDef; r.maximumFractionDigits = mxfdDef; }
    var ug = o.useGrouping;
    r.useGrouping = ug === undefined ? (r.notation === 'compact' ? 'min2' : 'auto') : (ug === true || ug === 'true' ? 'always' : (ug === false || ug === 'false' ? false : String(ug)));
    r.notation === 'compact' && (r.compactDisplay = getOpt(o, 'compactDisplay', ['short', 'long'], 'short'));
    r.signDisplay = getOpt(o, 'signDisplay', ['auto', 'never', 'always', 'exceptZero', 'negative'], 'auto');
    r.roundingMode = getOpt(o, 'roundingMode', ['ceil', 'floor', 'expand', 'trunc', 'halfCeil', 'halfFloor', 'halfExpand', 'halfTrunc', 'halfEven'], 'halfExpand');
    r.roundingIncrement = 1; r.roundingPriority = 'auto'; r.trailingZeroDisplay = getOpt(o, 'trailingZeroDisplay', ['auto', 'stripIfInteger'], 'auto');
    var bound = this;
    Object.defineProperty(this, 'format', { configurable: true, enumerable: false, get: function () { var f = function (x) { return bound._format(x); }; Object.defineProperty(this, 'format', { value: f, configurable: true }); return f; } });
  }
  function groupInt(s, sep, mode, mg) {
    if (!mode || s.length < 4) return s;
    var min = mode === 'always' ? 4 : (mode === 'min2' ? 5 : mg);
    if (s.length < min) return s;
    var out = '', n = s.length;
    for (var i = 0; i < n; i++) { if (i && (n - i) % 3 === 0) out += sep; out += s[i]; }
    return out;
  }
  function formatCompactTemplate(forms, cat) { return forms[cat] || forms.other || forms[Object.keys(forms)[0]]; }
  NumberFormat.prototype._parts = function (x, mode0) {
    var r = this._r, L = this._L, D = L.data.n, parts = [];
    var d = toDec(x), neg = !!d.neg, isNaNv = !!d.nan;
    function push(type, value) { if (value !== '' && value !== undefined) parts.push({ type: type, value: value }); }
    var cat = 'other';
    var body = [];           // części numeryczne
    var compactTpl = null;
    if (isNaNv) { body.push({ type: 'nan', value: D.nan }); neg = false; }
    else if (d.inf) { body.push({ type: 'infinity', value: D.inf.replace(/^[-+]/, '') }); }
    else {
      var rm = r.roundingMode;
      if (r.style === 'percent') d = shiftDec(d, 2);
      var exp = null, expStr = '';
      if (r.notation === 'compact') {
        var tbl = D.compact[r.compactDisplay === 'long' ? 'long' : 'short'], mag0 = magnitude(d), use = null;
        for (var pass = 0; pass < 2; pass++) {
          var mg = pass ? magnitude(roundForCompact(this, shiftDec(d, -use[1]), rm)) + use[1] : mag0;
          var cand = null;
          for (var k = 0; k < tbl.length; k++) if (tbl[k][0] <= mg) cand = tbl[k];
          if (!cand) break;
          if (pass && cand === use) break;
          use = cand;
        }
        if (use && !isZeroDec(d)) { d = shiftDec(d, -use[1]); compactTpl = use[2]; }
        d = roundForCompact(this, d, rm);
      } else if (r.notation === 'scientific' || r.notation === 'engineering') {
        var m0 = isZeroDec(d) ? 0 : magnitude(d);
        var e0 = r.notation === 'engineering' ? Math.floor(m0 / 3) * 3 : m0;
        d = shiftDec(d, -e0); exp = e0;
        d = this._round(d, rm);
        if (!isZeroDec(d) && magnitude(d) >= (r.notation === 'engineering' ? 3 : 1)) { d = shiftDec(d, r.notation === 'engineering' ? -3 : -1); exp += r.notation === 'engineering' ? 3 : 1; d = this._round(d, rm); }
      } else d = this._round(d, rm);
      var ip = d.i, fp = d.f;
      if (!this._sig || true) {
        var minFD = this._sig ? 0 : r.minimumFractionDigits;
        if (this._sig) { var magn = isZeroDec(d) ? 0 : magnitude(d); minFD = Math.max(0, r.minimumSignificantDigits - 1 - magn); if (isZeroDec(d)) minFD = r.minimumSignificantDigits - 1; }
        if (this._compactDefault) minFD = 0;
        while (fp.length < minFD) fp += '0';
        if (!this._sig) while (fp.length > Math.max(minFD, 0) && fp[fp.length - 1] === '0' && fp.length > (this._compactDefault ? 0 : minFD)) fp = fp.slice(0, -1);
        if (this._sig) while (fp.length > minFD && fp[fp.length - 1] === '0') fp = fp.slice(0, -1);
        if (r.trailingZeroDisplay === 'stripIfInteger' && /^0*$/.test(fp)) fp = '';
      }
      while (ip.length < r.minimumIntegerDigits) ip = '0' + ip;
      cat = pluralOf(L.lang, L.pt.region, 'cardinal', ip + (fp ? '.' + fp : ''));
      var zero = isZeroDec(d);
      if (zero && neg && r.signDisplay !== 'auto') neg = neg;
      var mode = r.useGrouping;
      var grouped = groupInt(ip, D.grp, mode === false ? false : mode, D.mg);
      var gparts = grouped.split(D.grp);
      gparts.forEach(function (gp, i) { if (i) body.push({ type: 'group', value: D.grp }); body.push({ type: 'integer', value: gp }); });
      if (fp) { body.push({ type: 'decimal', value: D.dec }); body.push({ type: 'fraction', value: fp }); }
      if (exp !== null) {
        body.push({ type: 'exponentSeparator', value: D.exp || 'E' });
        if (exp < 0) body.push({ type: 'exponentMinusSign', value: D.minus });
        body.push({ type: 'exponentInteger', value: String(Math.abs(exp)) });
      }
      this._zero = zero;
    }
    // znak
    var zeroV = !isNaNv && !d.inf && this._zero;
    var sign = '';
    var sd = r.signDisplay;
    if (sd === 'never') sign = '';
    else if (sd === 'always') sign = neg ? '-' : '+';
    else if (sd === 'exceptZero') sign = isNaNv || zeroV ? '' : (neg ? '-' : '+');
    else if (sd === 'negative') sign = neg && !zeroV ? '-' : '';
    else sign = neg ? '-' : '';
    var signType = sign === '-' ? 'minusSign' : 'plusSign', signVal = sign === '-' ? D.minus : D.plus;
    // składanie wzorców
    function emitTemplate(tpl, extra) {
      var i = 0, buf = '';
      function flush() { if (buf) { push('literal', buf); buf = ''; } }
      while (i < tpl.length) {
        if (tpl.startsWith('{n}', i)) { flush(); body.forEach(function (b) { parts.push(b); }); if (compactTpl && extra && extra.afterNum) { } i += 3; }
        else if (tpl.startsWith('{s}', i)) { flush(); if (sign) push(signType, signVal); i += 3; }
        else if (tpl[i] === '¤' && extra && extra.cur !== undefined) { flush(); push(extra.curType || 'currency', extra.cur); i++; }
        else if (tpl[i] === '%' && extra && extra.pct) { flush(); push('percentSign', '%'); i++; }
        else { buf += tpl[i]; i++; }
      }
      flush();
    }
    function withCompact() {            // liczba + przyrostek compact (według szablonu)
      if (!compactTpl) return null;
      return formatCompactTemplate(compactTpl, cat);
    }
    var ctpl = withCompact();
    function numWithCompact() {         // zwraca tablicę parts: [liczba][compact]
      var arr = [];
      if (!ctpl) { body.forEach(function (b) { arr.push(b); }); return arr; }
      var i = 0, buf = '';
      function fl() { if (buf) { arr.push({ type: buf.trim() === '' ? 'literal' : 'compact', value: buf }); buf = ''; } }
      while (i < ctpl.length) {
        if (ctpl.startsWith('{n}', i)) { fl(); body.forEach(function (b) { arr.push(b); }); i += 3; } else { buf += ctpl[i]; i++; }
      }
      fl();
      // rozdziel literały (spacje) i przyrostek
      var res = [];
      arr.forEach(function (a) {
        if (a.type === 'compact') { var m = /^([\s\u00a0\u202f]*)(.*)$/.exec(a.value); if (m[1]) res.push({ type: 'literal', value: m[1] }); if (m[2]) res.push({ type: 'compact', value: m[2] }); }
        else res.push(a);
      });
      return res;
    }
    var numArr = numWithCompact();
    function emitNum() { numArr.forEach(function (b) { parts.push(b); }); }
    function emit(tpl, extra) {
      var i = 0, buf = '';
      function flush() { if (buf) { push('literal', buf); buf = ''; } }
      while (i < tpl.length) {
        if (tpl.startsWith('{n}', i)) { flush(); emitNum(); i += 3; }
        else if (tpl.startsWith('{s}', i)) { flush(); if (sign) push(signType, signVal); i += 3; }
        else if (tpl[i] === '¤') { flush(); push(extra.curType || 'currency', extra.cur); i++; }
        else if (tpl[i] === '%' && extra && extra.pct) { flush(); push('percentSign', '%'); i++; }
        else { buf += tpl[i]; i++; }
      }
      flush();
    }
    if (r.style === 'currency') {
      var code = r.currency, sym, tpl;
      if (r.currencyDisplay === 'code') sym = code;
      else if (r.currencyDisplay === 'narrowSymbol') sym = D.nsym[code] || D.sym[code] || code;
      else if (r.currencyDisplay === 'name') sym = null;
      else sym = D.sym[code] || code;
      if (sym === null) {
        var nm = D.cname[code]; var nmv = nm && (nm[cat] || nm.other) ;
        parts.push.apply(parts, []);
        if (sign) push(signType, signVal);
        emitNum(); push('literal', ' '); push('currency', nmv || code);
      } else {
        var neg2 = sign === '-';
        if (r.currencySign === 'accounting' && neg2) tpl = D.curAcc; else if (sign) tpl = D.curNeg; else tpl = D.cur;
        var signTpl = sign === '+' ? D.curNeg.replace('{s}', '{s}') : tpl;
        var ctpl2 = sign === '+' ? D.curNeg : tpl;
        if (/\p{L}$/u.test(sym)) ctpl2 = ctpl2.replace('¤{n}', '¤\u00a0{n}').replace('¤{s}{n}', '¤\u00a0{s}{n}');
        if (/^\p{L}/u.test(sym)) ctpl2 = ctpl2.replace('{n}¤', '{n}\u00a0¤');
        emit(ctpl2, { cur: sym, curType: 'currency' });
      }
    } else if (r.style === 'percent') {
      emit(sign ? D.pctNeg : D.pct, { pct: true });
    } else if (r.style === 'unit') {
      var UD = (L.data.n.unit || DATA.loc['en-US'].n.unit), U = UD[r.unit], tplu = null;
      if (U && U[r.unitDisplay]) { var forms = U[r.unitDisplay]; tplu = forms[cat] || forms.other || forms[Object.keys(forms)[0]]; }
      if (!tplu) { var ten = DATA.loc['en-US'].n.unit[r.unit]; tplu = ten && ten[r.unitDisplay] ? (ten[r.unitDisplay][cat] || ten[r.unitDisplay].other) : '{n} ' + r.unit; }
      if (sign) push(signType, signVal);
      var k2 = 0, bb = '';
      while (k2 < tplu.length) {
        if (tplu.startsWith('{n}', k2)) { if (bb) { push(bb.trim() === '' ? 'literal' : 'unit', bb); bb = ''; } emitNum(); k2 += 3; } else { bb += tplu[k2]; k2++; }
      }
      if (bb) { var m2 = /^([\s\u00a0\u202f]*)(.*?)([\s\u00a0\u202f]*)$/.exec(bb); if (m2[1]) push('literal', m2[1]); if (m2[2]) push('unit', m2[2]); if (m2[3]) push('literal', m2[3]); }
    } else {
      if (sign) push(signType, signVal);
      emitNum();
    }
    return parts;
  };
  function roundForCompact(nf, d, mode) {
    var r = nf._r;
    if (nf._sig) return roundSig(d, r.maximumSignificantDigits, mode);
    if (nf._compactDefault) { return d.i.length >= 2 ? roundDec(d, 0, mode) : roundSig(d, 2, mode); }
    return roundDec(d, r.maximumFractionDigits, mode);
  }
  NumberFormat.prototype._round = function (d, mode) {
    var r = this._r;
    if (this._sig) return roundSig(d, r.maximumSignificantDigits, mode);
    return roundDec(d, r.maximumFractionDigits, mode);
  };
  NumberFormat.prototype._format = function (x) { return this._parts(x).map(function (p) { return p.value; }).join(''); };
  NumberFormat.prototype.formatToParts = function (x) { return this._parts(x); };
  NumberFormat.prototype.formatRange = function (a, b) {
    var sep = this._r.style === 'decimal' || this._r.style === 'percent' && false ? '–' : ' – ';
    if (this._r.style === 'decimal') sep = '–';
    return this._format(a) + sep + this._format(b);
  };
  NumberFormat.prototype.formatRangeToParts = function (a, b) {
    var pa = this._parts(a).map(function (p) { p.source = 'startRange'; return p; }), pb = this._parts(b).map(function (p) { p.source = 'endRange'; return p; });
    return pa.concat([{ type: 'literal', value: this._r.style === 'decimal' ? '–' : ' – ', source: 'shared' }], pb);
  };
  NumberFormat.prototype.resolvedOptions = function () {
    var r = this._r, o = { locale: r.locale, numberingSystem: 'latn', style: r.style };
    ['currency', 'currencyDisplay', 'currencySign', 'unit', 'unitDisplay'].forEach(function (k) { if (r[k] !== undefined) o[k] = r[k]; });
    o.minimumIntegerDigits = r.minimumIntegerDigits;
    if (r.minimumSignificantDigits !== undefined) { o.minimumSignificantDigits = r.minimumSignificantDigits; o.maximumSignificantDigits = r.maximumSignificantDigits; }
    else { o.minimumFractionDigits = r.minimumFractionDigits; o.maximumFractionDigits = r.maximumFractionDigits; }
    o.useGrouping = r.useGrouping; o.notation = r.notation; if (r.compactDisplay) o.compactDisplay = r.compactDisplay;
    o.signDisplay = r.signDisplay; o.roundingIncrement = 1; o.roundingMode = r.roundingMode; o.roundingPriority = 'auto'; o.trailingZeroDisplay = r.trailingZeroDisplay;
    return o;
  };
  NumberFormat.supportedLocalesOf = function (l) { return canonList(l).filter(supported); };
  g.__silver_nf = NumberFormat;

  // ---------------------------------------------------------------- strefy czasowe
  var TZ = DATA.tz || {};
  function nthSunday(y, m, n) { var d = new Date(Date.UTC(y, m, 1)), off = (7 - d.getUTCDay()) % 7; return 1 + off + (n - 1) * 7; }
  function lastSunday(y, m) { var d = new Date(Date.UTC(y, m + 1, 0)); return d.getUTCDate() - d.getUTCDay(); }
  function dstOn(rule, std, ms) {                  // czy w chwili ms (UTC) obowiązuje czas letni
    var y = new Date(ms).getUTCFullYear();
    if (rule === 'EU') { return ms >= Date.UTC(y, 2, lastSunday(y, 2), 1) && ms < Date.UTC(y, 9, lastSunday(y, 9), 1); }
    if (rule === 'US') {
      var start = Date.UTC(y, 2, nthSunday(y, 2, y < 2007 ? 1 : 2), 2) - std * 60000, end = Date.UTC(y, 10, nthSunday(y, 10, 1), 2) - (std + 60) * 60000;
      if (y < 2007) { start = Date.UTC(y, 3, nthSunday(y, 3, 1), 2) - std * 60000; end = Date.UTC(y, 9, lastSunday(y, 9), 2) - (std + 60) * 60000; }
      return ms >= start && ms < end;
    }
    if (rule === 'AU') { var s = Date.UTC(y, 9, nthSunday(y, 9, 1), 2) - std * 60000, e = Date.UTC(y, 3, nthSunday(y, 3, 1), 3) - (std + 60) * 60000; return ms >= s || ms < e; }
    if (rule === 'NZ') { var s2 = Date.UTC(y, 8, lastSunday(y, 8), 2) - std * 60000, e2 = Date.UTC(y, 3, nthSunday(y, 3, 1), 3) - (std + 60) * 60000; return ms >= s2 || ms < e2; }
    if (rule === 'IL') { var fri = lastSunday(y, 2) - 2; return ms >= Date.UTC(y, 2, fri, 2) - std * 60000 && ms < Date.UTC(y, 9, lastSunday(y, 9), 2) - (std + 60) * 60000; }
    return false;
  }
  function tzOffset(zone, ms) {                    // minuty względem UTC
    if (zone === undefined) return -new Date(ms).getTimezoneOffset();
    if (zone.fixed !== undefined) return zone.fixed;
    var z = TZ[zone.name];
    if (!z) return 0;
    return z[0] + (z[1] && dstOn(z[1], z[0], ms) ? 60 : 0);
  }
  function resolveZone(tz) {
    if (tz === undefined) return undefined;
    var s = String(tz), u = s.toUpperCase();
    if (u === 'UTC' || u === 'GMT' || u === 'ETC/UTC' || u === 'ETC/GMT' || u === 'Z') return { name: 'UTC', fixed: 0 };
    var m = /^([+-])(\d{2})(?::?(\d{2}))?$/.exec(s);
    if (m) return { name: s, fixed: (m[1] === '-' ? -1 : 1) * (parseInt(m[2], 10) * 60 + (m[3] ? parseInt(m[3], 10) : 0)) };
    for (var k in TZ) if (k.toLowerCase() === s.toLowerCase()) return { name: k };
    throw rangeErr('Invalid time zone specified: ' + s);
  }
  function zonedFields(ms, zone) {
    var off = tzOffset(zone, ms), d = new Date(ms + off * 60000);
    return { Y: d.getUTCFullYear(), M: d.getUTCMonth(), D: d.getUTCDate(), W: d.getUTCDay(), h: d.getUTCHours(), m: d.getUTCMinutes(), s: d.getUTCSeconds(), ms: d.getUTCMilliseconds(), off: off };
  }
  var US_ABBR = { 'America/New_York': ['EST', 'EDT'], 'America/Toronto': ['EST', 'EDT'], 'America/Chicago': ['CST', 'CDT'], 'America/Denver': ['MST', 'MDT'], 'America/Phoenix': ['MST', 'MST'], 'America/Los_Angeles': ['PST', 'PDT'],
    'America/Anchorage': ['AKST', 'AKDT'], 'Pacific/Honolulu': ['HST', 'HST'], 'America/Vancouver': ['PST', 'PDT'], 'America/Halifax': ['AST', 'ADT'] };
  function offsetString(off, long, gmt) {
    var a = Math.abs(off), h = Math.floor(a / 60), m = a % 60, sg = off < 0 ? '-' : '+';
    if (off === 0) return gmt;
    return gmt + sg + (long ? (h < 10 ? '0' : '') + h + ':' + (m < 10 ? '0' : '') + m : h + (m ? ':' + (m < 10 ? '0' : '') + m : ''));
  }

  // ---------------------------------------------------------------- DateTimeFormat
  var DATE_FIELDS = ['weekday', 'era', 'year', 'month', 'day'], TIME_FIELDS = ['hour', 'minute', 'second'];
  function DateTimeFormat(locales, options) {
    if (!(this instanceof DateTimeFormat)) return new DateTimeFormat(locales, options);
    var o = toOpts(options);
    var L = this._L = resolveLocale(locales);
    var r = this._r = { locale: L.tag, calendar: 'gregory', numberingSystem: 'latn' };
    this._zone = resolveZone(o.timeZone === undefined ? undefined : String(o.timeZone));
    r.timeZone = this._zone ? this._zone.name : (function () { try { return Intl.__localZone || 'UTC'; } catch (e) { return 'UTC'; } })();
    var hour12 = o.hour12, hc = getOpt(o, 'hourCycle', ['h11', 'h12', 'h23', 'h24'], undefined);
    var u = L.pt.ext; if (hc === undefined && u && u.hc) hc = u.hc;
    r.weekday = getOpt(o, 'weekday', ['narrow', 'short', 'long'], undefined);
    r.era = getOpt(o, 'era', ['narrow', 'short', 'long'], undefined);
    r.year = getOpt(o, 'year', ['2-digit', 'numeric'], undefined);
    r.month = getOpt(o, 'month', ['2-digit', 'numeric', 'narrow', 'short', 'long'], undefined);
    r.day = getOpt(o, 'day', ['2-digit', 'numeric'], undefined);
    r.dayPeriod = getOpt(o, 'dayPeriod', ['narrow', 'short', 'long'], undefined);
    r.hour = getOpt(o, 'hour', ['2-digit', 'numeric'], undefined);
    r.minute = getOpt(o, 'minute', ['2-digit', 'numeric'], undefined);
    r.second = getOpt(o, 'second', ['2-digit', 'numeric'], undefined);
    r.fractionalSecondDigits = o.fractionalSecondDigits === undefined ? undefined : getNumOpt(o, 'fractionalSecondDigits', 1, 3, undefined);
    r.timeZoneName = getOpt(o, 'timeZoneName', ['short', 'long', 'shortOffset', 'longOffset', 'shortGeneric', 'longGeneric'], undefined);
    r.dateStyle = getOpt(o, 'dateStyle', ['full', 'long', 'medium', 'short'], undefined);
    r.timeStyle = getOpt(o, 'timeStyle', ['full', 'long', 'medium', 'short'], undefined);
    var hasField = DATE_FIELDS.concat(TIME_FIELDS).concat(['dayPeriod', 'fractionalSecondDigits', 'timeZoneName']).some(function (k) { return r[k] !== undefined; });
    if ((r.dateStyle || r.timeStyle) && hasField) throw new TypeError("Can't set option " + DATE_FIELDS.concat(TIME_FIELDS).concat(['timeZoneName']).filter(function (k) { return r[k] !== undefined; })[0] + ' when ' + (r.dateStyle ? 'dateStyle' : 'timeStyle') + ' is used');
    var needDefaults = o.__required || 'any', defaults = o.__defaults || 'date';
    if (!r.dateStyle && !r.timeStyle) {
      var hasDate = DATE_FIELDS.some(function (k) { return r[k] !== undefined; }), hasTime = TIME_FIELDS.concat(['dayPeriod', 'fractionalSecondDigits']).some(function (k) { return r[k] !== undefined; });
      var need = false;
      if (needDefaults === 'date' && !hasDate) need = true; else if (needDefaults === 'time' && !hasTime) need = true; else if (needDefaults === 'any' && !hasDate && !hasTime) need = true;
      if (need && (defaults === 'date' || defaults === 'all')) { r.year = 'numeric'; r.month = 'numeric'; r.day = 'numeric'; }
      if (need && (defaults === 'time' || defaults === 'all')) { r.hour = 'numeric'; r.minute = 'numeric'; r.second = 'numeric'; }
    }
    var timeUsed = r.hour !== undefined || r.timeStyle !== undefined;
    if (timeUsed) {
      var def = L.data.d.hc;
      if (hour12 !== undefined) hc = hour12 ? (def === 'h11' ? 'h11' : 'h12') : (def === 'h24' ? 'h24' : 'h23');
      else if (hc === undefined) hc = def;
      r.hourCycle = hc; r.hour12 = hc === 'h11' || hc === 'h12';
    }
    this._hc = r.hourCycle;
    var bound = this;
    Object.defineProperty(this, 'format', { configurable: true, enumerable: false, get: function () { var f = function (d) { return bound._format(d); }; Object.defineProperty(this, 'format', { value: f, configurable: true }); return f; } });
  }
  function dtMs(d) {
    var ms = d === undefined ? Date.now() : (d instanceof Date ? d.getTime() : Number(d));
    if (!isFinite(ms) || Math.abs(ms) > 8.64e15) throw rangeErr('Invalid time value');
    return ms;
  }
  DateTimeFormat.prototype._tokens = function () {
    if (this._tk) return this._tk;
    var r = this._r, D = this._L.data.d, tk = [];
    function widthOf(v, kind) { return v; }
    if (r.dateStyle || r.timeStyle) {
      var datePart = r.dateStyle ? PL(D.ds[r.dateStyle]) : null, timePart = r.timeStyle ? PL(D.ts[r.timeStyle]) : null;
      // dostosuj zegar 12/24 do hourCycle: wzorzec ICU pochodzi z domyślnego cyklu lokalizacji → przelicz przez klucz godzin
      if (timePart && this._hc && ((this._hc === 'h12' || this._hc === 'h11') !== (D.hc === 'h12' || D.hc === 'h11'))) {
        var withSec = r.timeStyle !== 'short';
        var base = PL(D.time[(r.hour12 ? 12 : 24) + '1' + '1' + (withSec ? '1' : '0')]);
        if (base) { timePart = base.slice(); if (r.timeStyle === 'full' || r.timeStyle === 'long') timePart = timePart.concat([' ', { f: 'timeZoneName', w: 'x', ts: r.timeStyle }]); }
      }
      if (timePart && (r.timeStyle === 'full' || r.timeStyle === 'long')) timePart = timePart.map(function (t) { return t && t.f === 'timeZoneName' ? { f: 'timeZoneName', w: 'x', ts: r.timeStyle } : t; });
      if (datePart) tk = tk.concat(datePart);
      if (datePart && timePart) tk.push(D.join[r.dateStyle]);
      if (timePart) tk = tk.concat(timePart);
    } else {
      var hasD = r.weekday || r.year || r.month || r.day, hasT = r.hour || r.minute || r.second || r.fractionalSecondDigits || r.dayPeriod;
      if (hasD) {
        var mk = r.month === undefined ? '0' : (r.month === 'numeric' || r.month === '2-digit' ? 'n' : (r.month === 'long' ? 't' : 's'));
        var key = (r.weekday ? 1 : 0) + '' + (r.year ? 1 : 0) + mk + (r.day ? 1 : 0), pat = PL(D.date[key]) || [];
        var arr = pat.map(function (t) {
          if (typeof t === 'string') return t;
          var c = { f: t.f, w: t.w }; c.user = r[t.f]; return c;
        });
        if (r.era && !arr.some(function (t) { return t && t.f === 'era'; })) { arr.push(' '); arr.push({ f: 'era', w: 'x', user: r.era }); }
        tk = tk.concat(arr);
      }
      if (hasT) {
        var h12 = r.hourCycle === 'h12' || r.hourCycle === 'h11';
        var tkey = (h12 ? 12 : 24) + '' + (r.hour ? 1 : 0) + (r.minute ? 1 : 0) + (r.second ? 1 : 0), tp = PL(D.time[tkey]);
        var tarr;
        if (tp) tarr = tp.map(function (t) { if (typeof t === 'string') return t; var c = { f: t.f, w: t.w, user: r[t.f] }; return c; });
        else { tarr = []; ['hour', 'minute', 'second'].forEach(function (f) { if (r[f]) { if (tarr.length) tarr.push(':'); tarr.push({ f: f, w: '2d', user: r[f] }); } }); }
        if (r.fractionalSecondDigits) { var si = -1; tarr.forEach(function (t, i) { if (t && t.f === 'second') si = i; }); if (si >= 0) { tarr.splice(si + 1, 0, ',', { f: 'fractionalSecond', w: 'x', n: r.fractionalSecondDigits }); tarr[si + 1] = D.num && D.num.dec || '.'; } }
        if (r.timeZoneName) { tarr.push(' '); tarr.push({ f: 'timeZoneName', w: 'x', user: r.timeZoneName }); }
        if (tk.length) tk.push(r.month === 'long' ? D.join.long : D.join.medium);
        tk = tk.concat(tarr);
      }
    }
    return (this._tk = tk);
  };
  function pad(n, w) { var s = String(n); while (s.length < w) s = '0' + s; return s; }
  DateTimeFormat.prototype._parts = function (date) {
    var ms = dtMs(date), r = this._r, D = this._L.data.d, f = zonedFields(ms, this._zone), tk = this._tokens(), out = [];
    var hasDay = tk.some(function (t) { return t && t.f === 'day'; });
    var self = this;
    tk.forEach(function (t) {
      if (typeof t === 'string') { if (t) out.push({ type: 'literal', value: t }); return; }
      var v = '', w = t.user || t.w, type = t.f;
      switch (type) {
        case 'weekday': v = D.days[w === 'narrow' ? 'narrow' : (w === 'short' ? 'short' : 'long')][f.W]; break;
        case 'era': v = D.era.short[f.Y > 0 ? 1 : 0]; break;
        case 'year': {
          var y = f.Y <= 0 && false ? 1 - f.Y : f.Y;
          v = (t.user === '2-digit' || (t.user !== 'numeric' && t.w === '2d')) ? pad(((y % 100) + 100) % 100, 2) : (t.w === '2d' && !t.user ? pad(((y % 100) + 100) % 100, 2) : String(y));
          break;
        }
        case 'month': {
          var uw = t.user;
          if (uw === 'numeric' || uw === '2-digit' || (!uw && (t.w === 'num' || t.w === '2d'))) v = (uw === '2-digit' || t.w === '2d') ? pad(f.M + 1, 2) : String(f.M + 1);
          else { var mw = uw || t.w; if (mw === 'num' || mw === '2d') mw = 'short'; v = hasDay && D.monthsF[mw] ? D.monthsF[mw][f.M] : D.months[mw][f.M]; }
          break;
        }
        case 'day': v = (t.user === '2-digit' || t.w === '2d') ? pad(f.D, 2) : String(f.D); break;
        case 'hour': {
          var hh = f.h, hc = r.hourCycle;
          if (hc === 'h12') hh = hh % 12 || 12; else if (hc === 'h11') hh = hh % 12; else if (hc === 'h24') hh = hh || 24;
          v = (t.user === '2-digit' || t.w === '2d') ? pad(hh, 2) : String(hh);
          break;
        }
        case 'minute': v = (t.user === '2-digit' || t.w === '2d') ? pad(f.m, 2) : String(f.m); break;
        case 'second': v = (t.user === '2-digit' || t.w === '2d') ? pad(f.s, 2) : String(f.s); break;
        case 'fractionalSecond': v = pad(f.ms, 3).slice(0, t.n || 3); break;
        case 'dayPeriod': v = f.h < 12 ? D.am : D.pm; break;
        case 'timeZoneName': {
          var mode = t.user || (t.ts === 'full' ? 'long' : 'short'), z = self._zone, gmt = (D.gmt && D.gmt[1]) ? String(D.gmt[1]).replace(/[+\-\d:].*$/, '') || 'GMT' : 'GMT';
          if (D.gmt && D.gmt[0]) gmt = D.gmt[0].replace(/[+\-−\d].*$/, '') || 'GMT';
          var ab = z && z.name && US_ABBR[z.name];
          if (z && z.name === 'UTC' && z.fixed === 0 && (mode === 'short' || mode === 'shortGeneric')) v = 'UTC';
          else if (ab && (mode === 'short' || mode === 'shortGeneric')) { var dst = TZ[z.name] && TZ[z.name][1] && dstOn(TZ[z.name][1], TZ[z.name][0], ms); v = mode === 'shortGeneric' ? ab[0].replace(/S/, '') : ab[dst ? 1 : 0]; }
          else if (mode === 'long' && z && z.name === 'UTC') v = D.utcLong || 'Coordinated Universal Time';
          else if (mode === 'longOffset' || mode === 'long' || mode === 'longGeneric') v = f.off === 0 ? gmt : offsetString(f.off, true, gmt);
          else v = f.off === 0 ? gmt : offsetString(f.off, false, gmt);
          break;
        }
      }
      out.push({ type: type, value: v });
    });
    // scal sąsiednie literały
    var merged = [];
    out.forEach(function (p) { var l = merged[merged.length - 1]; if (l && l.type === 'literal' && p.type === 'literal') l.value += p.value; else merged.push(p); });
    return merged;
  };
  DateTimeFormat.prototype._format = function (d) { return this._parts(d).map(function (p) { return p.value; }).join(''); };
  DateTimeFormat.prototype.formatToParts = function (d) { return this._parts(d); };
  DateTimeFormat.prototype.formatRange = function (a, b) {
    var sep = this._L.lang === 'en' ? '\u2009–\u2009' : ' – ';
    var fa = this._format(a), fb = this._format(b);
    return fa === fb ? fa : fa + sep + fb;
  };
  DateTimeFormat.prototype.formatRangeToParts = function (a, b) {
    var sep = this._L.lang === 'en' ? '\u2009–\u2009' : ' – ';
    var pa = this._parts(a).map(function (p) { p.source = 'startRange'; return p; }), pb = this._parts(b).map(function (p) { p.source = 'endRange'; return p; });
    return pa.concat([{ type: 'literal', value: sep, source: 'shared' }], pb);
  };
  DateTimeFormat.prototype.resolvedOptions = function () {
    var r = this._r, o = { locale: r.locale, calendar: 'gregory', numberingSystem: 'latn', timeZone: r.timeZone };
    if (r.hourCycle) { o.hourCycle = r.hourCycle; o.hour12 = r.hour12; }
    ['weekday', 'era', 'year', 'month', 'day', 'dayPeriod', 'hour', 'minute', 'second', 'fractionalSecondDigits', 'timeZoneName', 'dateStyle', 'timeStyle'].forEach(function (k) { if (r[k] !== undefined) o[k] = r[k]; });
    if (o.hour && o.hour !== '2-digit' && (o.hourCycle === 'h23' || o.hourCycle === 'h24')) o.hour = '2-digit';
    if (o.minute && o.hour) o.minute = '2-digit'; if (o.second && o.minute) o.second = '2-digit';
    return o;
  };
  DateTimeFormat.supportedLocalesOf = function (l) { return canonList(l).filter(supported); };
  try { var off0 = -new Date().getTimezoneOffset(); Intl.__localZone = 'UTC'; } catch (e) { }

  // ---------------------------------------------------------------- RelativeTimeFormat
  var REL_UNIT = { second: 1, minute: 1, hour: 1, day: 1, week: 1, month: 1, quarter: 1, year: 1 };
  function RelativeTimeFormat(locales, options) {
    if (!(this instanceof RelativeTimeFormat)) throw new TypeError("Constructor Intl.RelativeTimeFormat requires 'new'");
    var o = toOpts(options);
    this._L = resolveLocale(locales);
    this._style = getOpt(o, 'style', ['long', 'short', 'narrow'], 'long');
    this._numeric = getOpt(o, 'numeric', ['always', 'auto'], 'always');
    this._nf = new NumberFormat(locales);
  }
  RelativeTimeFormat.prototype._p = function (value, unit) {
    value = Number(value);
    if (!isFinite(value)) throw rangeErr('Value need to be finite number for Intl.RelativeTimeFormat.prototype.format()');
    unit = String(unit).replace(/s$/, '');
    if (!REL_UNIT[unit]) throw rangeErr('Invalid unit argument for format() \'' + unit + '\'');
    var L = this._L, RS = L.data.r, R = (RS[this._style] && RS[this._style][unit]) || (RS.short && RS.short[unit]) || RS.long[unit], neg = value < 0 || (value === 0 && 1 / value < 0);
    if (this._numeric === 'auto' && R.auto[String(value)] !== undefined && !(value === 0 && neg && false)) return [{ type: 'literal', value: R.auto[String(value === 0 ? 0 : value)] }];
    var abs = Math.abs(value), cat = pluralOf(L.lang, L.pt.region, 'cardinal', String(abs));
    var forms = neg ? R.past : R.fut, tpl = forms[cat] || forms.other || forms[Object.keys(forms)[0]];
    var out = [], nump = this._nf._parts(abs).map(function (p) { return { type: p.type, value: p.value, unit: unit }; });
    var i = tpl.indexOf('{n}');
    if (i > 0) out.push({ type: 'literal', value: tpl.slice(0, i) });
    nump.forEach(function (p) { out.push(p); });
    if (i + 3 < tpl.length) out.push({ type: 'literal', value: tpl.slice(i + 3) });
    return out;
  };
  RelativeTimeFormat.prototype.format = function (v, u) { return this._p(v, u).map(function (p) { return p.value; }).join(''); };
  RelativeTimeFormat.prototype.formatToParts = function (v, u) { return this._p(v, u); };
  RelativeTimeFormat.prototype.resolvedOptions = function () { return { locale: this._L.tag, style: this._style, numeric: this._numeric, numberingSystem: 'latn' }; };
  RelativeTimeFormat.supportedLocalesOf = function (l) { return canonList(l).filter(supported); };

  // ---------------------------------------------------------------- ListFormat
  function ListFormat(locales, options) {
    if (!(this instanceof ListFormat)) throw new TypeError("Constructor Intl.ListFormat requires 'new'");
    var o = toOpts(options);
    this._L = resolveLocale(locales);
    this._type = getOpt(o, 'type', ['conjunction', 'disjunction', 'unit'], 'conjunction');
    this._style = getOpt(o, 'style', ['long', 'short', 'narrow'], 'long');
  }
  ListFormat.prototype.formatToParts = function (list) {
    var a = [];
    if (list !== undefined) { var it = list[Symbol.iterator] ? Array.from(list) : []; it.forEach(function (x) { if (typeof x !== 'string') throw new TypeError('Iterable yielded ' + String(x) + ' which is not a string'); a.push(x); }); }
    var P = this._L.data.l[this._type + '|' + this._style], out = [];
    a.forEach(function (el, i) {
      if (i) { var sep = a.length === 2 ? P.two : (i === a.length - 1 ? P.end : (i === 1 ? P.start : P.mid)); out.push({ type: 'literal', value: sep }); }
      out.push({ type: 'element', value: el });
    });
    return out;
  };
  ListFormat.prototype.format = function (l) { return this.formatToParts(l).map(function (p) { return p.value; }).join(''); };
  ListFormat.prototype.resolvedOptions = function () { return { locale: this._L.tag, type: this._type, style: this._style }; };
  ListFormat.supportedLocalesOf = function (l) { return canonList(l).filter(supported); };

  // ---------------------------------------------------------------- Collator
  var ALPHA = {
    pl: 'aąbcćdeęfghijklłmnńoóprsśtuwyzźż', cs: 'aábcčdďeéěfghiíjklmnňoópqrřsštťuúůvwxyýzž', sk: 'aáäbcčdďeéfghiíjklĺľmnňoóôpqrŕsštťuúvwxyýzž',
    sv: 'abcdefghijklmnopqrstuvwxyzåäö', fi: 'abcdefghijklmnopqrsštuvwxyzžåäö', da: 'abcdefghijklmnopqrstuvwxyzæøå', nb: 'abcdefghijklmnopqrstuvwxyzæøå', no: 'abcdefghijklmnopqrstuvwxyzæøå',
    es: 'abcdefghijklmnñopqrstuvwxyz', tr: 'abcçdefgğhıijklmnoöprsştuüvyz', hu: 'aábcdeéfghiíjklmnoóöőpqrstuúüűvwxyz', ro: 'aăâbcdefghiîjklmnopqrsștțuvwxyz',
    hr: 'abcčćdđefghijklmnoprsštuvzž', sl: 'abcčdefghijklmnoprsštuvzž', lt: 'aąbcčdeęėfghiįyjklmnoprsštuųūvzž', lv: 'aābcčdeēfgģhiījkķlļmnņoprsštuūvzž',
    ru: 'абвгдеёжзийклмнопрстуфхцчшщъыьэюя', uk: 'абвгґдеєжзиіїйклмнопрстуфхцчшщьюя', bg: 'абвгдежзийклмнопрстуфхцчшщъьюя', el: 'αβγδεζηθικλμνξοπρσςτυφχψω'
  };
  var CONTR = { cs: ['ch'], sk: ['ch'] };
  function charClass(c) {
    if (/\s/.test(c)) return 1;
    if (/[\p{P}]/u.test(c)) return 2;
    if (/[\p{S}]/u.test(c)) return 3;
    if (/\p{N}/u.test(c)) return 4;
    return 5;
  }
  function collationKeys(str, lang, opt) {
    var s = String(str).normalize('NFC'), prim = [], sec = [], ter = [];
    var alpha = ALPHA[lang] || '', contr = CONTR[lang] || [], i = 0;
    var lower = s.toLowerCase();
    function ord(ch) { return alpha.indexOf(ch); }
    while (i < s.length) {
      var ch = s[i], cl = lower[i] || ch.toLowerCase(), cp = s.codePointAt(i), adv = cp > 0xffff ? 2 : 1;
      if (adv === 2) { ch = s.substr(i, 2); cl = ch.toLowerCase(); }
      var cls = charClass(ch);
      if (opt.ignorePunctuation && (cls === 1 || cls === 2)) { i += adv; continue; }
      var isUp = ch !== cl && ch.toLowerCase() !== ch;
      var done = false;
      for (var c = 0; c < contr.length && !done; c++) {
        if (lower.substr(i, contr[c].length) === contr[c]) { prim.push([5, 1000 + ord(contr[c][0]) * 10 + 5]); sec.push(0); ter.push(s.substr(i, contr[c].length) === contr[c] ? 0 : 1); i += contr[c].length; done = true; }
      }
      if (done) continue;
      if (cls === 4 && opt.numeric) {
        var m = /^\d+/.exec(s.slice(i)); if (m) { var digits = m[0].replace(/^0+(?=\d)/, ''); prim.push([4, digits.length * 1e6 + parseInt(digits.slice(0, 9), 10) % 1e6, digits]); sec.push(0); ter.push(0); i += m[0].length; continue; }
      }
      var w, accent = 0;
      var idx = ord(cl);
      if (idx >= 0) w = idx * 10 + 5;
      else {
        var dec = cl.normalize('NFD'), base = dec[0], marks = dec.slice(1);
        var bi = ord(base);
        if (bi >= 0) { w = bi * 10 + 5; accent = marks.length ? marks.charCodeAt(0) : 0; }
        else if (alpha === '' && /[a-z]/.test(base)) { w = (base.charCodeAt(0) - 97) * 10 + 5; accent = marks.length ? marks.charCodeAt(0) : 0; }
        else if (cl === 'ß') { prim.push([5, (115 - 97) * 10 + 5]); sec.push(0); ter.push(0); w = (115 - 97) * 10 + 5; }
        else if (cl === 'æ' && !alpha) { prim.push([5, 5]); sec.push(0); ter.push(0); w = (101 - 97) * 10 + 5; }
        else if (cl === 'ø' && !alpha) { w = (111 - 97) * 10 + 5; accent = 0x338; }
        else if (cl === 'đ' && !alpha) { w = (100 - 97) * 10 + 5; accent = 0x335; }
        else if (cl === 'ł' && !alpha) { w = (108 - 97) * 10 + 5; accent = 0x338; }
        else w = (alpha ? 1000 : 3000) + cl.codePointAt(0);
        if (accent && /\p{M}/u.test(marks[0])) { } 
      }
      if (cls === 5 && w >= 3000 && !/[a-z]/.test(cl)) w = 3000 + cl.codePointAt(0);
      prim.push([cls, w]); sec.push(accent); ter.push(isUp ? 1 : 0);
      i += adv;
    }
    return { p: prim, s: sec, t: ter };
  }
  function Collator(locales, options) {
    if (!(this instanceof Collator)) return new Collator(locales, options);
    var o = toOpts(options);
    this._L = resolveLocale(locales);
    this._usage = getOpt(o, 'usage', ['sort', 'search'], 'sort');
    this._sens = getOpt(o, 'sensitivity', ['base', 'accent', 'case', 'variant'], 'variant');
    this._ip = o.ignorePunctuation === true;
    this._num = o.numeric === true || (this._L.pt.ext && this._L.pt.ext.kn === 'true');
    this._cf = getOpt(o, 'caseFirst', ['upper', 'lower', 'false'], 'false');
    var self = this;
    this._opt = { ignorePunctuation: this._ip, numeric: this._num };
    Object.defineProperty(this, 'compare', { configurable: true, enumerable: false, get: function () { var f = function (a, b) { return self._cmp(String(a), String(b)); }; Object.defineProperty(self, 'compare', { value: f, configurable: true }); return f; } });
  }
  Collator.prototype._cmp = function (a, b) {
    if (a === b) return 0;
    var lang = this._L.lang, ka = collationKeys(a, lang, this._opt), kb = collationKeys(b, lang, this._opt), n = Math.max(ka.p.length, kb.p.length), i;
    for (i = 0; i < n; i++) {
      var x = ka.p[i], y = kb.p[i];
      if (!x) return -1; if (!y) return 1;
      if (x[0] !== y[0]) return x[0] < y[0] ? -1 : 1;
      if (x[1] !== y[1]) return x[1] < y[1] ? -1 : 1;
    }
    var sens = this._sens;
    if (sens === 'accent' || sens === 'variant') for (i = 0; i < n; i++) { if (ka.s[i] !== kb.s[i]) return ka.s[i] < kb.s[i] ? -1 : 1; }
    if (sens === 'case' || sens === 'variant') {
      for (i = 0; i < n; i++) if (ka.t[i] !== kb.t[i]) { var lowFirst = this._cf !== 'upper'; return (ka.t[i] < kb.t[i]) === lowFirst ? -1 : 1; }
    }
    return 0;
  };
  Collator.prototype.resolvedOptions = function () { return { locale: this._L.tag, usage: this._usage, sensitivity: this._sens, ignorePunctuation: this._ip, collation: 'default', numeric: !!this._num, caseFirst: this._cf }; };
  Collator.supportedLocalesOf = function (l) { return canonList(l).filter(supported); };

  // ---------------------------------------------------------------- DisplayNames
  function DisplayNames(locales, options) {
    if (!(this instanceof DisplayNames)) throw new TypeError("Constructor Intl.DisplayNames requires 'new'");
    var o = toOpts(options);
    if (o.type === undefined) throw new TypeError('Required option "type" is missing');
    this._L = resolveLocale(locales);
    this._type = getOpt(o, 'type', ['language', 'region', 'script', 'currency', 'calendar', 'dateTimeField'], undefined);
    this._fallback = getOpt(o, 'fallback', ['code', 'none'], 'code');
    this._style = getOpt(o, 'style', ['narrow', 'short', 'long'], 'long');
    this._ld = getOpt(o, 'languageDisplay', ['dialect', 'standard'], 'dialect');
  }
  DisplayNames.prototype.of = function (code) {
    code = String(code);
    var key = DATA.names[this._L.key] ? this._L.key : (LANGDEF[this._L.lang] && DATA.names[LANGDEF[this._L.lang]] ? LANGDEF[this._L.lang] : 'en-US');
    var N = DATA.names[key] || DATA.names['en-US'], T = this._type, v;
    if (T === 'region') v = N.region[code.toUpperCase()];
    else if (T === 'currency') v = N.currency[code.toUpperCase()];
    else if (T === 'script') v = N.script[code[0].toUpperCase() + code.slice(1).toLowerCase()];
    else if (T === 'language') {
      var p = code.split(/[-_]/), l = N.language[p[0].toLowerCase()];
      if (l) { var rest = p.slice(1).map(function (x) { return x.length === 2 ? (N.region[x.toUpperCase()] || x.toUpperCase()) : (N.script[x[0].toUpperCase() + x.slice(1).toLowerCase()] || x); }); v = rest.length ? l + ' (' + rest.join(', ') + ')' : l; }
    }
    if (v === undefined) return this._fallback === 'code' ? (T === 'region' || T === 'currency' ? code.toUpperCase() : code) : undefined;
    return v;
  };
  DisplayNames.prototype.resolvedOptions = function () { return { locale: this._L.tag, style: this._style, type: this._type, fallback: this._fallback, languageDisplay: this._ld }; };
  DisplayNames.supportedLocalesOf = function (l) { return canonList(l).filter(supported); };

  // ---------------------------------------------------------------- Locale
  var LIKELY_REGION = { en: 'US', pl: 'PL', de: 'DE', fr: 'FR', es: 'ES', it: 'IT', pt: 'BR', nl: 'NL', cs: 'CZ', sk: 'SK', uk: 'UA', ru: 'RU', sv: 'SE', da: 'DK', nb: 'NO', fi: 'FI', tr: 'TR', ja: 'JP', zh: 'CN', ko: 'KR', hu: 'HU', ro: 'RO', el: 'GR', bg: 'BG', hr: 'HR', lt: 'LT', lv: 'LV', sl: 'SI' };
  function Locale(tag, options) {
    if (!(this instanceof Locale)) throw new TypeError("Constructor Intl.Locale requires 'new'");
    if (typeof tag !== 'string' && !(tag && tag.__isLocale)) throw new TypeError('First argument to Intl.Locale constructor can\'t be empty or missing');
    var pt = parseTag(String(tag)), o = toOpts(options);
    if (o.language) pt.language = String(o.language).toLowerCase();
    if (o.script) pt.script = String(o.script);
    if (o.region) pt.region = String(o.region).toUpperCase();
    ['calendar:ca', 'collation:co', 'hourCycle:hc', 'caseFirst:kf', 'numberingSystem:nu'].forEach(function (kv) { var p = kv.split(':'); if (o[p[0]] !== undefined) pt.ext[p[1]] = String(o[p[0]]); });
    if (o.numeric !== undefined) pt.ext.kn = o.numeric ? 'true' : 'false';
    this._pt = pt;
    Object.defineProperty(this, '__isLocale', { value: true });
  }
  Locale.prototype = {
    get language() { return this._pt.language; }, get script() { return this._pt.script; }, get region() { return this._pt.region; },
    get baseName() { var p = this._pt; return p.language + (p.script ? '-' + p.script : '') + (p.region ? '-' + p.region : ''); },
    get calendar() { return this._pt.ext.ca; }, get collation() { return this._pt.ext.co; }, get hourCycle() { return this._pt.ext.hc; },
    get caseFirst() { return this._pt.ext.kf; }, get numeric() { return this._pt.ext.kn === 'true'; }, get numberingSystem() { return this._pt.ext.nu; },
    toString: function () {
      var ks = Object.keys(this._pt.ext).sort(), u = ks.map(function (k) { var v = this._pt.ext[k]; return k + (v === 'true' ? '' : '-' + v); }, this).join('-');
      return this.baseName + (u ? '-u-' + u : '');
    },
    maximize: function () { var p = this._pt, n = new Locale(this.toString()); n._pt.script = n._pt.script || SCRIPT[p.language] || 'Latn'; n._pt.region = n._pt.region || LIKELY_REGION[p.language]; return n; },
    minimize: function () { var n = new Locale(this.toString()); if (LIKELY_REGION[n._pt.language] === n._pt.region) n._pt.region = undefined; if ((SCRIPT[n._pt.language] || 'Latn') === n._pt.script) n._pt.script = undefined; return n; },
    getWeekInfo: function () { var fd = DATA.loc[dataKey(this._pt)].d.wk; return { firstDay: fd, weekend: [6, 7], minimalDays: 4 }; },
    getTextInfo: function () { return { direction: /^(ar|he|fa|ur)$/.test(this._pt.language) ? 'rtl' : 'ltr' }; },
    getHourCycles: function () { return [DATA.loc[dataKey(this._pt)].d.hc]; },
    getCalendars: function () { return ['gregory']; }, getNumberingSystems: function () { return ['latn']; }, getTimeZones: function () { return undefined; }
  };
  Object.defineProperty(Locale.prototype, 'weekInfo', { get: function () { return this.getWeekInfo(); } });
  Object.defineProperty(Locale.prototype, 'textInfo', { get: function () { return this.getTextInfo(); } });

  // ---------------------------------------------------------------- Segmenter
  function Segmenter(locales, options) {
    if (!(this instanceof Segmenter)) throw new TypeError("Constructor Intl.Segmenter requires 'new'");
    var o = toOpts(options);
    this._L = resolveLocale(locales);
    this._g = getOpt(o, 'granularity', ['grapheme', 'word', 'sentence'], 'grapheme');
  }
  function graphemes(str) {
    var out = [], i = 0, n = str.length;
    var RE_EXT = /[\p{M}\u200d\ufe0f\u{1f3fb}-\u{1f3ff}\u{e0020}-\u{e007f}]/u;
    var cps = Array.from(str), idx = 0, cur = '', prev = null, ri = 0;
    cps.forEach(function (c, k) {
      var cp = c.codePointAt(0), isExt = RE_EXT.test(c), isRI = cp >= 0x1f1e6 && cp <= 0x1f1ff;
      var join = false;
      if (cur) {
        if (isExt) join = true;
        else if (prev === '\u200d' && /[\p{Extended_Pictographic}]/u.test(c)) join = true;
        else if (prev === '\r' && c === '\n') join = true;
        else if (isRI && ri % 2 === 1) join = true;
        else if (cp >= 0x1160 && cp <= 0x11ff && /[\u1100-\u115f\uac00-\ud7a3]/.test(prev || '')) join = true;
        else if (cp >= 0x11a8 && cp <= 0x11ff) join = true;
      }
      if (isRI) ri = join ? ri + 1 : 1; else ri = 0;
      if (join) cur += c; else { if (cur) out.push({ segment: cur, index: idx - cur.length }); cur = c; }
      idx += c.length; prev = c;
    });
    if (cur) out.push({ segment: cur, index: idx - cur.length });
    return out;
  }
  Segmenter.prototype.segment = function (input) {
    var str = String(input), g = this._g, segs = [];
    if (g === 'grapheme') segs = graphemes(str);
    else if (g === 'word') { var re = /[\p{L}\p{N}\p{M}_]+(?:['’.\u00b7][\p{L}\p{N}\p{M}_]+)*|\s+|[\u3040-\u30ff\u4e00-\u9fff]|[\s\S]/gu, m; while ((m = re.exec(str))) segs.push({ segment: m[0], index: m.index, isWordLike: /[\p{L}\p{N}]/u.test(m[0]) }); }
    else { var re2 = /[^.!?…]+(?:[.!?…]+["'”’)\]]*\s*|$)|[.!?…]+["'”’)\]]*\s*/g, m2; while ((m2 = re2.exec(str))) { if (!m2[0]) { re2.lastIndex++; continue; } segs.push({ segment: m2[0], index: m2.index }); } }
    segs.forEach(function (s) { s.input = str; });
    var res = { containing: function (i) { i = i === undefined ? 0 : Math.trunc(Number(i)); if (i < 0 || i >= str.length) return undefined; for (var k = 0; k < segs.length; k++) if (i >= segs[k].index && i < segs[k].index + segs[k].segment.length) return segs[k]; return undefined; } };
    res[Symbol.iterator] = function () { var k = 0; return { next: function () { return k < segs.length ? { value: segs[k++], done: false } : { value: undefined, done: true }; }, [Symbol.iterator]: function () { return this; } }; };
    return res;
  };
  Segmenter.prototype.resolvedOptions = function () { return { locale: this._L.tag, granularity: this._g }; };
  Segmenter.supportedLocalesOf = function (l) { return canonList(l).filter(supported); };

  // ---------------------------------------------------------------- Intl + prototypy
  var CURS = Object.keys(DATA.loc['en-US'].n.sym).sort();
  var UNITS = Object.keys(DATA.loc['en-US'].n.unit || {}).sort();
  var intl = {
    NumberFormat: NumberFormat, DateTimeFormat: DateTimeFormat, PluralRules: PluralRules, Collator: Collator, ListFormat: ListFormat,
    RelativeTimeFormat: RelativeTimeFormat, Segmenter: Segmenter, DisplayNames: DisplayNames, Locale: Locale,
    getCanonicalLocales: function (l) { return canonList(l); },
    supportedValuesOf: function (k) {
      switch (String(k)) {
        case 'currency': return CURS.slice();
        case 'timeZone': return Object.keys(TZ).filter(function (z) { return z !== 'UTC' && z !== 'GMT'; }).sort();
        case 'unit': return UNITS.slice();
        case 'calendar': return ['gregory']; case 'collation': return ['default']; case 'numberingSystem': return ['latn'];
        default: throw rangeErr('Invalid key : ' + k);
      }
    }
  };
  Object.keys(intl).forEach(function (k) { if (typeof intl[k] === 'function' && /^[A-Z]/.test(k)) Object.defineProperty(intl[k], 'name', { value: k }); });
  if (typeof Symbol !== 'undefined' && Symbol.toStringTag) Object.defineProperty(intl, Symbol.toStringTag, { value: 'Intl', configurable: true });
  function defDT(opts, req, defs) { var c = {}; for (var k in opts) c[k] = opts[k]; c.__required = req; c.__defaults = defs; return c; }
  Object.defineProperty(Number.prototype, 'toLocaleString', { value: function (l, o) { return new NumberFormat(l, o).format(Number(this)); }, writable: true, configurable: true });
  if (typeof BigInt === 'function') Object.defineProperty(BigInt.prototype, 'toLocaleString', { value: function (l, o) { return new NumberFormat(l, o).format(this.valueOf()); }, writable: true, configurable: true });
  Object.defineProperty(Date.prototype, 'toLocaleDateString', { value: function (l, o) { return new DateTimeFormat(l, defDT(toOpts(o), 'date', 'date')).format(this); }, writable: true, configurable: true });
  Object.defineProperty(Date.prototype, 'toLocaleTimeString', { value: function (l, o) { return new DateTimeFormat(l, defDT(toOpts(o), 'time', 'time')).format(this); }, writable: true, configurable: true });
  Object.defineProperty(Date.prototype, 'toLocaleString', { value: function (l, o) { return new DateTimeFormat(l, defDT(toOpts(o), 'any', 'all')).format(this); }, writable: true, configurable: true });
  Object.defineProperty(String.prototype, 'localeCompare', { value: function (that, l, o) { return new Collator(l, o).compare(String(this), String(that)); }, writable: true, configurable: true });
  Object.defineProperty(String.prototype, 'toLocaleUpperCase', { value: function (l) { var lang = l === undefined ? '' : String(Array.isArray(l) ? l[0] : l).split('-')[0]; var s = String(this); if (lang === 'tr' || lang === 'az') s = s.replace(/i/g, 'İ').replace(/ı/g, 'I'); return s.toUpperCase(); }, writable: true, configurable: true });
  Object.defineProperty(String.prototype, 'toLocaleLowerCase', { value: function (l) { var lang = l === undefined ? '' : String(Array.isArray(l) ? l[0] : l).split('-')[0]; var s = String(this); if (lang === 'tr' || lang === 'az') s = s.replace(/İ/g, 'i').replace(/I/g, 'ı'); return s.toLowerCase(); }, writable: true, configurable: true });
  g.Intl = intl;
})(globalThis);
