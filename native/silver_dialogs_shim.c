#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SILVER_PATH_MAX 16384

static char g_result[SILVER_PATH_MAX];

#if defined(_WIN32)
#  define POPEN  _popen
#  define PCLOSE _pclose
#else
#  define POPEN  popen
#  define PCLOSE pclose
#endif

/* ---- cytowanie ---- */
static void append(char *dst, size_t cap, const char *s) {
    size_t n = strlen(dst);
    if (n + 1 >= cap) return;
    snprintf(dst + n, cap - n, "%s", s);
}

/* POSIX: 'a'\''b' */
static void append_sh(char *dst, size_t cap, const char *s) {
    append(dst, cap, "'");
    for (; s && *s; s++) {
        if (*s == '\'') append(dst, cap, "'\\''");
        else { char c[2] = { *s, 0 }; append(dst, cap, c); }
    }
    append(dst, cap, "'");
}

/* PowerShell / AppleScript: podwajanie apostrofu/cudzysłowu */
__attribute__((unused)) static void append_ps(char *dst, size_t cap, const char *s) {
    append(dst, cap, "'");
    for (; s && *s; s++) {
        if (*s == '\'') append(dst, cap, "''");
        else { char c[2] = { *s, 0 }; append(dst, cap, c); }
    }
    append(dst, cap, "'");
}

__attribute__((unused)) static void append_as(char *dst, size_t cap, const char *s) {
    append(dst, cap, "\"");
    for (; s && *s; s++) {
        if (*s == '"' || *s == '\\') append(dst, cap, "\\");
        if (*s == '\n') { append(dst, cap, "\\n"); continue; }
        char c[2] = { *s, 0 };
        append(dst, cap, c);
    }
    append(dst, cap, "\"");
}

static int have_tool(const char *name) {
#if defined(_WIN32)
    (void)name; return 0;
#else
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "command -v %s >/dev/null 2>&1", name);
    return system(cmd) == 0;
#endif
}

/* uruchamia `cmd`, wynik (bez końcowych \n) → g_result; zwraca kod wyjścia */
static int run(const char *cmd) {
    g_result[0] = '\0';
    FILE *p = POPEN(cmd, "r");
    if (!p) return -1;
    size_t n = fread(g_result, 1, SILVER_PATH_MAX - 1, p);
    g_result[n] = '\0';
    int rc = PCLOSE(p);
    while (n > 0 && (g_result[n - 1] == '\n' || g_result[n - 1] == '\r')) g_result[--n] = '\0';
    return rc;
}

/* "*.png;*.jpg" → "*.png *.jpg" (zenity) albo "png jpg" (inne) */
static void pattern_words(const char *pat, char *out, size_t cap, int strip_star, char sep) {
    out[0] = '\0';
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s", pat ? pat : "");
    char *tok = strtok(tmp, ";, ");
    while (tok) {
        if (strip_star) { if (tok[0] == '*' && tok[1] == '.') tok += 2; else if (tok[0] == '*') tok += 1; }
        size_t n = strlen(out);
        if (n) { out[n] = sep; out[n + 1] = '\0'; }
        append(out, cap, tok);
        tok = strtok(NULL, ";, ");
    }
}

/* ---- wspólna implementacja wyboru pliku/folderu ----
 * mode: 0 open file, 1 open files, 2 save file, 3 open folder */
