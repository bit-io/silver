#ifndef SILVER_NET_H
#define SILVER_NET_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#ifdef _MSC_VER
#  define strncasecmp _strnicmp
#  define strcasecmp _stricmp
#endif

#ifdef SILVER_TLS
#  include <openssl/ssl.h>
#  include <openssl/err.h>
#  include <openssl/x509v3.h>
#endif

/* ---------------------------------------------------------------- platforma */
#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  include <windows.h>
typedef SOCKET net_sock_t;
#  define NET_INVALID INVALID_SOCKET
typedef CRITICAL_SECTION net_mutex_t;
typedef CONDITION_VARIABLE net_cond_t;
static void net_mutex_init(net_mutex_t *m) { InitializeCriticalSection(m); }
static void net_mutex_lock(net_mutex_t *m) { EnterCriticalSection(m); }
static void net_mutex_unlock(net_mutex_t *m) { LeaveCriticalSection(m); }
static void net_cond_init(net_cond_t *c) { InitializeConditionVariable(c); }
static void net_cond_signal(net_cond_t *c) { WakeConditionVariable(c); }
static void net_cond_wait_ms(net_cond_t *c, net_mutex_t *m, int ms) { SleepConditionVariableCS(c, m, (DWORD)(ms < 0 ? 0 : ms)); }
typedef struct { void *(*fn)(void *); void *arg; } net_thr_boot_t;
static DWORD WINAPI net_thr_tramp(LPVOID p) {
    net_thr_boot_t b = *(net_thr_boot_t *)p; free(p); b.fn(b.arg); return 0;
}
static int net_thread_start(void *(*fn)(void *), void *arg) {   /* wątek odłączony; 0 = błąd */
    net_thr_boot_t *b = (net_thr_boot_t *)malloc(sizeof *b);
    if (!b) return 0;
    b->fn = fn; b->arg = arg;
    HANDLE h = CreateThread(NULL, 1u << 20, net_thr_tramp, b, 0, NULL);
    if (!h) { free(b); return 0; }
    CloseHandle(h);
    return 1;
}
static void net_sleep_ms(int ms) { Sleep((DWORD)ms); }
static int64_t net_now_ms(void) {
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f); QueryPerformanceCounter(&c);
    return (int64_t)(c.QuadPart * 1000 / f.QuadPart);
}
static void net_sock_init(void) {
    static int done = 0;
    if (!done) { WSADATA d; WSAStartup(MAKEWORD(2, 2), &d); done = 1; }
}
static int net_close(net_sock_t s) { return closesocket(s); }
static int net_set_nonblock(net_sock_t s) { u_long m = 1; return ioctlsocket(s, FIONBIO, &m); }
static int net_would_block(void) { int e = WSAGetLastError(); return e == WSAEWOULDBLOCK || e == WSAEINTR; }
static int net_in_progress(void) { int e = WSAGetLastError(); return e == WSAEWOULDBLOCK || e == WSAEINPROGRESS; }
#  define NET_SEND_FLAGS 0
#  define net_ssize_t int
#else
#  include <sys/types.h>
#  include <sys/socket.h>
#  include <sys/select.h>
#  include <netdb.h>
#  include <unistd.h>
#  include <fcntl.h>
#  include <netinet/in.h>
#  include <netinet/tcp.h>
#  include <arpa/inet.h>
#  include <pthread.h>
#  include <signal.h>
#  include <time.h>
typedef int net_sock_t;
#  define NET_INVALID (-1)
typedef pthread_mutex_t net_mutex_t;
typedef pthread_cond_t net_cond_t;
static void net_mutex_init(net_mutex_t *m) { pthread_mutex_init(m, NULL); }
static void net_mutex_lock(net_mutex_t *m) { pthread_mutex_lock(m); }
static void net_mutex_unlock(net_mutex_t *m) { pthread_mutex_unlock(m); }
static void net_cond_init(net_cond_t *c) { pthread_cond_init(c, NULL); }
static void net_cond_signal(net_cond_t *c) { pthread_cond_signal(c); }
static void net_cond_wait_ms(net_cond_t *c, net_mutex_t *m, int ms) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += ms / 1000; ts.tv_nsec += (long)(ms % 1000) * 1000000L;
    if (ts.tv_nsec >= 1000000000L) { ts.tv_sec++; ts.tv_nsec -= 1000000000L; }
    pthread_cond_timedwait(c, m, &ts);
}
static int net_thread_start(void *(*fn)(void *), void *arg) {
    pthread_t t; pthread_attr_t at;
    pthread_attr_init(&at);
    pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
    pthread_attr_setstacksize(&at, (size_t)2 << 20);
    int rc = pthread_create(&t, &at, fn, arg);
    pthread_attr_destroy(&at);
    return rc == 0;
}
static void net_sleep_ms(int ms) { struct timespec ts = { ms / 1000, (long)(ms % 1000) * 1000000L }; nanosleep(&ts, NULL); }
static int64_t net_now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}
static void net_sock_init(void) { }
static int net_close(net_sock_t s) { return close(s); }
static int net_set_nonblock(net_sock_t s) { return fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK); }
static int net_would_block(void) { return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR; }
static int net_in_progress(void) { return errno == EINPROGRESS; }
#  ifdef MSG_NOSIGNAL
#    define NET_SEND_FLAGS MSG_NOSIGNAL
#  else
#    define NET_SEND_FLAGS 0
#  endif
#  define net_ssize_t ssize_t
#endif

