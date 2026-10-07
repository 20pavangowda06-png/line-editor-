/*
 * line-editor — a simple command-line line editor (Activity 7).
 *
 * The document is a dynamic array of heap-allocated strings (see DESIGN.md
 * for the data-structure rationale). All line numbers shown to the user are
 * 1-based; internally they are 0-based.
 *
 * Build:  gcc -Wall -Wextra -std=c11 -D_POSIX_C_SOURCE=200809L -o editor editor.c
 *         (or just `make`)
 * Run:    ./editor [file]
 */
#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Document: dynamic array of lines                                     */
/* ------------------------------------------------------------------ */

typedef struct {
    char **lines;   /* heap strings, no trailing '\n' stored */
    int count;
    int capacity;
} Document;

static void doc_init(Document *d)
{
    d->lines = NULL;
    d->count = 0;
    d->capacity = 0;
}

static void doc_free(Document *d)
{
    for (int i = 0; i < d->count; i++)
        free(d->lines[i]);
    free(d->lines);
    doc_init(d);
}

/* Grow capacity so at least `needed` slots exist. Returns 1 on success. */
static int doc_reserve(Document *d, int needed)
{
    if (needed <= d->capacity)
        return 1;
    int cap = d->capacity ? d->capacity : 8;
    while (cap < needed)
        cap *= 2;
    char **nl = realloc(d->lines, (size_t)cap * sizeof *nl);
    if (!nl)
        return 0;
    d->lines = nl;
    d->capacity = cap;
    return 1;
}

/*
 * Raw insert/delete: no undo bookkeeping, no user messages.
 * doc_insert_raw takes ownership of `text` (must be heap-allocated).
 * doc_delete_raw returns the removed line (caller owns it) or NULL.
 */
static int doc_insert_raw(Document *d, int idx, char *text)
{
    if (idx < 0 || idx > d->count)
        return 0;
    if (!doc_reserve(d, d->count + 1))
        return 0;
    memmove(&d->lines[idx + 1], &d->lines[idx],
            (size_t)(d->count - idx) * sizeof *d->lines);
    d->lines[idx] = text;
    d->count++;
    return 1;
}

static char *doc_delete_raw(Document *d, int idx)
{
    if (idx < 0 || idx >= d->count)
        return NULL;
    char *t = d->lines[idx];
    memmove(&d->lines[idx], &d->lines[idx + 1],
            (size_t)(d->count - idx - 1) * sizeof *d->lines);
    d->count--;
    return t;
}

static void doc_print(const Document *d)
{
    if (d->count == 0) {
        printf("(empty document)\n");
        return;
    }
    for (int i = 0; i < d->count; i++)
        printf("%4d  %s\n", i + 1, d->lines[i]);
}

static void doc_print_line(const Document *d, int idx)
{
    printf("%4d  %s\n", idx + 1, d->lines[idx]);
}

/* Save; returns number of lines written, or -1 on error. */
static int doc_save(const Document *d, const char *path)
{
    FILE *f = fopen(path, "w");
    if (!f)
        return -1;
    for (int i = 0; i < d->count; i++)
        fprintf(f, "%s\n", d->lines[i]);
    if (fclose(f) != 0)
        return -1;
    return d->count;
}

/*
 * Load: replaces the document with the file's lines (trailing '\n' stripped).
 * Returns number of lines read, or -1 on error (document left untouched).
 */
static int doc_load(Document *d, const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f)
        return -1;

    Document tmp;
    doc_init(&tmp);
    char *line = NULL;
    size_t cap = 0;
    ssize_t n;
    int ok = 1;
    while ((n = getline(&line, &cap, f)) >= 0) {
        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r'))
            line[--n] = '\0';
        char *copy = strdup(line);
        if (!copy || !doc_reserve(&tmp, tmp.count + 1) ||
            !doc_insert_raw(&tmp, tmp.count, copy)) {
            free(copy);
            ok = 0;
            break;
        }
    }
    free(line);
    fclose(f);
    if (!ok) {
        doc_free(&tmp);
        return -1;
    }
    doc_free(d);
    *d = tmp;
    return d->count;
}

/* Search: prints matching lines, returns number of matches. */
static int doc_search(const Document *d, const char *pattern)
{
    int hits = 0;
    for (int i = 0; i < d->count; i++) {
        if (strstr(d->lines[i], pattern)) {
            printf("line %d: %s\n", i + 1, d->lines[i]);
            hits++;
        }
    }
    return hits;
}