static const char *pick(int mode, const char *title, const char *desc, const char *pattern,
                        const char *def_path) {
    char cmd[8192] = "";
    g_result[0] = '\0';
    title = title ? title : "";
#if defined(_WIN32)
    const char *cls = (mode == 2) ? "SaveFileDialog" : (mode == 3 ? "FolderBrowserDialog" : "OpenFileDialog");
    append(cmd, sizeof(cmd), "powershell -NoProfile -Command \"Add-Type -AssemblyName System.Windows.Forms; $d=New-Object System.Windows.Forms.");
    append(cmd, sizeof(cmd), cls);
    append(cmd, sizeof(cmd), ";");
    if (mode == 3) {
        append(cmd, sizeof(cmd), "$d.Description=");
        append_ps(cmd, sizeof(cmd), title);
        append(cmd, sizeof(cmd), "; if($d.ShowDialog() -eq 'OK'){$d.SelectedPath}\"");
    } else {
        append(cmd, sizeof(cmd), "$d.Title=");
        append_ps(cmd, sizeof(cmd), title);
        append(cmd, sizeof(cmd), ";");
        if (mode == 1) append(cmd, sizeof(cmd), "$d.Multiselect=$true;");
        if (pattern && pattern[0]) {
            char w[512]; pattern_words(pattern, w, sizeof(w), 0, ';');
            char f[1024]; snprintf(f, sizeof(f), "%s|%s", desc && desc[0] ? desc : "Pliki", w);
            append(cmd, sizeof(cmd), "$d.Filter=");
            append_ps(cmd, sizeof(cmd), f);
            append(cmd, sizeof(cmd), ";");
        }
        if (def_path && def_path[0]) {
            append(cmd, sizeof(cmd), mode == 2 ? "$d.FileName=" : "$d.InitialDirectory=");
            append_ps(cmd, sizeof(cmd), def_path);
            append(cmd, sizeof(cmd), ";");
        }
        append(cmd, sizeof(cmd), "if($d.ShowDialog() -eq 'OK'){ if($d.Multiselect){$d.FileNames -join \\\"`n\\\"}else{$d.FileName} }\"");
    }
    int rc = run(cmd);
    (void)rc;
    return g_result;
#elif defined(__APPLE__)
    {
        const char *verb = (mode == 3) ? "choose folder" : (mode == 2 ? "choose file name" : "choose file");
        char script[4096] = "";
        append(script, sizeof(script), "set r to (");
        append(script, sizeof(script), verb);
        append(script, sizeof(script), " with prompt ");
        append_as(script, sizeof(script), title);
        if (mode == 1) append(script, sizeof(script), " with multiple selections allowed");
        if (mode == 2 && def_path && def_path[0]) {
            const char *slash = strrchr(def_path, '/');
            append(script, sizeof(script), " default name ");
            append_as(script, sizeof(script), slash ? slash + 1 : def_path);
        }
        append(script, sizeof(script), ")\n");
        if (mode == 1)
            append(script, sizeof(script),
                   "set out to \"\"\nrepeat with f in r\nset out to out & POSIX path of f & linefeed\nend repeat\nreturn out");
        else
            append(script, sizeof(script), "return POSIX path of r");
        append(cmd, sizeof(cmd), "osascript -e ");
        append_sh(cmd, sizeof(cmd), script);
        append(cmd, sizeof(cmd), " 2>/dev/null");
        (void)desc; (void)pattern;
        run(cmd);
    }
    return g_result;
#else
    if (have_tool("zenity")) {
        append(cmd, sizeof(cmd), "zenity --file-selection --title=");
        append_sh(cmd, sizeof(cmd), title);
        if (mode == 1) append(cmd, sizeof(cmd), " --multiple --separator='\n'");
        if (mode == 2) append(cmd, sizeof(cmd), " --save --confirm-overwrite");
        if (mode == 3) append(cmd, sizeof(cmd), " --directory");
        if (def_path && def_path[0]) {
            append(cmd, sizeof(cmd), " --filename=");
            append_sh(cmd, sizeof(cmd), def_path);
        }
        if (pattern && pattern[0] && mode != 3) {
            char w[512]; pattern_words(pattern, w, sizeof(w), 0, ' ');
            char f[1024]; snprintf(f, sizeof(f), "%s | %s", desc && desc[0] ? desc : "Pliki", w);
            append(cmd, sizeof(cmd), " --file-filter=");
            append_sh(cmd, sizeof(cmd), f);
        }
        append(cmd, sizeof(cmd), " 2>/dev/null");
        run(cmd);
        return g_result;
    }
    if (have_tool("kdialog")) {
        const char *sub = mode == 2 ? "--getsavefilename" : (mode == 3 ? "--getexistingdirectory" : "--getopenfilename");
        append(cmd, sizeof(cmd), "kdialog --title ");
        append_sh(cmd, sizeof(cmd), title);
        append(cmd, sizeof(cmd), " ");
        append(cmd, sizeof(cmd), sub);
        append(cmd, sizeof(cmd), " ");
        append_sh(cmd, sizeof(cmd), def_path && def_path[0] ? def_path : ".");
        if (pattern && pattern[0] && mode != 3) {
            char w[512]; pattern_words(pattern, w, sizeof(w), 0, ' ');
            char f[1024]; snprintf(f, sizeof(f), "%s (%s)", desc && desc[0] ? desc : "Pliki", w);
            append(cmd, sizeof(cmd), " ");
            append_sh(cmd, sizeof(cmd), f);
        }
        if (mode == 1) append(cmd, sizeof(cmd), " --multiple --separate-output");
        append(cmd, sizeof(cmd), " 2>/dev/null");
        run(cmd);
        return g_result;
    }
    return g_result;
#endif
}