/* ---------------------------------------------------------------- TLS (wspólny kontekst) */
#ifdef SILVER_TLS
static SSL_CTX *g_net_tls_ctx = NULL;
static net_mutex_t g_net_tls_lock;
static int g_net_tls_lock_init = 0;
static SSL_CTX *net_tls_ctx(void) {
    if (!g_net_tls_lock_init) { net_mutex_init(&g_net_tls_lock); g_net_tls_lock_init = 1; }
    net_mutex_lock(&g_net_tls_lock);
    if (!g_net_tls_ctx) {
        SSL_library_init();
        SSL_CTX *c = SSL_CTX_new(TLS_client_method());
        if (c) {
            SSL_CTX_set_min_proto_version(c, TLS1_2_VERSION);
            SSL_CTX_set_verify(c, SSL_VERIFY_PEER, NULL);
            const char *env_ca = getenv("SSL_CERT_FILE");
            int ok = 0;
            if (env_ca && env_ca[0]) ok = SSL_CTX_load_verify_locations(c, env_ca, NULL);
            if (!ok) ok = SSL_CTX_set_default_verify_paths(c);
            static const char *bundles[] = {
                "/etc/ssl/certs/ca-certificates.crt", "/etc/pki/tls/certs/ca-bundle.crt",
                "/etc/ssl/ca-bundle.pem", "/etc/ssl/cert.pem", "/usr/local/share/certs/ca-root-nss.crt",
                "/opt/homebrew/etc/ca-certificates/cert.pem", "/usr/local/etc/openssl@3/cert.pem",
                "/usr/local/etc/openssl/cert.pem", "C:/msys64/usr/ssl/certs/ca-bundle.crt",
                "C:/msys64/mingw64/etc/ssl/certs/ca-bundle.crt", "C:/Program Files/Git/mingw64/etc/ssl/certs/ca-bundle.crt", NULL };
            for (int i = 0; bundles[i]; i++) SSL_CTX_load_verify_locations(c, bundles[i], NULL);
            g_net_tls_ctx = c;
        }
    }
    SSL_CTX *r = g_net_tls_ctx;
    net_mutex_unlock(&g_net_tls_lock);
    return r;
}
#endif

/* ---------------------------------------------------------------- adres URL */
typedef struct {
    int https;
    char host[256];
    char port[8];
    char *path;                 /* malloc: ścieżka + zapytanie, zawsze zaczyna się od '/' */
} NetUrl;

static void net_url_free(NetUrl *u) { free(u->path); u->path = NULL; }

static int net_url_parse(const char *url, NetUrl *u) {
    memset(u, 0, sizeof *u);
    const char *p;
    if (strncasecmp(url, "https://", 8) == 0) { u->https = 1; p = url + 8; }
    else if (strncasecmp(url, "http://", 7) == 0) { u->https = 0; p = url + 7; }
    else return 0;
    const char *end = p;
    while (*end && *end != '/' && *end != '?' && *end != '#') end++;
    const char *at = NULL;
    for (const char *q = p; q < end; q++) if (*q == '@') at = q;
    if (at) p = at + 1;
    size_t hl = (size_t)(end - p);
    if (hl == 0 || hl >= sizeof u->host) return 0;
    char hp[256]; memcpy(hp, p, hl); hp[hl] = 0;
    snprintf(u->port, sizeof u->port, "%s", u->https ? "443" : "80");
    if (hp[0] == '[') {
        char *rb = strchr(hp, ']');
        if (!rb) return 0;
        *rb = 0;
        snprintf(u->host, sizeof u->host, "%s", hp + 1);
        if (rb[1] == ':' && rb[2]) snprintf(u->port, sizeof u->port, "%s", rb + 2);
    } else {
        char *colon = strrchr(hp, ':');
        if (colon) { *colon = 0; if (colon[1]) snprintf(u->port, sizeof u->port, "%s", colon + 1); }
        snprintf(u->host, sizeof u->host, "%s", hp);
    }
    const char *frag = strchr(end, '#');
    size_t pl = frag ? (size_t)(frag - end) : strlen(end);
    u->path = (char *)malloc(pl + 2);
    if (!u->path) return 0;
    if (pl == 0 || end[0] == '?') { u->path[0] = '/'; memcpy(u->path + 1, end, pl); u->path[pl + 1] = 0; }
    else { memcpy(u->path, end, pl); u->path[pl] = 0; }
    return 1;
}

/* usuwa segmenty "." i ".." ze ścieżki w adresie (zostawia schemat, host, zapytanie i fragment) */
static void net_url_dotseg(char *url) {
    char *p = strstr(url, "://");
    if (!p) return;
    p = strchr(p + 3, '/');
    if (!p) return;
    char *q = p + strcspn(p, "?#");
    size_t tail_len = strlen(q);
    char *tail = (char *)malloc(tail_len + 1);
    if (!tail) return;
    memcpy(tail, q, tail_len + 1);
    *q = 0;
    char *out = p; size_t o = 0;
    const char *seg = p;
    while (*seg) {
        const char *e = seg + 1;                 /* seg wskazuje na '/' */
        while (*e && *e != '/') e++;
        size_t sl = (size_t)(e - seg) - 1;
        if (sl == 1 && seg[1] == '.') { if (!*e) out[o++] = '/'; }
        else if (sl == 2 && seg[1] == '.' && seg[2] == '.') {
            while (o > 0 && out[o - 1] != '/') o--;
            if (o > 0) o--;                       /* usuń poprzedni segment razem z '/' */
            if (!*e) out[o++] = '/';
        } else { memmove(out + o, seg, sl + 1); o += sl + 1; }
        seg = e;
    }
    if (o == 0) out[o++] = '/';
    out[o] = 0;
    memcpy(out + o, tail, tail_len + 1);
    free(tail);
}