static void doc_stats(const Document *d, int *nlines, int *nwords, int *nchars)
{
    int words = 0, chars = 0;
    for (int i = 0; i < d->count; i++) {
        const char *p = d->lines[i];
        chars += (int)strlen(p);
        int in_word = 0;
        for (; *p; p++) {
            if (isspace((unsigned char)*p)) {
                in_word = 0;
            } else if (!in_word) {
                in_word = 1;
                words++;
            }
        }
    }
    *nlines = d->count;
    *nwords = words;
    *nchars = chars;
}

/* ------------------------------------------------------------------ */
/* Undo: stack of inverse operations                                   */
/* ------------------------------------------------------------------ */

typedef enum { OP_INSERT, OP_DELETE, OP_REPLACE } OpType;

typedef struct {
    int line;        /* 0-based */
    char *old_text;  /* owned */
} LineChange;

typedef struct {
    OpType type;
    int line;             /* 0-based; for OP_INSERT / OP_DELETE */
    char *text;           /* OP_DELETE: the removed line (owned) */
    LineChange *changes;  /* OP_REPLACE: per-line old texts (owned) */
    int n_changes;
} UndoOp;

typedef struct {
    UndoOp *ops;
    int count;
    int capacity;
} UndoStack;

static void undo_init(UndoStack *u)
{
    u->ops = NULL;
    u->count = 0;
    u->capacity = 0;
}

static void undo_op_free(UndoOp *op)
{
    free(op->text);
    for (int i = 0; i < op->n_changes; i++)
        free(op->changes[i].old_text);
    free(op->changes);
}

static void undo_clear(UndoStack *u)
{
    for (int i = 0; i < u->count; i++)
        undo_op_free(&u->ops[i]);
    u->count = 0;
}

static void undo_free(UndoStack *u)
{
    undo_clear(u);
    free(u->ops);
    undo_init(u);
}

static int undo_push(UndoStack *u, UndoOp op)
{
    if (u->count == u->capacity) {
        int cap = u->capacity ? u->capacity * 2 : 8;
        UndoOp *no = realloc(u->ops, (size_t)cap * sizeof *no);
        if (!no) {
            undo_op_free(&op);
            return 0;
        }
        u->ops = no;
        u->capacity = cap;
    }
    u->ops[u->count++] = op;
    return 1;
}

static void undo_push_insert(UndoStack *u, int line)
{
    UndoOp op = { .type = OP_INSERT, .line = line,
                  .text = NULL, .changes = NULL, .n_changes = 0 };
    undo_push(u, op);
}

static void undo_push_delete(UndoStack *u, int line, char *text)
{
    UndoOp op = { .type = OP_DELETE, .line = line,
                  .text = text, .changes = NULL, .n_changes = 0 };
    undo_push(u, op);
}

static void undo_push_replace(UndoStack *u, LineChange *changes, int n)
{
    UndoOp op = { .type = OP_REPLACE, .line = -1,
                  .text = NULL, .changes = changes, .n_changes = n };
    undo_push(u, op);
}

/*
 * Undo the most recent action. Uses the raw document ops so no new undo
 * record is created. Returns 1 if something was undone, 0 if stack empty.
 */
static int undo_pop_apply(UndoStack *u, Document *d)
{
    if (u->count == 0)
        return 0;
    UndoOp op = u->ops[--u->count];
    switch (op.type) {
    case OP_INSERT: {
        /* we inserted line -> delete it back */
        char *t = doc_delete_raw(d, op.line);
        free(t);
        printf("undone: removed inserted line %d\n", op.line + 1);
        break;
    }
    case OP_DELETE:
        /* we deleted line -> put the saved text back */
        if (doc_insert_raw(d, op.line, op.text)) {
            op.text = NULL; /* ownership moved to the document */
            printf("undone: restored deleted line %d\n", op.line + 1);
        } else {
            printf("error: out of memory — undo failed\n");
            u->ops[u->count++] = op; /* put it back */
            return 1;
        }
        break;
    case OP_REPLACE:
        /* restore every changed line's old text */
        for (int i = 0; i < op.n_changes; i++) {
            free(d->lines[op.changes[i].line]);
            d->lines[op.changes[i].line] = op.changes[i].old_text;
            op.changes[i].old_text = NULL; /* ownership moved */
        }
        printf("undone: restored %d replaced line(s)\n", op.n_changes);
        break;
    }
    undo_op_free(&op);
    return 1;
}