/* ---- API publiczne ---- */
const char *silver_dialog_open_file(const char *title, const char *filter_desc, const char *filter_pattern) {
    return pick(0, title, filter_desc, filter_pattern, "");
}
const char *silver_dialog_open_file_ex(const char *title, const char *filter_desc,
                                       const char *filter_pattern, const char *default_path) {
    return pick(0, title, filter_desc, filter_pattern, default_path);
}
/* wiele plików, rozdzielone '\n' */
const char *silver_dialog_open_files(const char *title, const char *filter_desc,
                                     const char *filter_pattern, const char *default_path) {
    return pick(1, title, filter_desc, filter_pattern, default_path);
}
const char *silver_dialog_save_file(const char *title, const char *default_name) {
    return pick(2, title, "", "", default_name);
}
const char *silver_dialog_save_file_ex(const char *title, const char *default_path,
                                       const char *filter_desc, const char *filter_pattern) {
    return pick(2, title, filter_desc, filter_pattern, default_path);
}
const char *silver_dialog_open_folder(const char *title) { return pick(3, title, "", "", ""); }
const char *silver_dialog_open_folder_ex(const char *title, const char *default_path) {
    return pick(3, title, "", "", default_path);
}

/* kind: "info" | "warning" | "error" | "question" — dla "question" true = Tak */
int silver_dialog_message(const char *title, const char *text, const char *kind) {
    char cmd[8192] = "";
    int question = kind && strcmp(kind, "question") == 0;
    title = title ? title : ""; text = text ? text : "";
#if defined(_WIN32)
    const char *btn = question ? "YesNo" : "OK";
    const char *icon = question ? "Question" : (kind && !strcmp(kind, "warning") ? "Warning" : (kind && !strcmp(kind, "error") ? "Error" : "Information"));
    append(cmd, sizeof(cmd), "powershell -NoProfile -Command \"Add-Type -AssemblyName System.Windows.Forms; [System.Windows.Forms.MessageBox]::Show(");
    append_ps(cmd, sizeof(cmd), text);
    append(cmd, sizeof(cmd), ",");
    append_ps(cmd, sizeof(cmd), title);
    append(cmd, sizeof(cmd), ",'");
    append(cmd, sizeof(cmd), btn);
    append(cmd, sizeof(cmd), "','");
    append(cmd, sizeof(cmd), icon);
    append(cmd, sizeof(cmd), "')\"");
    run(cmd);
    return !question || strstr(g_result, "Yes") != NULL;
#elif defined(__APPLE__)
    char script[4096] = "display dialog ";
    append_as(script, sizeof(script), text);
    append(script, sizeof(script), " with title ");
    append_as(script, sizeof(script), title);
    append(script, sizeof(script), question ? " buttons {\"Nie\",\"Tak\"} default button \"Tak\"" : " buttons {\"OK\"} default button \"OK\"");
    append(cmd, sizeof(cmd), "osascript -e ");
    append_sh(cmd, sizeof(cmd), script);
    append(cmd, sizeof(cmd), " 2>/dev/null");
    int rc = run(cmd);
    if (!question) return 1;
    return rc == 0 && strstr(g_result, "Tak") != NULL;
#else
    if (have_tool("zenity")) {
        const char *flag = question ? "--question" : (kind && !strcmp(kind, "warning") ? "--warning"
                         : (kind && !strcmp(kind, "error") ? "--error" : "--info"));
        append(cmd, sizeof(cmd), "zenity ");
        append(cmd, sizeof(cmd), flag);
        append(cmd, sizeof(cmd), " --no-markup --title=");
        append_sh(cmd, sizeof(cmd), title);
        append(cmd, sizeof(cmd), " --text=");
        append_sh(cmd, sizeof(cmd), text);
        append(cmd, sizeof(cmd), " 2>/dev/null");
        int rc = system(cmd);
        return question ? rc == 0 : 1;
    }
    if (have_tool("kdialog")) {
        const char *flag = question ? "--yesno" : (kind && !strcmp(kind, "warning") ? "--sorry"
                         : (kind && !strcmp(kind, "error") ? "--error" : "--msgbox"));
        append(cmd, sizeof(cmd), "kdialog --title ");
        append_sh(cmd, sizeof(cmd), title);
        append(cmd, sizeof(cmd), " ");
        append(cmd, sizeof(cmd), flag);
        append(cmd, sizeof(cmd), " ");
        append_sh(cmd, sizeof(cmd), text);
        append(cmd, sizeof(cmd), " 2>/dev/null");
        int rc = system(cmd);
        return question ? rc == 0 : 1;
    }
    fprintf(stderr, "[silver] %s: %s (brak zenity/kdialog)\n", title, text);
    return question ? 0 : 1;
#endif
}