/* absolutny adres z nagłówka Location (względem bazowego) */
static char *net_url_join(const NetUrl *b, const char *loc) {
    size_t cap = strlen(loc) + strlen(b->host) + strlen(b->path) + 64;
    char *o = (char *)malloc(cap);
    if (!o) return NULL;
    const char *scheme = b->https ? "https" : "http";
    int defport = b->https ? 443 : 80;
    char hostport[300];
    int ipv6 = strchr(b->host, ':') != NULL;
    if (atoi(b->port) == defport) snprintf(hostport, sizeof hostport, ipv6 ? "[%s]" : "%s", b->host);
    else snprintf(hostport, sizeof hostport, ipv6 ? "[%s]:%s" : "%s:%s", b->host, b->port);
    if (strncasecmp(loc, "http://", 7) == 0 || strncasecmp(loc, "https://", 8) == 0) snprintf(o, cap, "%s", loc);
    else if (loc[0] == '/' && loc[1] == '/') snprintf(o, cap, "%s:%s", scheme, loc);
    else if (loc[0] == '/') snprintf(o, cap, "%s://%s%s", scheme, hostport, loc);
    else if (loc[0] == '?') {
        char *q = strchr(b->path, '?'); size_t pl = q ? (size_t)(q - b->path) : strlen(b->path);
        snprintf(o, cap, "%s://%s%.*s%s", scheme, hostport, (int)pl, b->path, loc);
    } else {
        char *q = strchr(b->path, '?'); size_t pl = q ? (size_t)(q - b->path) : strlen(b->path);
        const char *slash = NULL;
        for (size_t i = 0; i < pl; i++) if (b->path[i] == '/') slash = b->path + i;
        size_t dl = slash ? (size_t)(slash - b->path) + 1 : 1;
        snprintf(o, cap, "%s://%s%.*s%s", scheme, hostport, (int)dl, b->path, loc);
    }
    net_url_dotseg(o);
    return o;
}

/* ---------------------------------------------------------------- połączenie */
typedef struct {
    net_sock_t s;
#ifdef SILVER_TLS
    SSL *ssl;
#endif
    int64_t deadline;           /* bezwzględny koniec całego żądania (ms, monotonic) */
    volatile int *cancel;
} NetConn;

#define NET_ERR_TIMEOUT (-3)
#define NET_ERR_CANCEL  (-2)

static int net_wait_fd(net_sock_t s, int for_write, int ms) {
    fd_set fs; FD_ZERO(&fs);
#ifdef _WIN32
    FD_SET(s, &fs);
#else
    if (s >= FD_SETSIZE) { net_sleep_ms(5); return 1; }
    FD_SET(s, &fs);
#endif
    struct timeval tv; tv.tv_sec = ms / 1000; tv.tv_usec = (ms % 1000) * 1000;
    return select((int)s + 1, for_write ? NULL : &fs, for_write ? &fs : NULL, NULL, &tv);
}

/* 1 = można próbować dalej, 0 = koniec czasu, -1 = anulowano */
static int net_wait_step(NetConn *c, int for_write) {
    if (c->cancel && *c->cancel) return -1;
    int64_t left = c->deadline - net_now_ms();
    if (left <= 0) return 0;
    net_wait_fd(c->s, for_write, left > 100 ? 100 : (int)left);
    return 1;
}

static int conn_read(NetConn *c, unsigned char *buf, size_t n) {   /* >0 bajty, 0 EOF, <0 błąd */
    for (;;) {
#ifdef SILVER_TLS
        if (c->ssl) {
            int r = SSL_read(c->ssl, buf, (int)(n > 65536 ? 65536 : n));
            if (r > 0) return r;
            int e = SSL_get_error(c->ssl, r);
            if (e == SSL_ERROR_ZERO_RETURN) return 0;
            if (e == SSL_ERROR_WANT_READ || e == SSL_ERROR_WANT_WRITE) {
                int w = net_wait_step(c, e == SSL_ERROR_WANT_WRITE);
                if (w == 1) continue;
                return w == 0 ? NET_ERR_TIMEOUT : NET_ERR_CANCEL;
            }
            if (e == SSL_ERROR_SYSCALL && r == 0) return 0;      /* zamknięcie bez close_notify */
            return -1;
        }
#endif
        net_ssize_t r = recv(c->s, (char *)buf, (int)(n > 65536 ? 65536 : n), 0);
        if (r > 0) return (int)r;
        if (r == 0) return 0;
        if (net_would_block()) {
            int w = net_wait_step(c, 0);
            if (w == 1) continue;
            return w == 0 ? NET_ERR_TIMEOUT : NET_ERR_CANCEL;
        }
        return -1;
    }
}