/* ------------------------------------------------------------------ */
/* Small helpers                                                       */
/* ------------------------------------------------------------------ */

static char *skip_ws(char *p)
{
    while (*p && isspace((unsigned char)*p))
        p++;
    return p;
}

/*
 * Parse a 1-based line number at `p`. On success sets *out (0-based index)
 * and *rest (first char after the number), returns 1. Else returns 0.
 */
static int parse_line_no(char *p, const Document *d, int *out, char **rest)
{
    (void)d;
    char *end;
    long n = strtol(p, &end, 10);
    if (end == p)
        return 0; /* no digits at all */
    *out = (int)(n - 1);
    *rest = end;
    return 1;
}

/* 1-based range check with a friendly message. */
static int check_line(const Document *d, int idx0, int allow_end)
{
    int hi = allow_end ? d->count + 1 : d->count;
    if (idx0 < 0 || idx0 + 1 > hi) {
        if (d->count == 0)
            printf("error: document is empty\n");
        else
            printf("error: line number out of range (1-%d)\n", hi);
        return 0;
    }
    return 1;
}

/*
 * Replace every non-overlapping occurrence of `old` with `new_` in `src`.
 * Returns a new heap string and sets *nrep; returns NULL (and *nrep = 0)
 * when there is nothing to replace. `old` must be non-empty.
 */
static char *replace_all(const char *src, const char *old,
                         const char *new_, int *nrep)
{
    size_t old_len = strlen(old), new_len = strlen(new_);
    int n = 0;
    for (const char *p = src; (p = strstr(p, old)) != NULL; p += old_len)
        n++;
    *nrep = n;
    if (n == 0)
        return NULL;

    size_t src_len = strlen(src);
    /* n*old_len <= src_len (non-overlapping), so this cannot underflow */
    size_t dst_len = src_len - (size_t)n * old_len + (size_t)n * new_len;
    char *dst = malloc(dst_len + 1);
    if (!dst)
        return NULL;

    char *w = dst;
    const char *p = src;
    const char *q;
    while ((q = strstr(p, old)) != NULL) {
        size_t seg = (size_t)(q - p);
        memcpy(w, p, seg);
        w += seg;
        memcpy(w, new_, new_len);
        w += new_len;
        p = q + old_len;
    }
    strcpy(w, p);
    return dst;
}

static void print_help(void)
{
    printf("Commands (line numbers are 1-based):\n"
           "  i <n> <text>      insert <text> as line n (n may be len+1 to append)\n"
           "  a <text>           append <text> after the last line\n"
           "  d <n>              delete line n\n"
           "  p                 print all lines with numbers\n"
           "  p <n>              print line n\n"
           "  w <file>          save to <file> (w alone re-uses the last file)\n"
           "  r <file>          load <file>, replacing the current document\n"
           "  s <pattern>       search: show lines containing <pattern>\n"
           "  f <old> <new>     replace <old> with <new> everywhere\n"
           "  f <n> <old> <new> replace on line n only\n"
           "  u                 undo the last change (multi-level)\n"
           "  c                 show line / word / character counts\n"
           "  h                 show this help\n"
           "  q                 quit (warns once about unsaved changes)\n");
}

/* ------------------------------------------------------------------ */
/* Replace command                                                     */
/* ------------------------------------------------------------------ */

/* Replace on one line; records undo. Returns 1 if the line changed. */
static int replace_oneline(Document *d, UndoStack *u, int idx,
                           const char *old, const char *new_)
{
    int nrep = 0;
    char *ns = replace_all(d->lines[idx], old, new_, &nrep);
    if (!ns)
        return 0;
    LineChange *ch = malloc(sizeof *ch);
    if (!ch) {
        free(ns);
        return 0;
    }
    ch[0].line = idx;
    ch[0].old_text = d->lines[idx];
    d->lines[idx] = ns;
    undo_push_replace(u, ch, 1);
    printf("replaced %d occurrence(s) on line %d\n", nrep, idx + 1);
    return 1;
}