static int conn_write_all(NetConn *c, const unsigned char *d, size_t n) {   /* 1 = ok */
    size_t off = 0;
    while (off < n) {
#ifdef SILVER_TLS
        if (c->ssl) {
            int r = SSL_write(c->ssl, d + off, (int)((n - off) > 65536 ? 65536 : (n - off)));
            if (r > 0) { off += (size_t)r; continue; }
            int e = SSL_get_error(c->ssl, r);
            if (e == SSL_ERROR_WANT_READ || e == SSL_ERROR_WANT_WRITE) {
                if (net_wait_step(c, e == SSL_ERROR_WANT_WRITE) == 1) continue;
            }
            return 0;
        }
#endif
        net_ssize_t r = send(c->s, (const char *)d + off, (int)((n - off) > 65536 ? 65536 : (n - off)), NET_SEND_FLAGS);
        if (r > 0) { off += (size_t)r; continue; }
        if (r < 0 && net_would_block()) { if (net_wait_step(c, 1) == 1) continue; }
        return 0;
    }
    return 1;
}

static void conn_close(NetConn *c) {
#ifdef SILVER_TLS
    if (c->ssl) { SSL_shutdown(c->ssl); SSL_free(c->ssl); c->ssl = NULL; }
#endif
    if (c->s != NET_INVALID) { net_close(c->s); c->s = NET_INVALID; }
}

static int net_is_ip_literal(const char *h) {
    struct in_addr a4; struct in6_addr a6;
    return inet_pton(AF_INET, h, &a4) == 1 || inet_pton(AF_INET6, h, &a6) == 1;
}

static int conn_open(NetConn *c, const NetUrl *u, char *err, size_t errn) {
    net_sock_init();
    c->s = NET_INVALID;
#ifdef SILVER_TLS
    c->ssl = NULL;
#endif
    struct addrinfo hints, *res = NULL, *ai;
    memset(&hints, 0, sizeof hints);
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(u->host, u->port, &hints, &res) != 0 || !res) {
        snprintf(err, errn, "nie można rozwiązać nazwy hosta: %.150s", u->host);
        return 0;
    }
    int ok = 0;
    for (ai = res; ai && !ok; ai = ai->ai_next) {
        net_sock_t s = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (s == NET_INVALID) continue;
        net_set_nonblock(s);
        int one = 1; setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char *)&one, sizeof one);
#if defined(SO_NOSIGPIPE)
        setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, (const char *)&one, sizeof one);
#endif
        int cr = connect(s, ai->ai_addr, (int)ai->ai_addrlen);
        if (cr != 0 && !net_in_progress()) { net_close(s); continue; }
        c->s = s;
        if (cr != 0) {
            int done = 0;
            while (!done) {
                int w = net_wait_step(c, 1);
                if (w != 1) { done = -1; break; }
                fd_set wf; FD_ZERO(&wf); FD_SET(s, &wf);
                struct timeval tv = { 0, 0 };
                if (select((int)s + 1, NULL, &wf, NULL, &tv) > 0) done = 1;
            }
            if (done == 1) {
                int e = 0; socklen_t el = sizeof e;
                getsockopt(s, SOL_SOCKET, SO_ERROR, (char *)&e, &el);
                if (e) done = -1;
            }
            if (done != 1) { net_close(s); c->s = NET_INVALID; continue; }
        }
        ok = 1;
    }
    freeaddrinfo(res);
    if (!ok) { snprintf(err, errn, "nie można połączyć z %.100s:%.8s", u->host, u->port); return 0; }
    if (u->https) {
#ifdef SILVER_TLS
        SSL_CTX *tc = net_tls_ctx();
        SSL *ssl = tc ? SSL_new(tc) : NULL;
        if (!ssl) { snprintf(err, errn, "TLS: brak kontekstu"); conn_close(c); return 0; }
        SSL_set_fd(ssl, (int)c->s);
        if (net_is_ip_literal(u->host)) X509_VERIFY_PARAM_set1_ip_asc(SSL_get0_param(ssl), u->host);
        else { SSL_set_tlsext_host_name(ssl, u->host); SSL_set1_host(ssl, u->host); }
        SSL_set_connect_state(ssl);
        c->ssl = ssl;
        for (;;) {
            int hr = SSL_do_handshake(ssl);
            if (hr == 1) break;
            int e = SSL_get_error(ssl, hr);
            if (e == SSL_ERROR_WANT_READ || e == SSL_ERROR_WANT_WRITE) {
                int w = net_wait_step(c, e == SSL_ERROR_WANT_WRITE);
                if (w == 1) continue;
                snprintf(err, errn, w == 0 ? "TLS: przekroczono czas" : "anulowano");
            } else {
                long vr = SSL_get_verify_result(ssl);
                if (vr != X509_V_OK) snprintf(err, errn, "TLS: certyfikat odrzucony (%s)", X509_verify_cert_error_string(vr));
                else snprintf(err, errn, "TLS: błąd uścisku dłoni");
            }
            conn_close(c);
            return 0;
        }
#else
        snprintf(err, errn, "https:// wymaga budowy z OpenSSL (SILVER_TLS=1)");
        conn_close(c);
        return 0;
#endif
    }
    return 1;
}

/* ---------------------------------------------------------------- klient HTTP */
typedef struct {
    int status;
    char status_text[64];
    char *headers;              /* "nazwa: wartość\n" (nazwy małymi literami) */
    unsigned char *body; size_t body_len;
    char *final_url; int redirected;
    char err[220];
} NetResp;

static void net_resp_free(NetResp *r) {
    free(r->headers); free(r->body); free(r->final_url);
    memset(r, 0, sizeof *r);
}

#define NET_MAX_BODY ((size_t)256u * 1024u * 1024u)
#define NET_MAX_HEAD 65536

typedef struct { NetConn *c; unsigned char b[16384]; size_t pos, lim; int err; } NetRd;

static int rd_byte(NetRd *r) {            /* 0..255, -1 EOF, -2 błąd */
    if (r->pos >= r->lim) {
        int n = conn_read(r->c, r->b, sizeof r->b);
        if (n == 0) return -1;
        if (n < 0) { r->err = n; return -2; }
        r->pos = 0; r->lim = (size_t)n;
    }
    return r->b[r->pos++];
}

static int rd_line(NetRd *r, char *out, size_t cap) {   /* 1 = linia, 0 = EOF, -1 = błąd */
    size_t n = 0;
    for (;;) {
        int ch = rd_byte(r);
        if (ch == -1) return n ? 1 : 0;
        if (ch == -2) return -1;
        if (ch == '\n') { while (n && out[n - 1] == '\r') n--; out[n] = 0; return 1; }
        if (n + 1 < cap) out[n++] = (char)ch;
        else return -1;
    }
}

static int rd_exact(NetRd *r, unsigned char *dst, size_t n) {   /* 1 = ok */
    size_t got = 0;
    while (got < n) {
        if (r->pos >= r->lim) {
            int k = conn_read(r->c, r->b, sizeof r->b);
            if (k <= 0) { if (k < 0) r->err = k; return 0; }
            r->pos = 0; r->lim = (size_t)k;
        }
        size_t take = r->lim - r->pos; if (take > n - got) take = n - got;
        memcpy(dst + got, r->b + r->pos, take); r->pos += take; got += take;
    }
    return 1;
}

typedef struct { unsigned char *p; size_t len, cap; } NetBuf;
static int nb_add(NetBuf *b, const void *d, size_t n) {
    if (b->len + n + 1 > b->cap) {
        size_t nc = b->cap ? b->cap : 4096;
        while (nc < b->len + n + 1) nc *= 2;
        unsigned char *np = (unsigned char *)realloc(b->p, nc);
        if (!np) return 0;
        b->p = np; b->cap = nc;
    }
    if (n && d) memcpy(b->p + b->len, d, n);
    b->len += n; b->p[b->len] = 0;
    return 1;
}

static char *net_lower_dup(const char *s, size_t n) {
    char *o = (char *)malloc(n + 1);
    if (!o) return NULL;
    for (size_t i = 0; i < n; i++) o[i] = (char)tolower((unsigned char)s[i]);
    o[n] = 0;
    return o;
}

static const char *net_hdr_get(const char *hdrs, const char *name) {   /* wskaźnik na wartość w "hdrs" (nie kończy się \0) */
    size_t nl = strlen(name);
    const char *p = hdrs;
    while (p && *p) {
        if (strncasecmp(p, name, nl) == 0 && p[nl] == ':') {
            p += nl + 1;
            while (*p == ' ' || *p == '\t') p++;
            return p;
        }
        const char *nx = strchr(p, '\n');
        p = nx ? nx + 1 : NULL;
    }
    return NULL;
}

static int net_hdr_has_name(const char *hdrs, const char *name) { return net_hdr_get(hdrs ? hdrs : "", name) != NULL; }