/* Replace across the whole document; records one grouped undo entry. */
static void replace_all_lines(Document *d, UndoStack *u,
                              const char *old, const char *new_)
{
    LineChange *changes = NULL;
    int nch = 0, capch = 0, total = 0;
    for (int i = 0; i < d->count; i++) {
        int nrep = 0;
        char *ns = replace_all(d->lines[i], old, new_, &nrep);
        if (!ns)
            continue;
        if (nch == capch) {
            int nc = capch ? capch * 2 : 8;
            LineChange *tmp = realloc(changes, (size_t)nc * sizeof *tmp);
            if (!tmp) {
                free(ns);
                break;
            }
            changes = tmp;
            capch = nc;
        }
        changes[nch].line = i;
        changes[nch].old_text = d->lines[i];
        nch++;
        d->lines[i] = ns;
        total += nrep;
    }
    if (nch == 0) {
        free(changes);
        printf("no occurrences of '%s' found\n", old);
        return;
    }
    undo_push_replace(u, changes, nch);
    printf("replaced %d occurrence(s) on %d line(s)\n", total, nch);
}

/* ------------------------------------------------------------------ */
/* Main loop                                                           */
/* ------------------------------------------------------------------ */

int main(int argc, char **argv)
{
    Document doc;
    UndoStack undo;
    doc_init(&doc);
    undo_init(&undo);

    char *last_file = NULL;
    int modified = 0;
    int quit_armed = 0;

    if (argc > 2) {
        printf("usage: %s [file]\n", argv[0]);
        return 1;
    }
    if (argc > 1) {
        int n = doc_load(&doc, argv[1]);
        if (n < 0) {
            printf("error: cannot read '%s' — starting empty\n", argv[1]);
        } else {
            last_file = strdup(argv[1]);
            printf("read %d line(s) from '%s'\n", n, argv[1]);
        }
    }

    char *input = NULL;
    size_t icap = 0;
    ssize_t n;

    while ((n = getline(&input, &icap, stdin)) >= 0) {
        while (n > 0 && (input[n - 1] == '\n' || input[n - 1] == '\r'))
            input[--n] = '\0';
        char *p = skip_ws(input);
        if (*p == '\0')
            continue;

        char cmd = *p;
        char *args = skip_ws(p + 1);
        quit_armed = (cmd == 'q' && modified) ? quit_armed : 0;

        switch (cmd) {
        case 'i': { /* i <n> <text> */
            int idx;
            char *rest;
            if (!parse_line_no(args, &doc, &idx, &rest)) {
                printf("usage: i <line> <text>\n");
                break;
            }
            if (!check_line(&doc, idx, 1))
                break;
            char *text = strdup(skip_ws(rest));
            if (!text || !doc_insert_raw(&doc, idx, text)) {
                free(text);
                printf("error: out of memory\n");
                break;
            }
            undo_push_insert(&undo, idx);
            modified = 1;
            printf("inserted at line %d\n", idx + 1);
            break;
        }
        case 'a': { /* a <text> */
            if (*args == '\0') {
                printf("usage: a <text>\n");
                break;
            }
            char *text = strdup(args);
            if (!text || !doc_insert_raw(&doc, doc.count, text)) {
                free(text);
                printf("error: out of memory\n");
                break;
            }
            undo_push_insert(&undo, doc.count - 1);
            modified = 1;
            printf("appended as line %d\n", doc.count);
            break;
        }
        case 'd': { /* d <n> */
            int idx;
            char *rest;
            if (!parse_line_no(args, &doc, &idx, &rest) || *skip_ws(rest) != '\0') {
                printf("usage: d <line>\n");
                break;
            }
            if (!check_line(&doc, idx, 0))
                break;
            char *t = doc_delete_raw(&doc, idx);
            undo_push_delete(&undo, idx, t);
            modified = 1;
            printf("deleted line %d: %s\n", idx + 1, t);
            break;
        }
        case 'p': { /* p | p <n> */
            if (*args == '\0') {
                doc_print(&doc);
            } else {
                int idx;
                char *rest;
                if (!parse_line_no(args, &doc, &idx, &rest) || *skip_ws(rest) != '\0') {
                    printf("usage: p [<line>]\n");
                    break;
                }
                if (!check_line(&doc, idx, 0))
                    break;
                doc_print_line(&doc, idx);
            }
            break;
        }
        case 'w': { /* w [file] */
            const char *path = (*args == '\0') ? last_file : args;
            if (!path) {
                printf("error: no file yet — use: w <file>\n");
                break;
            }
            /* filename is the first word */
            const char *sp = path;
            while (*sp && !isspace((unsigned char)*sp))
                sp++;
            size_t len = (size_t)(sp - path);
            char *fname = malloc(len + 1);
            if (!fname) {
                printf("error: out of memory\n");
                break;
            }
            memcpy(fname, path, len);
            fname[len] = '\0';
            int nw = doc_save(&doc, fname);
            if (nw < 0) {
                printf("error: cannot write '%s'\n", fname);
                free(fname);
                break;
            }
            free(last_file);
            last_file = fname; /* keep it */
            modified = 0;
            printf("wrote %d line(s) to '%s'\n", nw, last_file);
            break;
        }
        case 'r': { /* r <file> */
            if (*args == '\0') {
                printf("usage: r <file>\n");
                break;
            }
            /* filename is the first word */
            char *sp = args;
            while (*sp && !isspace((unsigned char)*sp))
                sp++;
            size_t flen = (size_t)(sp - args);
            char *fname = malloc(flen + 1);
            if (!fname) {
                printf("error: out of memory\n");
                break;
            }
            memcpy(fname, args, flen);
            fname[flen] = '\0';
            int nr = doc_load(&doc, fname);
            if (nr < 0) {
                printf("error: cannot read '%s'\n", fname);
                free(fname);
                break;
            }
            undo_clear(&undo); /* old line numbers are meaningless now */
            free(last_file);
            last_file = fname; /* keep it */
            modified = 0;
            printf("read %d line(s) from '%s'\n", nr, last_file);
            break;
        }
        case 's': { /* s <pattern> */
            if (*args == '\0') {
                printf("usage: s <pattern>\n");
                break;
            }
            int hits = doc_search(&doc, args);
            if (hits == 0)
                printf("no lines contain '%s'\n", args);
            break;
        }
        case 'f': { /* f <old> <new> | f <n> <old> <new> */
            char *t1 = strtok(args, " \t");
            char *t2 = strtok(NULL, " \t");
            char *t3 = strtok(NULL, " \t");
            char *t4 = strtok(NULL, " \t");
            if (!t1 || !t2 || t4) {
                printf("usage: f <old> <new>  |  f <line> <old> <new>\n");
                break;
            }
            const char *old, *new_;
            int only = -1;
            if (t3) {
                /* three words: first must be a valid line number */
                char *end;
                long ln = strtol(t1, &end, 10);
                if (*end != '\0' || ln < 1 || ln > doc.count) {
                    printf("error: '%s' is not a valid line number (1-%d)\n",
                           t1, doc.count);
                    break;
                }
                only = (int)ln - 1;
                old = t2;
                new_ = t3;
            } else {
                old = t1;
                new_ = t2;
            }
            if (*old == '\0') {
                printf("error: <old> must not be empty\n");
                break;
            }
            int changed = 0;
            if (only >= 0)
                changed = replace_oneline(&doc, &undo, only, old, new_);
            else {
                int before = undo.count;
                replace_all_lines(&doc, &undo, old, new_);
                changed = (undo.count != before);
            }
            if (changed)
                modified = 1;
            else if (only >= 0)
                printf("no occurrences of '%s' on line %d\n", old, only + 1);
            break;
        }
        case 'u': { /* undo */
            if (!undo_pop_apply(&undo, &doc))
                printf("nothing to undo\n");
            else
                modified = 1;
            break;
        }
        case 'c': { /* counts */
            int nl, nw, nc;
            doc_stats(&doc, &nl, &nw, &nc);
            printf("%d line(s), %d word(s), %d character(s)\n", nl, nw, nc);
            break;
        }
        case 'h':
            print_help();
            break;
        case 'q':
            if (modified && !quit_armed) {
                printf("warning: unsaved changes — 'q' again to quit anyway\n");
                quit_armed = 1;
                break;
            }
            goto done;
        default:
            printf("unknown command '%c' (type h for help)\n", cmd);
            break;
        }
    }

done:
    free(input);
    free(last_file);
    undo_free(&undo);
    doc_free(&doc);
    return 0;
}