/* jedno żądanie bez przekierowań; 1 = odpowiedź odebrana (nawet 4xx/5xx) */
static int net_http_once(const char *method, const NetUrl *u, const char *req_headers,
                         const unsigned char *body, size_t blen, int64_t deadline,
                         volatile int *cancel, NetResp *out) {
    NetConn c; memset(&c, 0, sizeof c);
    c.deadline = deadline; c.cancel = cancel;
    if (!conn_open(&c, u, out->err, sizeof out->err)) return 0;

    NetBuf rq; memset(&rq, 0, sizeof rq);
    char line[1024];
    snprintf(line, sizeof line, "%s %s HTTP/1.1\r\n", method, u->path);
    nb_add(&rq, line, strlen(line));
    if (!net_hdr_has_name(req_headers, "host")) {
        int defport = u->https ? 443 : 80;
        int ipv6 = strchr(u->host, ':') != NULL;
        if (atoi(u->port) == defport) snprintf(line, sizeof line, ipv6 ? "Host: [%s]\r\n" : "Host: %s\r\n", u->host);
        else snprintf(line, sizeof line, ipv6 ? "Host: [%s]:%s\r\n" : "Host: %s:%s\r\n", u->host, u->port);
        nb_add(&rq, line, strlen(line));
    }
    if (!net_hdr_has_name(req_headers, "user-agent")) nb_add(&rq, "User-Agent: Silver/0.5\r\n", 24);
    if (!net_hdr_has_name(req_headers, "accept")) nb_add(&rq, "Accept: */*\r\n", 13);
    nb_add(&rq, "Accept-Encoding: identity\r\nConnection: close\r\n", 46);
    if (req_headers) {                         /* "Nazwa: wartość\n" — bez CR/LF wewnątrz wartości */
        const char *p = req_headers;
        while (*p) {
            const char *nl = strchr(p, '\n');
            size_t ln = nl ? (size_t)(nl - p) : strlen(p);
            while (ln && (p[ln - 1] == '\r')) ln--;
            if (ln > 0 && memchr(p, ':', ln)) {
                const char *colon = (const char *)memchr(p, ':', ln);
                size_t nn = (size_t)(colon - p);
                char *lname = net_lower_dup(p, nn);
                int skip = !lname;
                if (lname && (strcmp(lname, "content-length") == 0 || strcmp(lname, "connection") == 0 ||
                              strcmp(lname, "accept-encoding") == 0 || strcmp(lname, "transfer-encoding") == 0)) skip = 1;
                free(lname);
                if (!skip) { nb_add(&rq, p, ln); nb_add(&rq, "\r\n", 2); }
            }
            if (!nl) break;
            p = nl + 1;
        }
    }
    if (body && blen) { snprintf(line, sizeof line, "Content-Length: %zu\r\n", blen); nb_add(&rq, line, strlen(line)); }
    else if (strcmp(method, "POST") == 0 || strcmp(method, "PUT") == 0 || strcmp(method, "PATCH") == 0) nb_add(&rq, "Content-Length: 0\r\n", 19);
    nb_add(&rq, "\r\n", 2);
    if (body && blen) nb_add(&rq, body, blen);
    int sent = conn_write_all(&c, rq.p, rq.len);
    free(rq.p);
    if (!sent) { snprintf(out->err, sizeof out->err, "błąd wysyłania żądania"); conn_close(&c); return 0; }

    NetRd *rd = (NetRd *)calloc(1, sizeof *rd);
    if (!rd) { conn_close(&c); snprintf(out->err, sizeof out->err, "brak pamięci"); return 0; }
    rd->c = &c;
    char hl[8192];
    int status = 0;
    NetBuf hb; memset(&hb, 0, sizeof hb);
    for (;;) {                                  /* pomiń odpowiedzi 1xx */
        int r = rd_line(rd, hl, sizeof hl);
        if (r <= 0) { snprintf(out->err, sizeof out->err, r == 0 ? "serwer zamknął połączenie bez odpowiedzi" : "błąd odczytu nagłówków (%d)", rd->err); goto fail; }
        if (strncmp(hl, "HTTP/", 5) != 0) { snprintf(out->err, sizeof out->err, "nieprawidłowa odpowiedź HTTP"); goto fail; }
        const char *sp = strchr(hl, ' ');
        status = sp ? atoi(sp + 1) : 0;
        if (sp) { const char *sp2 = strchr(sp + 1, ' '); snprintf(out->status_text, sizeof out->status_text, "%s", sp2 ? sp2 + 1 : ""); }
        hb.len = 0;
        size_t total = 0;
        for (;;) {
            r = rd_line(rd, hl, sizeof hl);
            if (r <= 0) { snprintf(out->err, sizeof out->err, "ucięte nagłówki odpowiedzi"); goto fail; }
            if (hl[0] == 0) break;
            total += strlen(hl);
            if (total > NET_MAX_HEAD) { snprintf(out->err, sizeof out->err, "nagłówki odpowiedzi zbyt duże"); goto fail; }
            char *colon = strchr(hl, ':');
            if (!colon) continue;
            char *ln = net_lower_dup(hl, (size_t)(colon - hl));
            if (!ln) goto fail;
            const char *val = colon + 1;
            while (*val == ' ' || *val == '\t') val++;
            nb_add(&hb, ln, strlen(ln)); nb_add(&hb, ": ", 2); nb_add(&hb, val, strlen(val)); nb_add(&hb, "\n", 1);
            free(ln);
        }
        if (status >= 100 && status < 200 && status != 101) continue;
        break;
    }
    out->status = status;
    out->headers = hb.p ? (char *)hb.p : strdup("");
    hb.p = NULL;
    {
        int head_only = strcmp(method, "HEAD") == 0 || status == 204 || status == 304 || (status >= 100 && status < 200);
        const char *te = net_hdr_get(out->headers, "transfer-encoding");
        const char *cl = net_hdr_get(out->headers, "content-length");
        NetBuf bb; memset(&bb, 0, sizeof bb);
        if (head_only) {
            /* bez treści */
        } else if (te && strncasecmp(te, "chunked", 7) == 0) {
            for (;;) {
                int r = rd_line(rd, hl, sizeof hl);
                if (r <= 0) { snprintf(out->err, sizeof out->err, "ucięty strumień chunked"); free(bb.p); goto fail; }
                size_t csz = (size_t)strtoull(hl, NULL, 16);
                if (csz == 0) {                  /* trailery */
                    while ((r = rd_line(rd, hl, sizeof hl)) > 0 && hl[0]) { }
                    break;
                }
                if (bb.len + csz > NET_MAX_BODY) { snprintf(out->err, sizeof out->err, "odpowiedź przekracza limit rozmiaru"); free(bb.p); goto fail; }
                size_t at = bb.len;
                if (!nb_add(&bb, NULL, csz) ) { free(bb.p); goto fail; }
                if (!rd_exact(rd, bb.p + at, csz)) { snprintf(out->err, sizeof out->err, "ucięty fragment chunked"); free(bb.p); goto fail; }
                rd_line(rd, hl, sizeof hl);         /* CRLF po fragmencie */
            }
        } else if (cl) {
            size_t n = (size_t)strtoull(cl, NULL, 10);
            if (n > NET_MAX_BODY) { snprintf(out->err, sizeof out->err, "odpowiedź przekracza limit rozmiaru"); goto fail; }
            if (n) {
                if (!nb_add(&bb, NULL, n) || !rd_exact(rd, bb.p, n)) { snprintf(out->err, sizeof out->err, "ucięta treść odpowiedzi"); free(bb.p); goto fail; }
            }
        } else {
            unsigned char tmp[16384];
            if (rd->lim > rd->pos) nb_add(&bb, rd->b + rd->pos, rd->lim - rd->pos);
            rd->pos = rd->lim = 0;
            for (;;) {
                int k = conn_read(&c, tmp, sizeof tmp);
                if (k == 0) break;
                if (k < 0) { snprintf(out->err, sizeof out->err, k == NET_ERR_TIMEOUT ? "przekroczono czas odczytu" : "błąd odczytu"); free(bb.p); goto fail; }
                if (bb.len + (size_t)k > NET_MAX_BODY) { snprintf(out->err, sizeof out->err, "odpowiedź przekracza limit rozmiaru"); free(bb.p); goto fail; }
                nb_add(&bb, tmp, (size_t)k);
            }
        }
        out->body = bb.p; out->body_len = bb.len;
    }
    free(rd); conn_close(&c);
    return 1;
fail:
    free(hb.p); free(rd); conn_close(&c);
    if (!out->err[0]) snprintf(out->err, sizeof out->err, "błąd sieci");
    return 0;
}

/* pełne żądanie z przekierowaniami. 0 = ok (odpowiedź w *out), -1 = błąd (out->err) */
static int net_http_request(const char *method, const char *url, const char *headers,
                            const unsigned char *body, size_t blen, int timeout_ms,
                            int max_redirects, volatile int *cancel, NetResp *out) {
    memset(out, 0, sizeof *out);
    if (timeout_ms <= 0) timeout_ms = 30000;
    int64_t deadline = net_now_ms() + timeout_ms;
    char meth[16]; snprintf(meth, sizeof meth, "%s", method && method[0] ? method : "GET");
    for (char *p = meth; *p; p++) *p = (char)toupper((unsigned char)*p);
    char *cur = strdup(url);
    char *hdrs = headers ? strdup(headers) : strdup("");
    unsigned char *bd = NULL; size_t bl = 0;
    if (body && blen) { bd = (unsigned char *)malloc(blen); if (bd) { memcpy(bd, body, blen); bl = blen; } }
    int rc = -1;
    for (int hop = 0; hop <= max_redirects + 1; hop++) {
        NetUrl u;
        if (!cur || !net_url_parse(cur, &u)) { snprintf(out->err, sizeof out->err, "nieprawidłowy adres URL"); break; }
        NetResp r; memset(&r, 0, sizeof r);
        int ok = net_http_once(meth, &u, hdrs, bd, bl, deadline, cancel, &r);
        if (!ok) { snprintf(out->err, sizeof out->err, "%s", r.err); net_resp_free(&r); net_url_free(&u); break; }
        int st = r.status;
        const char *loc = (st == 301 || st == 302 || st == 303 || st == 307 || st == 308) ? net_hdr_get(r.headers, "location") : NULL;
        if (loc && max_redirects > 0 && hop < max_redirects) {
            size_t ll = strcspn(loc, "\n");
            char *lc = (char *)malloc(ll + 1);
            if (lc) {
                memcpy(lc, loc, ll); lc[ll] = 0;
                char *next = net_url_join(&u, lc);
                free(lc);
                if (next) {
                    NetUrl nu; int same_origin = 0;
                    if (net_url_parse(next, &nu)) {
                        same_origin = nu.https == u.https && strcasecmp(nu.host, u.host) == 0 && strcmp(nu.port, u.port) == 0;
                        net_url_free(&nu);
                    }
                    if (st == 303 || ((st == 301 || st == 302) && strcmp(meth, "POST") == 0)) {
                        if (strcmp(meth, "HEAD") != 0) snprintf(meth, sizeof meth, "GET");
                        free(bd); bd = NULL; bl = 0;
                    }
                    if (!same_origin) {          /* jak w fetch(): nie wysyłamy poświadczeń dalej */
                        NetBuf nb; memset(&nb, 0, sizeof nb);
                        const char *p = hdrs;
                        while (*p) {
                            const char *nl = strchr(p, '\n'); size_t ln = nl ? (size_t)(nl - p) : strlen(p);
                            if (!(ln >= 14 && strncasecmp(p, "authorization:", 14) == 0) && !(ln >= 7 && strncasecmp(p, "cookie:", 7) == 0)) {
                                nb_add(&nb, p, ln); nb_add(&nb, "\n", 1);
                            }
                            if (!nl) break;
                            p = nl + 1;
                        }
                        free(hdrs); hdrs = nb.p ? (char *)nb.p : strdup("");
                    }
                    free(cur); cur = next;
                    out->redirected = 1;
                    net_resp_free(&r); net_url_free(&u);
                    continue;
                }
            }
        }
        /* odpowiedź końcowa */
        out->status = r.status; memcpy(out->status_text, r.status_text, sizeof out->status_text);
        out->headers = r.headers; out->body = r.body; out->body_len = r.body_len;
        out->final_url = cur; cur = NULL;
        net_url_free(&u);
        rc = 0;
        break;
    }
    if (rc != 0 && !out->err[0]) snprintf(out->err, sizeof out->err, "zbyt wiele przekierowań");
    free(cur); free(hdrs); free(bd);
    return rc;
}

/* ---------------------------------------------------------------- zadania asynchroniczne */
typedef struct NetJob {
    struct NetJob *next;
    uint64_t gen;               /* generacja kontekstu JS, który zlecił zadanie */
    int id;
    char *method, *url, *headers;
    unsigned char *body; size_t blen;
    int timeout_ms, max_redirects;
    volatile int cancelled;
    int done, orphan;           /* chronione muteksem g_net_lock */
    int rc;
    NetResp resp;
} NetJob;

static NetJob *g_net_jobs = NULL;
static net_mutex_t g_net_lock;
static int g_net_lock_ready = 0;
static void net_lock_ensure(void) {
    /* wołane z wątku głównego przed startem pierwszego zadania */
    if (!g_net_lock_ready) { net_mutex_init(&g_net_lock); g_net_lock_ready = 1; }
}

static void net_job_free(NetJob *j) {
    free(j->method); free(j->url); free(j->headers); free(j->body);
    net_resp_free(&j->resp);
    free(j);
}

static void *net_job_thread(void *arg) {
    NetJob *j = (NetJob *)arg;
    j->rc = net_http_request(j->method, j->url, j->headers, j->body, j->blen, j->timeout_ms, j->max_redirects, &j->cancelled, &j->resp);
    net_mutex_lock(&g_net_lock);
    if (j->orphan) {                       /* nikt już nie czeka na wynik */
        NetJob **pp = &g_net_jobs;
        while (*pp && *pp != j) pp = &(*pp)->next;
        if (*pp) *pp = j->next;
        net_mutex_unlock(&g_net_lock);
        net_job_free(j);
        return NULL;
    }
    j->done = 1;
    net_mutex_unlock(&g_net_lock);
    return NULL;
}

/* 1 = zadanie wystartowało */
static int net_job_start(uint64_t gen, int id, const char *method, const char *url, const char *headers,
                         const unsigned char *body, size_t blen, int timeout_ms, int max_redirects) {
    net_lock_ensure();
    NetJob *j = (NetJob *)calloc(1, sizeof *j);
    if (!j) return 0;
    j->gen = gen; j->id = id;
    j->method = strdup(method ? method : "GET"); j->url = strdup(url ? url : "");
    j->headers = strdup(headers ? headers : "");
    if (body && blen) { j->body = (unsigned char *)malloc(blen); if (j->body) { memcpy(j->body, body, blen); j->blen = blen; } }
    j->timeout_ms = timeout_ms; j->max_redirects = max_redirects;
    net_mutex_lock(&g_net_lock);
    j->next = g_net_jobs; g_net_jobs = j;
    net_mutex_unlock(&g_net_lock);
    if (!net_thread_start(net_job_thread, j)) {
        net_mutex_lock(&g_net_lock);
        NetJob **pp = &g_net_jobs;
        while (*pp && *pp != j) pp = &(*pp)->next;
        if (*pp) *pp = j->next;
        net_mutex_unlock(&g_net_lock);
        net_job_free(j);
        return 0;
    }
    return 1;
}

/* zdejmuje z kolejki jedno zakończone zadanie danej generacji (NULL gdy brak) */
static NetJob *net_job_take_done(uint64_t gen) {
    if (!g_net_lock_ready) return NULL;
    NetJob *res = NULL;
    net_mutex_lock(&g_net_lock);
    for (NetJob **pp = &g_net_jobs; *pp; pp = &(*pp)->next) {
        NetJob *j = *pp;
        if (j->gen == gen && j->done) { *pp = j->next; res = j; break; }
    }
    net_mutex_unlock(&g_net_lock);
    return res;
}

/* ile zadań danej generacji jeszcze trwa lub czeka na odbiór */
static int net_jobs_pending(uint64_t gen) {
    if (!g_net_lock_ready) return 0;
    int n = 0;
    net_mutex_lock(&g_net_lock);
    for (NetJob *j = g_net_jobs; j; j = j->next) if (j->gen == gen) n++;
    net_mutex_unlock(&g_net_lock);
    return n;
}

static void net_job_cancel(uint64_t gen, int id) {
    if (!g_net_lock_ready) return;
    net_mutex_lock(&g_net_lock);
    for (NetJob *j = g_net_jobs; j; j = j->next) if (j->gen == gen && j->id == id) j->cancelled = 1;
    net_mutex_unlock(&g_net_lock);
}

/* porzuca wszystkie zadania generacji (zamykanie kontekstu) */
static void net_jobs_discard(uint64_t gen) {
    if (!g_net_lock_ready) return;
    net_mutex_lock(&g_net_lock);
    NetJob **pp = &g_net_jobs;
    while (*pp) {
        NetJob *j = *pp;
        if (j->gen != gen) { pp = &j->next; continue; }
        if (j->done) { *pp = j->next; net_job_free(j); continue; }
        j->orphan = 1; j->cancelled = 1;
        pp = &j->next;
    }
    net_mutex_unlock(&g_net_lock);
}

#endif /* SILVER_NET_H */
