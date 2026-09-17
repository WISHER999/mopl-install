#ifndef __OBJC__
#define _POSIX_C_SOURCE 200809L
#endif
// mopl.c — expanded MOPL# interpreter, C reimplementation
//
// Build:  clang -O2 -Wall -Wextra -pedantic -fobjc-arc -framework Cocoa mopl.m -o mopl
// Run:    ./mopl run script.mopl
//
// Implemented in this version:
//   - Tag.<name> = <type>.<value>
//   - Tag.<name> +[+...]= <type>.<value> (the + Law)
//   - num / state / blea / logic tag types
//   - Terminal '<name>' / Terminal "literal"
//   - Op of <a> <operator> <b> ==
//   - Op of rnd <min> <max> ==
//   - Op of len <tag> ==
//   - Dif in <name>[, <name>...]
//   - Check / Else / EndCheck (nested conditionals)
//   - Cycle / EndCycle (nested loops)
//   - logic <name> [param ...] / EndLogic
//   - Call <name> [args ...]
//   - Return [value]
//   - Input <tag> ["prompt"]
//   - Gfx.init / Gfx.clear / Gfx.at / Gfx.text / Gfx.rect / Gfx.line
//     / Gfx.present / Gfx.sleep / Gfx.close
//   - Native macOS GUI:
//       Window "title" width height
//       Text "text" x y size
//       Button id "caption" x y width height
//       Input id x y width height
//       SetText id "text"
//       GetText id
//       OnClick id / EndClick
//       EndWindow
//
// On macOS, Window/Text/Button/Input use native Cocoa/AppKit controls.
// On non-macOS platforms, the normal terminal interpreter remains available.
//
// ============================================================================

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>
#include <errno.h>
#include <time.h>
#include <stdint.h>
#include <unistd.h>

#ifdef __APPLE__
#import <Cocoa/Cocoa.h>
#endif

#define MAX_NAME 128
#define MAX_LINE 1024
#define MAX_PROGRAM_LINES 8192
#define MAX_CYCLE_REPEATS 1000000LL
#define MAX_CALL_DEPTH 128
#define MAX_GFX_W 240
#define MAX_GFX_H 120

// ============================================================================
// Common helpers
// ============================================================================

static void die_oom(void) {
    fprintf(stderr, "fatal: out of memory\n");
    exit(2);
}

static char *xstrdup(const char *s) {
    char *p = strdup(s ? s : "");
    if (!p) die_oom();
    return p;
}

static char *lstrip(char *s) {
    while (*s && isspace((unsigned char)*s)) s++;
    return s;
}

static void rstrip(char *s) {
    size_t n = strlen(s);
    while (n > 0 && isspace((unsigned char)s[n - 1]))
        s[--n] = '\0';
}

static void trim_in_place(char *s) {
    char *p = lstrip(s);
    if (p != s)
        memmove(s, p, strlen(p) + 1);
    rstrip(s);
}

static int starts_with(const char *s, const char *prefix) {
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

static int starts_with_ci(const char *s, const char *prefix) {
    while (*prefix) {
        if (tolower((unsigned char)*s) !=
            tolower((unsigned char)*prefix))
            return 0;
        s++;
        prefix++;
    }
    return 1;
}

static long long parse_int(const char *s, long long fallback) {
    char *end = NULL;
    errno = 0;

    long long v = strtoll(s, &end, 10);

    if (errno != 0 || end == s)
        return fallback;

    return v;
}

static const char *next_token(
    const char *p,
    char *out,
    size_t out_sz
) {
    size_t n = 0;

    while (*p && isspace((unsigned char)*p))
        p++;

    if (!*p) {
        if (out_sz)
            out[0] = '\0';
        return p;
    }

    if (*p == '"' || *p == '\'') {
        char quote = *p++;

        while (*p && *p != quote) {
            char c = *p++;

            if (c == '\\' && *p) {
                char e = *p++;

                switch (e) {
                    case 'n': c = '\n'; break;
                    case 'r': c = '\r'; break;
                    case 't': c = '\t'; break;
                    case '\\': c = '\\'; break;
                    case '"': c = '"'; break;
                    case '\'': c = '\''; break;
                    default: c = e; break;
                }
            }

            if (n + 1 < out_sz)
                out[n++] = c;
        }

        if (*p == quote)
            p++;

    } else {
        while (*p && !isspace((unsigned char)*p)) {
            if (n + 1 < out_sz)
                out[n++] = *p;

            p++;
        }
    }

    if (out_sz)
        out[n] = '\0';

    return p;
}

static const char *after_first_word(const char *s) {
    while (*s && isspace((unsigned char)*s))
        s++;

    while (*s && !isspace((unsigned char)*s))
        s++;

    while (*s && isspace((unsigned char)*s))
        s++;

    return s;
}

static void strip_comment(char *s) {
    int quoted = 0;
    char quote = 0;

    for (char *p = s; *p; ++p) {
        if (*p == '\\' && quoted && p[1]) {
            ++p;
            continue;
        }

        if (*p == '"' || *p == '\'') {
            if (!quoted) {
                quoted = 1;
                quote = *p;
            } else if (quote == *p) {
                quoted = 0;
            }
        }

        if (!quoted && p[0] == '/' && p[1] == '/') {
            *p = '\0';
            break;
        }
    }

    rstrip(s);
}

static char *decode_remainder(const char *p) {
    while (*p && isspace((unsigned char)*p))
        p++;

    size_t cap = strlen(p) + 1;

    char *out = malloc(cap);

    if (!out)
        die_oom();

    size_t n = 0;

    if (*p == '"' || *p == '\'') {
        char quote = *p++;

        while (*p && *p != quote) {
            char c = *p++;

            if (c == '\\' && *p) {
                char e = *p++;

                switch (e) {
                    case 'n': c = '\n'; break;
                    case 'r': c = '\r'; break;
                    case 't': c = '\t'; break;
                    case '\\': c = '\\'; break;
                    case '"': c = '"'; break;
                    case '\'': c = '\''; break;
                    default: c = e; break;
                }
            }

            out[n++] = c;
        }

    } else {
        while (*p)
            out[n++] = *p++;

        while (n > 0 && isspace((unsigned char)out[n - 1]))
            n--;
    }

    out[n] = '\0';

    return out;
}

// ============================================================================
// Tags
// ============================================================================

typedef enum {
    TAG_NUM = 0,
    TAG_STATE = 1,
    TAG_BLEA = 2,
    TAG_LOGIC = 3
} TagType;

typedef struct Tag {
    char *name;
    TagType type;

    long long num_value;
    char *str_value;

    int level;

    struct Tag *next;
} Tag;

static Tag *g_tags = NULL;
static int g_in_loop = 0;

static Tag *find_tag(const char *name) {
    for (Tag *t = g_tags; t; t = t->next) {
        if (strcmp(t->name, name) == 0)
            return t;
    }

    return NULL;
}

static Tag *create_tag(
    const char *name,
    TagType type,
    long long num_value,
    const char *str_value
) {
    Tag *t = calloc(1, sizeof(*t));

    if (!t)
        die_oom();

    t->name = xstrdup(name);
    t->type = type;
    t->num_value = num_value;

    if (str_value)
        t->str_value = xstrdup(str_value);

    t->level = 1;

    t->next = g_tags;
    g_tags = t;

    return t;
}

static void free_tags(void) {
    Tag *t = g_tags;

    while (t) {
        Tag *next = t->next;

        free(t->name);
        free(t->str_value);
        free(t);

        t = next;
    }

    g_tags = NULL;
}

static long long tag_numeric(const Tag *t) {
    return t ? t->num_value : 0;
}

static long long resolve_operand(const char *tok) {
    Tag *t = find_tag(tok);

    if (t)
        return tag_numeric(t);

    if (strcmp(tok, "true") == 0 ||
        strcmp(tok, "TRUE") == 0)
        return 1;

    if (strcmp(tok, "false") == 0 ||
        strcmp(tok, "FALSE") == 0)
        return 0;

    if (*tok == '$') {
        Tag *v = find_tag(tok + 1);

        if (v)
            return tag_numeric(v);
    }

    return parse_int(tok, 0);
}

static const char *tag_type_name(TagType t) {
    switch (t) {
        case TAG_NUM: return "num";
        case TAG_STATE: return "state";
        case TAG_BLEA: return "blea";
        case TAG_LOGIC: return "logic";
    }

    return "unknown";
}

static void report_runtime(
    const char *msg,
    const char *line
) {
    fprintf(
        stderr,
        "runtime error: %s%s%s\n",
        msg,
        line ? " -- " : "",
        line ? line : ""
    );
}

// ============================================================================
// Program
// ============================================================================

typedef struct {
    char **lines;
    int count;
    int cap;
} Program;

typedef struct {
    char *name;

    char **params;
    int param_count;

    int start;
    int end;
} LogicDef;

typedef struct {
    LogicDef *items;
    int count;
    int cap;
} LogicTable;

static Program g_program = {0};
static LogicTable g_logic = {0};

static int g_call_depth = 0;
static int g_return_active = 0;

static void program_add(
    Program *p,
    const char *line
) {
    if (p->count == p->cap) {
        int new_cap = p->cap ? p->cap * 2 : 256;

        char **next = realloc(
            p->lines,
            (size_t)new_cap * sizeof(*next)
        );

        if (!next)
            die_oom();

        p->lines = next;
        p->cap = new_cap;
    }

    p->lines[p->count++] = xstrdup(line);
}

static void free_program(Program *p) {
    for (int i = 0; i < p->count; ++i)
        free(p->lines[i]);

    free(p->lines);

    memset(p, 0, sizeof(*p));
}

static void logic_add(LogicDef def) {
    if (g_logic.count == g_logic.cap) {
        int new_cap = g_logic.cap ? g_logic.cap * 2 : 32;

        LogicDef *next = realloc(
            g_logic.items,
            (size_t)new_cap * sizeof(*next)
        );

        if (!next)
            die_oom();

        g_logic.items = next;
        g_logic.cap = new_cap;
    }

    g_logic.items[g_logic.count++] = def;
}

static void free_logic_table(void) {
    for (int i = 0; i < g_logic.count; ++i) {
        free(g_logic.items[i].name);

        for (int j = 0; j < g_logic.items[i].param_count; ++j)
            free(g_logic.items[i].params[j]);

        free(g_logic.items[i].params);
    }

    free(g_logic.items);

    memset(&g_logic, 0, sizeof(g_logic));
}

static LogicDef *find_logic(const char *name) {
    for (int i = 0; i < g_logic.count; ++i) {
        if (strcmp(g_logic.items[i].name, name) == 0)
            return &g_logic.items[i];
    }

    return NULL;
}

// ============================================================================
// Block matching
// ============================================================================

typedef enum {
    BLOCK_CHECK,
    BLOCK_CYCLE,
    BLOCK_LOGIC,
    BLOCK_CLICK,
    BLOCK_WINDOW
} BlockType;

static int line_is(
    const char *line,
    const char *word
) {
    char tmp[MAX_LINE];

    snprintf(tmp, sizeof(tmp), "%s", line);

    strip_comment(tmp);
    trim_in_place(tmp);

    return starts_with_ci(tmp, word) &&
           (tmp[strlen(word)] == '\0' ||
            isspace((unsigned char)tmp[strlen(word)]));
}

static int find_matching_block(
    int start,
    int end,
    BlockType type,
    int *else_index
) {
    int depth = 0;

    if (else_index)
        *else_index = -1;

    for (int i = start + 1; i < end; ++i) {
        const char *line = g_program.lines[i];

        if (type == BLOCK_CHECK) {
            if (line_is(line, "Check")) {
                depth++;
            } else if (line_is(line, "EndCheck")) {
                if (depth == 0)
                    return i;

                depth--;
            } else if (line_is(line, "Else") &&
                       depth == 0 &&
                       else_index &&
                       *else_index < 0) {
                *else_index = i;
            }

        } else if (type == BLOCK_CYCLE) {
            if (line_is(line, "Cycle")) {
                depth++;
            } else if (line_is(line, "EndCycle")) {
                if (depth == 0)
                    return i;

                depth--;
            }

        } else if (type == BLOCK_LOGIC) {
            if (line_is(line, "logic")) {
                depth++;
            } else if (line_is(line, "EndLogic")) {
                if (depth == 0)
                    return i;

                depth--;
            }

        } else if (type == BLOCK_CLICK) {
            if (line_is(line, "OnClick")) {
                depth++;
            } else if (line_is(line, "EndClick")) {
                if (depth == 0)
                    return i;

                depth--;
            }

        } else if (type == BLOCK_WINDOW) {
            if (line_is(line, "Window")) {
                depth++;
            } else if (line_is(line, "EndWindow")) {
                if (depth == 0)
                    return i;

                depth--;
            }
        }
    }

    return -1;
}

// ============================================================================
// Tag assignment
// ============================================================================

static int parse_tag_type(
    const char *s,
    TagType *type
) {
    if (strcmp(s, "num") == 0) {
        *type = TAG_NUM;
        return 1;
    }

    if (strcmp(s, "state") == 0) {
        *type = TAG_STATE;
        return 1;
    }

    if (strcmp(s, "blea") == 0) {
        *type = TAG_BLEA;
        return 1;
    }

    if (strcmp(s, "logic") == 0) {
        *type = TAG_LOGIC;
        return 1;
    }

    return 0;
}

static void assign_tag(
    const char *name,
    TagType type,
    const char *value
) {
    Tag *t = find_tag(name);

    if (!t)
        t = create_tag(name, type, 0, NULL);

    t->type = type;

    if (type == TAG_BLEA || type == TAG_LOGIC) {
        free(t->str_value);
        t->str_value = xstrdup(value);
        t->num_value = 0;

    } else {
        t->num_value = resolve_operand(value);

        free(t->str_value);
        t->str_value = NULL;
    }
}

static void increment_tag(
    const char *name,
    TagType type,
    const char *value,
    int plus_count
) {
    Tag *t = find_tag(name);

    long long delta = resolve_operand(value);

    if (!t) {
        if (type == TAG_BLEA || type == TAG_LOGIC) {
            t = create_tag(
                name,
                type,
                0,
                value
            );
        } else {
            t = create_tag(
                name,
                type,
                0,
                NULL
            );

            t->num_value = delta * plus_count;
        }

        return;
    }

    if (type == TAG_BLEA || type == TAG_LOGIC) {
        free(t->str_value);

        t->str_value = xstrdup(value);
        t->type = type;

    } else {
        t->num_value += delta * plus_count;
        t->type = type;
    }
}

static void handle_tag_line(char *line) {
    char *p = line + 4;

    while (*p && isspace((unsigned char)*p))
        p++;

    char name[MAX_NAME];

    size_t n = 0;

    while (*p &&
           *p != '=' &&
           *p != '+' &&
           !isspace((unsigned char)*p) &&
           n + 1 < sizeof(name)) {
        name[n++] = *p++;
    }

    name[n] = '\0';

    while (*p && isspace((unsigned char)*p))
        p++;

    int plus_count = 0;

    while (*p == '+') {
        plus_count++;
        p++;
    }

    while (*p && isspace((unsigned char)*p))
        p++;

    if (*p != '=') {
        report_runtime("expected '=' in Tag assignment", line);
        return;
    }

    p++;

    while (*p && isspace((unsigned char)*p))
        p++;

    char type_name[MAX_NAME];

    p = (char *)next_token(
        p,
        type_name,
        sizeof(type_name)
    );

    TagType type;

    if (!parse_tag_type(type_name, &type)) {
        report_runtime("unknown tag type", line);
        return;
    }

    char *value = decode_remainder(p);

    if (plus_count > 0)
        increment_tag(
            name,
            type,
            value,
            plus_count
        );
    else
        assign_tag(
            name,
            type,
            value
        );

    free(value);
}

// ============================================================================
// Terminal
// ============================================================================

static void handle_terminal_line(char *line) {
    const char *p = after_first_word(line);

    if (!*p)
        return;

    if (*p == '\'' || *p == '"') {
        char *text = decode_remainder(p);

        printf("%s\n", text);

        free(text);
        return;
    }

    char tok[MAX_LINE];

    next_token(
        p,
        tok,
        sizeof(tok)
    );

    Tag *t = find_tag(tok);

    if (t) {
        if (t->type == TAG_BLEA ||
            t->type == TAG_LOGIC) {
            printf(
                "%s\n",
                t->str_value ? t->str_value : ""
            );
        } else {
            printf(
                "%lld\n",
                t->num_value
            );
        }

    } else {
        printf("%s\n", tok);
    }
}

// ============================================================================
// Op
// ============================================================================

static long long random_range(
    long long min,
    long long max
) {
    if (max < min) {
        long long tmp = min;
        min = max;
        max = tmp;
    }

    unsigned long long span =
        (unsigned long long)(max - min) + 1ULL;

    unsigned long long r =
        ((unsigned long long)rand() << 32) ^
        (unsigned long long)rand();

    if (span == 0)
        return (long long)r;

    return min + (long long)(r % span);
}

static long long eval_operator(
    long long a,
    const char *op,
    long long b
) {
    if (strcmp(op, "==") == 0)
        return a == b;

    if (strcmp(op, "!=") == 0)
        return a != b;

    if (strcmp(op, ">") == 0)
        return a > b;

    if (strcmp(op, "<") == 0)
        return a < b;

    if (strcmp(op, ">=") == 0)
        return a >= b;

    if (strcmp(op, "<=") == 0)
        return a <= b;

    if (strcmp(op, "+") == 0)
        return a + b;

    if (strcmp(op, "-") == 0)
        return a - b;

    if (strcmp(op, "*") == 0)
        return a * b;

    if (strcmp(op, "/") == 0)
        return b == 0 ? 0 : a / b;

    if (strcmp(op, "%") == 0)
        return b == 0 ? 0 : a % b;

    return 0;
}

static long long g_last_op_result = 0;

static void store_op_result(long long result) {
    g_last_op_result = result;

    Tag *t = find_tag("_");

    if (!t)
        t = create_tag("_", TAG_NUM, result, NULL);

    t->type = TAG_NUM;
    t->num_value = result;

    free(t->str_value);
    t->str_value = NULL;
}

static void handle_op_line(char *line) {
    const char *p = line;

    while (*p && !isspace((unsigned char)*p))
        p++;

    while (*p && isspace((unsigned char)*p))
        p++;

    if (!starts_with_ci(p, "of"))
        return;

    p += 2;

    while (*p && isspace((unsigned char)*p))
        p++;

    char a[256];

    p = next_token(
        p,
        a,
        sizeof(a)
    );

    if (strcmp(a, "rnd") == 0) {
        char min_s[256];
        char max_s[256];

        p = next_token(
            p,
            min_s,
            sizeof(min_s)
        );

        p = next_token(
            p,
            max_s,
            sizeof(max_s)
        );

        long long min = resolve_operand(min_s);
        long long max = resolve_operand(max_s);

        store_op_result(
            random_range(min, max)
        );

        return;
    }

    if (strcmp(a, "len") == 0) {
        char name[256];

        p = next_token(
            p,
            name,
            sizeof(name)
        );

        Tag *t = find_tag(name);

        long long result = 0;

        if (t && t->str_value)
            result = (long long)strlen(t->str_value);

        store_op_result(result);

        return;
    }

    char op[64];

    p = next_token(
        p,
        op,
        sizeof(op)
    );

    char b[256];

    p = next_token(
        p,
        b,
        sizeof(b)
    );

    long long av = resolve_operand(a);
    long long bv = resolve_operand(b);

    store_op_result(
        eval_operator(av, op, bv)
    );
}

// ============================================================================
// Dif
// ============================================================================

static void handle_dif_line(char *line) {
    const char *p = after_first_word(line);

    while (*p) {
        char name[MAX_NAME];

        p = next_token(
            p,
            name,
            sizeof(name)
        );

        if (!*name)
            break;

        Tag *t = find_tag(name);

        if (t) {
            printf(
                "%s (%s) = ",
                t->name,
                tag_type_name(t->type)
            );

            if (t->type == TAG_BLEA ||
                t->type == TAG_LOGIC) {
                printf(
                    "%s",
                    t->str_value ?
                        t->str_value :
                        ""
                );
            } else {
                printf(
                    "%lld",
                    t->num_value
                );
            }

            printf("\n");
        }

        while (*p == ',' ||
               isspace((unsigned char)*p))
            p++;
    }
}

// ============================================================================
// Conditions
// ============================================================================

static long long eval_condition(
    const char *expr,
    int *ok
) {
    char a[256];
    char op[64];
    char b[256];

    const char *p = next_token(
        expr,
        a,
        sizeof(a)
    );

    p = next_token(
        p,
        op,
        sizeof(op)
    );

    p = next_token(
        p,
        b,
        sizeof(b)
    );

    if (!*a || !*op || !*b) {
        if (ok)
            *ok = 0;

        return 0;
    }

    if (ok)
        *ok = 1;

    return eval_operator(
        resolve_operand(a),
        op,
        resolve_operand(b)
    );
}

// ============================================================================
// Input
// ============================================================================

static void handle_input_line(char *line) {
    const char *p = after_first_word(line);

    char name[MAX_NAME];

    p = next_token(
        p,
        name,
        sizeof(name)
    );

    if (!*name)
        return;

    char *prompt = decode_remainder(p);

    if (*prompt) {
        printf("%s", prompt);
        fflush(stdout);
    }

    char buffer[1024];

    if (!fgets(buffer, sizeof(buffer), stdin)) {
        buffer[0] = '\0';
    }

    rstrip(buffer);

    Tag *t = find_tag(name);

    if (!t)
        t = create_tag(
            name,
            TAG_BLEA,
            0,
            ""
        );

    t->type = TAG_BLEA;

    free(t->str_value);

    t->str_value = xstrdup(buffer);

    free(prompt);
}

// ============================================================================
// Native Gfx backend
// ============================================================================

#include "mopl_graphics.h"

static long long gfx_arg(const char *s) {
    return resolve_operand(s);
}

static int gfx_next_token(
    const char **pp,
    char *out,
    size_t cap
) {
    const char *p = *pp;

    while (*p && isspace((unsigned char)*p))
        p++;

    if (!*p)
        return 0;

    size_t n = 0;

    if (*p == '"') {
        p++;

        while (*p && *p != '"' && n + 1 < cap)
            out[n++] = *p++;

        if (*p == '"')
            p++;
    } else {
        while (*p &&
               !isspace((unsigned char)*p) &&
               n + 1 < cap) {
            out[n++] = *p++;
        }
    }

    out[n] = '\0';
    *pp = p;

    return 1;
}

static void handle_gfx_line(char *line) {
    const char *p = line + 4;

    char command[64];

    if (!gfx_next_token(
            &p,
            command,
            sizeof(command))) {
        return;
    }

    char a[1024];
    char b[128];
    char c[128];
    char d[128];
    char e[128];
    char f[128];

    if (strcmp(command, "init") == 0) {

        if (!gfx_next_token(&p, a, sizeof(a)))
            return;

        if (!gfx_next_token(&p, b, sizeof(b)))
            return;

        if (!gfx_next_token(&p, c, sizeof(c)))
            return;

        mopl_gfx_init(
            a,
            gfx_arg(b),
            gfx_arg(c)
        );

    } else if (strcmp(command, "clear") == 0) {

        if (!gfx_next_token(&p, a, sizeof(a)))
            return;

        mopl_gfx_clear_screen(
            (mopl_color_t)gfx_arg(a)
        );

    } else if (strcmp(command, "present") == 0) {

        mopl_gfx_present();

    } else if (strcmp(command, "poll") == 0) {

        mopl_gfx_poll_events();

    } else if (strcmp(command, "shutdown") == 0 ||
               strcmp(command, "close") == 0) {

        mopl_gfx_shutdown();

    } else if (strcmp(command, "pixel") == 0) {

        if (!gfx_next_token(&p, a, sizeof(a)))
            return;

        if (!gfx_next_token(&p, b, sizeof(b)))
            return;

        if (!gfx_next_token(&p, c, sizeof(c)))
            return;

        mopl_gfx_set_pixel(
            gfx_arg(a),
            gfx_arg(b),
            (mopl_color_t)gfx_arg(c)
        );

    } else if (strcmp(command, "rect") == 0) {

        if (!gfx_next_token(&p, a, sizeof(a)))
            return;

        if (!gfx_next_token(&p, b, sizeof(b)))
            return;

        if (!gfx_next_token(&p, c, sizeof(c)))
            return;

        if (!gfx_next_token(&p, d, sizeof(d)))
            return;

        if (!gfx_next_token(&p, e, sizeof(e)))
            return;

        mopl_gfx_draw_rect(
            gfx_arg(a),
            gfx_arg(b),
            gfx_arg(c),
            gfx_arg(d),
            (mopl_color_t)gfx_arg(e)
        );

    } else if (strcmp(command, "rect_outline") == 0) {

        if (!gfx_next_token(&p, a, sizeof(a)))
            return;

        if (!gfx_next_token(&p, b, sizeof(b)))
            return;

        if (!gfx_next_token(&p, c, sizeof(c)))
            return;

        if (!gfx_next_token(&p, d, sizeof(d)))
            return;

        if (!gfx_next_token(&p, e, sizeof(e)))
            return;

        if (!gfx_next_token(&p, f, sizeof(f)))
            return;

        mopl_gfx_draw_rect_outline(
            gfx_arg(a),
            gfx_arg(b),
            gfx_arg(c),
            gfx_arg(d),
            gfx_arg(e),
            (mopl_color_t)gfx_arg(f)
        );

    } else if (strcmp(command, "line") == 0) {

        if (!gfx_next_token(&p, a, sizeof(a)))
            return;

        if (!gfx_next_token(&p, b, sizeof(b)))
            return;

        if (!gfx_next_token(&p, c, sizeof(c)))
            return;

        if (!gfx_next_token(&p, d, sizeof(d)))
            return;

        if (!gfx_next_token(&p, e, sizeof(e)))
            return;

        mopl_gfx_draw_line(
            gfx_arg(a),
            gfx_arg(b),
            gfx_arg(c),
            gfx_arg(d),
            (mopl_color_t)gfx_arg(e)
        );

    } else if (strcmp(command, "circle") == 0) {

        if (!gfx_next_token(&p, a, sizeof(a)))
            return;

        if (!gfx_next_token(&p, b, sizeof(b)))
            return;

        if (!gfx_next_token(&p, c, sizeof(c)))
            return;

        if (!gfx_next_token(&p, d, sizeof(d)))
            return;

        mopl_gfx_draw_circle(
            gfx_arg(a),
            gfx_arg(b),
            gfx_arg(c),
            (mopl_color_t)gfx_arg(d)
        );

    } else if (strcmp(command, "text") == 0) {

        if (!gfx_next_token(&p, a, sizeof(a)))
            return;

        if (!gfx_next_token(&p, b, sizeof(b)))
            return;

        if (!gfx_next_token(&p, c, sizeof(c)))
            return;

        if (!gfx_next_token(&p, d, sizeof(d)))
            return;

        if (!gfx_next_token(&p, e, sizeof(e)))
            return;

        mopl_gfx_draw_text(
            a,
            gfx_arg(b),
            gfx_arg(c),
            gfx_arg(d),
            (mopl_color_t)gfx_arg(e)
        );
    }
}

// GUI forward declaration
// ============================================================================

static void execute_range(
    int start,
    int end
);

// ============================================================================
// Native macOS GUI
// ============================================================================

#ifdef __APPLE__

typedef struct GuiControl {
    char *name;
    NSView *view;
    struct GuiControl *next;
} GuiControl;

typedef struct GuiEvent {
    char *name;

    int start;
    int end;

    struct GuiEvent *next;
} GuiEvent;

static NSWindow *g_gui_window = nil;
static NSMutableArray *g_gui_targets = nil;

static GuiControl *g_gui_controls = NULL;
static GuiEvent *g_gui_events = NULL;

static GuiControl *gui_find_control(
    const char *name
) {
    for (GuiControl *c = g_gui_controls;
         c;
         c = c->next) {

        if (strcmp(c->name, name) == 0)
            return c;
    }

    return NULL;
}

static void gui_add_control(
    const char *name,
    NSView *view
) {
    GuiControl *old =
        gui_find_control(name);

    if (old) {
        old->view = view;
        return;
    }

    GuiControl *c =
        calloc(1, sizeof(*c));

    if (!c)
        die_oom();

    c->name = xstrdup(name);
    c->view = view;

    c->next = g_gui_controls;
    g_gui_controls = c;
}

static void gui_free_controls(void) {
    GuiControl *c = g_gui_controls;

    while (c) {
        GuiControl *next = c->next;

        free(c->name);
        free(c);

        c = next;
    }

    g_gui_controls = NULL;
}

static GuiEvent *gui_find_event(
    const char *name
) {
    for (GuiEvent *e = g_gui_events;
         e;
         e = e->next) {

        if (strcmp(e->name, name) == 0)
            return e;
    }

    return NULL;
}

static void gui_free_events(void) {
    GuiEvent *e = g_gui_events;

    while (e) {
        GuiEvent *next = e->next;

        free(e->name);
        free(e);

        e = next;
    }

    g_gui_events = NULL;
}

static void gui_add_event(
    const char *name,
    int start,
    int end
) {
    GuiEvent *e =
        gui_find_event(name);

    if (e) {
        e->start = start;
        e->end = end;
        return;
    }

    e = calloc(1, sizeof(*e));

    if (!e)
        die_oom();

    e->name = xstrdup(name);
    e->start = start;
    e->end = end;

    e->next = g_gui_events;
    g_gui_events = e;
}

@interface MOPLGUIAction : NSObject

@property(nonatomic, assign) const char *eventName;

- (void)trigger:(id)sender;

@end

@implementation MOPLGUIAction

- (void)trigger:(id)sender {
    (void)sender;

    if (!self.eventName)
        return;

    GuiEvent *event =
        gui_find_event(self.eventName);

    if (!event)
        return;

    execute_range(
        event->start,
        event->end
    );
}

@end

static void gui_index_events(void) {
    gui_free_events();

    for (int i = 0;
         i < g_program.count;
         ++i) {

        char line[MAX_LINE];

        snprintf(
            line,
            sizeof(line),
            "%s",
            g_program.lines[i]
        );

        strip_comment(line);
        trim_in_place(line);

        if (!starts_with_ci(
                line,
                "OnClick"))
            continue;

        const char *p =
            after_first_word(line);

        char name[MAX_NAME];

        p = next_token(
            p,
            name,
            sizeof(name)
        );

        if (!*name)
            continue;

        int end =
            find_matching_block(
                i,
                g_program.count,
                BLOCK_CLICK,
                NULL
            );

        if (end < 0)
            continue;

        gui_add_event(
            name,
            i + 1,
            end
        );

        i = end;
    }
}

static NSRect gui_rect_from_top_left(
    CGFloat x,
    CGFloat y,
    CGFloat width,
    CGFloat height,
    CGFloat window_height
) {
    return NSMakeRect(
        x,
        window_height - y - height,
        width,
        height
    );
}

static void gui_init_window(
    const char *title,
    int width,
    int height
) {
    if (width < 200)
        width = 200;

    if (height < 120)
        height = 120;

    if (g_gui_window) {
        [g_gui_window close];
        g_gui_window = nil;
    }

    NSRect frame =
        NSMakeRect(
            0,
            0,
            width,
            height
        );

    NSUInteger style =
        NSWindowStyleMaskTitled |
        NSWindowStyleMaskClosable |
        NSWindowStyleMaskMiniaturizable |
        NSWindowStyleMaskResizable;

    g_gui_window =
        [[NSWindow alloc]
            initWithContentRect:frame
            styleMask:style
            backing:NSBackingStoreBuffered
            defer:NO];

    [g_gui_window setTitle:
        [NSString stringWithUTF8String:title]];

    [g_gui_window center];

    [g_gui_window makeKeyAndOrderFront:nil];

    [g_gui_window setReleasedWhenClosed:NO];

    if (!g_gui_targets)
        g_gui_targets = [NSMutableArray array];

    [g_gui_targets removeAllObjects];

    gui_free_controls();

    gui_index_events();
}

static void gui_add_text(
    const char *text,
    CGFloat x,
    CGFloat y,
    CGFloat font_size
) {
    if (!g_gui_window)
        return;

    NSTextField *label =
        [[NSTextField alloc]
            initWithFrame:
                gui_rect_from_top_left(
                    x,
                    y,
                    500,
                    font_size + 12,
                    g_gui_window.contentView.bounds.size.height
                )];

    [label setStringValue:
        [NSString stringWithUTF8String:text]];

    [label setFont:
        [NSFont systemFontOfSize:font_size]];

    [label setEditable:NO];
    [label setSelectable:NO];
    [label setBordered:NO];
    [label setDrawsBackground:NO];

    [g_gui_window.contentView
        addSubview:label];

    [g_gui_targets addObject:label];
}

static void gui_add_input(
    const char *name,
    CGFloat x,
    CGFloat y,
    CGFloat width,
    CGFloat height
) {
    if (!g_gui_window)
        return;

    NSTextField *field =
        [[NSTextField alloc]
            initWithFrame:
                gui_rect_from_top_left(
                    x,
                    y,
                    width,
                    height,
                    g_gui_window.contentView.bounds.size.height
                )];

    [field setPlaceholderString:
        [NSString stringWithUTF8String:name]];

    [g_gui_window.contentView
        addSubview:field];

    [g_gui_targets addObject:field];

    gui_add_control(
        name,
        field
    );
}

static void gui_add_button(
    const char *name,
    const char *caption,
    CGFloat x,
    CGFloat y,
    CGFloat width,
    CGFloat height
) {
    if (!g_gui_window)
        return;

    NSButton *button =
        [[NSButton alloc]
            initWithFrame:
                gui_rect_from_top_left(
                    x,
                    y,
                    width,
                    height,
                    g_gui_window.contentView.bounds.size.height
                )];

    [button setTitle:
        [NSString stringWithUTF8String:caption]];

    [button setButtonType:NSButtonTypeMomentaryPushIn];
    [button setBezelStyle:NSBezelStyleRounded];

    MOPLGUIAction *action =
        [[MOPLGUIAction alloc] init];

    action.eventName = xstrdup(name);

    [g_gui_targets addObject:action];

    [button setTarget:action];
    [button setAction:@selector(trigger:)];

    [g_gui_window.contentView
        addSubview:button];

    [g_gui_targets addObject:button];

    gui_add_control(
        name,
        button
    );
}

static void gui_set_text(
    const char *name,
    const char *text
) {
    GuiControl *c =
        gui_find_control(name);

    if (!c || !c->view)
        return;

    NSString *value =
        [NSString stringWithUTF8String:text];

    if ([c->view isKindOfClass:
            [NSTextField class]]) {

        NSTextField *field =
            (NSTextField *)c->view;

        [field setStringValue:value];

    } else if ([c->view isKindOfClass:
                   [NSButton class]]) {

        NSButton *button =
            (NSButton *)c->view;

        [button setTitle:value];
    }
}

static char *gui_get_text(
    const char *name
) {
    GuiControl *c =
        gui_find_control(name);

    if (!c || !c->view)
        return xstrdup("");

    if ([c->view isKindOfClass:
            [NSTextField class]]) {

        NSTextField *field =
            (NSTextField *)c->view;

        return xstrdup(
            field.stringValue.UTF8String
                ? field.stringValue.UTF8String
                : ""
        );
    }

    if ([c->view isKindOfClass:
            [NSButton class]]) {

        NSButton *button =
            (NSButton *)c->view;

        return xstrdup(
            button.title.UTF8String
                ? button.title.UTF8String
                : ""
        );
    }

    return xstrdup("");
}

static void handle_gui_line(
    char *line,
    int *ip,
    int end
) {
    if (starts_with_ci(line, "Window")) {
        const char *p =
            after_first_word(line);

        char title[256];
        char ws[64];
        char hs[64];

        p = next_token(
            p,
            title,
            sizeof(title)
        );

        p = next_token(
            p,
            ws,
            sizeof(ws)
        );

        p = next_token(
            p,
            hs,
            sizeof(hs)
        );

        gui_init_window(
            title,
            (int)resolve_operand(ws),
            (int)resolve_operand(hs)
        );

        return;
    }

    if (starts_with_ci(line, "Text")) {
        const char *p =
            after_first_word(line);

        char *text =
            NULL;

        char xs[64];
        char ys[64];
        char size_s[64];

        if (*p == '"' || *p == '\'') {
            text = decode_remainder(p);

            // For Text, position/size follows the quoted text.
            const char *quote_end = p + 1;
            char quote = *p;

            while (*quote_end &&
                   *quote_end != quote) {

                if (*quote_end == '\\' &&
                    quote_end[1])
                    quote_end++;

                quote_end++;
            }

            if (*quote_end)
                quote_end++;

            p = quote_end;

        } else {
            text = decode_remainder(p);

            while (*p && !isspace((unsigned char)*p))
                p++;
        }

        p = next_token(
            p,
            xs,
            sizeof(xs)
        );

        p = next_token(
            p,
            ys,
            sizeof(ys)
        );

        p = next_token(
            p,
            size_s,
            sizeof(size_s)
        );

        gui_add_text(
            text,
            (CGFloat)resolve_operand(xs),
            (CGFloat)resolve_operand(ys),
            (CGFloat)resolve_operand(size_s)
        );

        free(text);

        return;
    }

    if (starts_with_ci(line, "Button")) {
        const char *p =
            after_first_word(line);

        char name[MAX_NAME];
        char caption[256];
        char xs[64];
        char ys[64];
        char ws[64];
        char hs[64];

        p = next_token(
            p,
            name,
            sizeof(name)
        );

        p = next_token(
            p,
            caption,
            sizeof(caption)
        );

        p = next_token(
            p,
            xs,
            sizeof(xs)
        );

        p = next_token(
            p,
            ys,
            sizeof(ys)
        );

        p = next_token(
            p,
            ws,
            sizeof(ws)
        );

        p = next_token(
            p,
            hs,
            sizeof(hs)
        );

        gui_add_button(
            name,
            caption,
            (CGFloat)resolve_operand(xs),
            (CGFloat)resolve_operand(ys),
            (CGFloat)resolve_operand(ws),
            (CGFloat)resolve_operand(hs)
        );

        return;
    }

    if (starts_with_ci(line, "Input")) {
        const char *p =
            after_first_word(line);

        char name[MAX_NAME];
        char xs[64];
        char ys[64];
        char ws[64];
        char hs[64];

        p = next_token(
            p,
            name,
            sizeof(name)
        );

        p = next_token(
            p,
            xs,
            sizeof(xs)
        );

        p = next_token(
            p,
            ys,
            sizeof(ys)
        );

        p = next_token(
            p,
            ws,
            sizeof(ws)
        );

        p = next_token(
            p,
            hs,
            sizeof(hs)
        );

        gui_add_input(
            name,
            (CGFloat)resolve_operand(xs),
            (CGFloat)resolve_operand(ys),
            (CGFloat)resolve_operand(ws),
            (CGFloat)resolve_operand(hs)
        );

        return;
    }

    if (starts_with_ci(line, "SetText")) {
        const char *p =
            after_first_word(line);

        char name[MAX_NAME];

        p = next_token(
            p,
            name,
            sizeof(name)
        );

        char *text =
            decode_remainder(p);

        gui_set_text(
            name,
            text
        );

        free(text);

        return;
    }

    if (starts_with_ci(line, "GetText")) {
        const char *p =
            after_first_word(line);

        char name[MAX_NAME];

        p = next_token(
            p,
            name,
            sizeof(name)
        );

        char *text =
            gui_get_text(name);

        Tag *t =
            find_tag("_");

        if (!t)
            t = create_tag(
                "_",
                TAG_BLEA,
                0,
                ""
            );

        t->type = TAG_BLEA;

        free(t->str_value);

        t->str_value = text;

        return;
    }

    if (starts_with_ci(line, "OnClick")) {
        int close_idx =
            find_matching_block(
                *ip,
                end,
                BLOCK_CLICK,
                NULL
            );

        if (close_idx >= 0)
            *ip = close_idx;

        return;
    }

    if (starts_with_ci(line, "EndClick") ||
        starts_with_ci(line, "EndWindow")) {
        return;
    }
}

#endif

// ============================================================================
// Logic definitions
// ============================================================================

static void index_logic_blocks(void) {
    for (int i = 0;
         i < g_program.count;
         ++i) {

        char line[MAX_LINE];

        snprintf(
            line,
            sizeof(line),
            "%s",
            g_program.lines[i]
        );

        strip_comment(line);
        trim_in_place(line);

        if (!starts_with_ci(line, "logic "))
            continue;

        const char *p =
            after_first_word(line);

        char name[MAX_NAME];

        p = next_token(
            p,
            name,
            sizeof(name)
        );

        if (!*name)
            continue;

        LogicDef def = {0};

        def.name = xstrdup(name);
        def.start = i + 1;

        while (*p) {
            char param[MAX_NAME];

            p = next_token(
                p,
                param,
                sizeof(param)
            );

            if (!*param)
                break;

            char **next =
                realloc(
                    def.params,
                    (size_t)(def.param_count + 1) *
                    sizeof(*next)
                );

            if (!next)
                die_oom();

            def.params = next;

            def.params[
                def.param_count++
            ] = xstrdup(param);
        }

        int close_idx =
            find_matching_block(
                i,
                g_program.count,
                BLOCK_LOGIC,
                NULL
            );

        if (close_idx < 0) {
            free(def.name);

            for (int j = 0;
                 j < def.param_count;
                 ++j)
                free(def.params[j]);

            free(def.params);

            continue;
        }

        def.end = close_idx;

        logic_add(def);

        i = close_idx;
    }
}

// ============================================================================
// Saved tags for logic calls
// ============================================================================

typedef struct {
    char *name;

    int existed;

    TagType type;
    long long num_value;
    char *str_value;
} SavedTag;

static void saved_tag_capture(
    SavedTag *s,
    const char *name
) {
    memset(s, 0, sizeof(*s));

    s->name = xstrdup(name);

    Tag *t = find_tag(name);

    if (!t)
        return;

    s->existed = 1;
    s->type = t->type;
    s->num_value = t->num_value;

    if (t->str_value)
        s->str_value = xstrdup(t->str_value);
}

static void saved_tag_free(SavedTag *s) {
    free(s->name);
    free(s->str_value);

    memset(s, 0, sizeof(*s));
}

static void restore_tag(
    const SavedTag *s
) {
    Tag *t = find_tag(s->name);

    if (s->existed) {
        if (!t)
            t = create_tag(
                s->name,
                s->type,
                s->num_value,
                s->str_value
            );

        t->type = s->type;
        t->num_value = s->num_value;

        free(t->str_value);

        t->str_value =
            s->str_value ?
                xstrdup(s->str_value) :
                NULL;

    } else {
        if (!t)
            return;

        Tag **pp = &g_tags;

        while (*pp && *pp != t)
            pp = &(*pp)->next;

        if (*pp == t)
            *pp = t->next;

        free(t->name);
        free(t->str_value);
        free(t);
    }
}

// ============================================================================
// Execution
// ============================================================================

static void handle_check(
    int *ip,
    int end,
    char *line
) {
    int else_idx = -1;

    int close_idx =
        find_matching_block(
            *ip,
            end,
            BLOCK_CHECK,
            &else_idx
        );

    if (close_idx < 0) {
        report_runtime(
            "missing EndCheck",
            line
        );

        *ip = end;
        return;
    }

    const char *expr =
        after_first_word(line);

    int ok = 0;

    int condition =
        (int)eval_condition(
            expr,
            &ok
        );

    if (!ok)
        report_runtime(
            "could not parse Check condition",
            line
        );

    int true_start =
        *ip + 1;

    int true_end =
        else_idx >= 0 ?
            else_idx :
            close_idx;

    int false_start =
        else_idx >= 0 ?
            else_idx + 1 :
            close_idx;

    if (condition) {
        execute_range(
            true_start,
            true_end
        );
    } else if (else_idx >= 0) {
        execute_range(
            false_start,
            close_idx
        );
    }

    *ip = close_idx;
}

static void handle_cycle(
    int *ip,
    int end,
    char *line
) {
    int close_idx =
        find_matching_block(
            *ip,
            end,
            BLOCK_CYCLE,
            NULL
        );

    if (close_idx < 0) {
        report_runtime(
            "missing EndCycle",
            line
        );

        *ip = end;
        return;
    }

    const char *p =
        after_first_word(line);

    long long n =
        resolve_operand(p);

    if (n < 0)
        n = 0;

    if (n > MAX_CYCLE_REPEATS)
        n = MAX_CYCLE_REPEATS;

    int old_loop = g_in_loop;

    g_in_loop = 1;

    for (long long i = 0;
         i < n;
         ++i) {

        execute_range(
            *ip + 1,
            close_idx
        );

        if (g_return_active)
            break;
    }

    g_in_loop = old_loop;

    *ip = close_idx;
}

static void handle_return(char *line) {
    const char *p =
        after_first_word(line);

    if (*p) {
        char tok[256];

        next_token(
            p,
            tok,
            sizeof(tok)
        );

        store_op_result(
            resolve_operand(tok)
        );
    }

    g_return_active = 1;
}

static void invoke_logic(
    LogicDef *fn,
    const char *arg_text
) {
    if (!fn)
        return;

    if (g_call_depth >= MAX_CALL_DEPTH) {
        report_runtime(
            "maximum logic call depth reached",
            fn->name
        );

        return;
    }

    SavedTag *saved = NULL;

    if (fn->param_count > 0) {
        saved = calloc(
            (size_t)fn->param_count,
            sizeof(*saved)
        );

        if (!saved)
            die_oom();
    }

    const char *p = arg_text;

    for (int i = 0;
         i < fn->param_count;
         ++i) {

        saved_tag_capture(
            &saved[i],
            fn->params[i]
        );

        char tok[256];

        const char *next =
            next_token(
                p,
                tok,
                sizeof(tok)
            );

        p = next;

        Tag *existing =
            find_tag(fn->params[i]);

        if (!*tok) {
            if (existing) {
                existing->type = TAG_BLEA;

                free(existing->str_value);

                existing->str_value =
                    xstrdup("");

            } else {
                create_tag(
                    fn->params[i],
                    TAG_BLEA,
                    0,
                    ""
                );
            }

        } else {
            Tag *argtag =
                find_tag(tok);

            if (argtag) {
                if (existing) {
                    existing->type =
                        argtag->type;

                    existing->num_value =
                        argtag->num_value;

                    free(existing->str_value);

                    existing->str_value =
                        argtag->str_value ?
                            xstrdup(
                                argtag->str_value
                            ) :
                            NULL;

                } else {
                    create_tag(
                        fn->params[i],
                        argtag->type,
                        argtag->num_value,
                        argtag->str_value
                    );
                }

            } else if (
                strcmp(tok, "true") == 0 ||
                strcmp(tok, "false") == 0
            ) {
                if (existing) {
                    existing->type =
                        TAG_STATE;

                    existing->num_value =
                        resolve_operand(tok);

                    free(existing->str_value);
                    existing->str_value = NULL;

                } else {
                    create_tag(
                        fn->params[i],
                        TAG_STATE,
                        resolve_operand(tok),
                        NULL
                    );
                }

            } else {
                char *endptr = NULL;

                errno = 0;

                long long v =
                    strtoll(
                        tok,
                        &endptr,
                        10
                    );

                if (
                    errno == 0 &&
                    endptr &&
                    *endptr == '\0'
                ) {
                    if (existing) {
                        existing->type =
                            TAG_NUM;

                        existing->num_value = v;

                        free(existing->str_value);
                        existing->str_value = NULL;

                    } else {
                        create_tag(
                            fn->params[i],
                            TAG_NUM,
                            v,
                            NULL
                        );
                    }

                } else {
                    if (existing) {
                        existing->type =
                            TAG_BLEA;

                        free(existing->str_value);

                        existing->str_value =
                            xstrdup(tok);

                    } else {
                        create_tag(
                            fn->params[i],
                            TAG_BLEA,
                            0,
                            tok
                        );
                    }
                }
            }
        }
    }

    int old_return =
        g_return_active;

    g_return_active = 0;

    g_call_depth++;

    execute_range(
        fn->start,
        fn->end
    );

    g_call_depth--;

    g_return_active =
        old_return;

    for (int i = fn->param_count - 1;
         i >= 0;
         --i) {

        restore_tag(&saved[i]);
        saved_tag_free(&saved[i]);
    }

    free(saved);
}

static void handle_call(char *line) {
    const char *p =
        after_first_word(line);

    char name[MAX_NAME];

    p = next_token(
        p,
        name,
        sizeof(name)
    );

    if (!*name)
        return;

    LogicDef *fn =
        find_logic(name);

    if (!fn) {
        Tag *t = find_tag(name);

        if (
            t &&
            t->type == TAG_LOGIC &&
            t->str_value
        ) {
            fn = find_logic(
                t->str_value
            );
        }
    }

    if (!fn) {
        char msg[256];

        snprintf(
            msg,
            sizeof(msg),
            "unknown logic '%s'",
            name
        );

        report_runtime(
            msg,
            line
        );

        return;
    }

    invoke_logic(
        fn,
        p
    );
}

// ============================================================================
// Dispatcher
// ============================================================================

static void dispatch_line(
    char *line,
    int *ip,
    int end
) {
    char work[MAX_LINE];

    snprintf(
        work,
        sizeof(work),
        "%s",
        line
    );

    strip_comment(work);
    trim_in_place(work);

    if (!*work)
        return;

    if (starts_with(work, "Tag.")) {
        handle_tag_line(work);

    } else if (starts_with_ci(
                   work,
                   "Terminal")) {

        handle_terminal_line(work);

    } else if (starts_with_ci(
                   work,
                   "Op of")) {

        handle_op_line(work);

    } else if (starts_with_ci(
                   work,
                   "Dif in")) {

        handle_dif_line(work);

    } else if (starts_with_ci(
                   work,
                   "Check")) {

        handle_check(
            ip,
            end,
            work
        );

    } else if (starts_with_ci(
                   work,
                   "Cycle")) {

        handle_cycle(
            ip,
            end,
            work
        );

    } else if (starts_with_ci(
                   work,
                   "Call")) {

        handle_call(work);

    } else if (starts_with_ci(
                   work,
                   "Return")) {

        handle_return(work);

    } else if (starts_with_ci(
                   work,
                   "Input")) {

#ifdef __APPLE__
        // On macOS, Input is ambiguous between the traditional
        // terminal command and the GUI control. GUI Input is handled
        // only when a Window is active and the line has GUI syntax.
        if (g_gui_window) {
            handle_gui_line(
                work,
                ip,
                end
            );
        } else {
            handle_input_line(work);
        }
#else
        handle_input_line(work);
#endif

    } else if (starts_with_ci(
                   work,
                   "Gfx.")) {

        handle_gfx_line(work);

#ifdef __APPLE__

    } else if (
        starts_with_ci(work, "Window") ||
        starts_with_ci(work, "Text") ||
        starts_with_ci(work, "Button") ||
        starts_with_ci(work, "SetText") ||
        starts_with_ci(work, "GetText") ||
        starts_with_ci(work, "OnClick") ||
        starts_with_ci(work, "EndClick") ||
        starts_with_ci(work, "EndWindow")
    ) {

        handle_gui_line(
            work,
            ip,
            end
        );

#endif

    } else if (starts_with_ci(
                   work,
                   "logic ")) {

        int close_idx =
            find_matching_block(
                *ip,
                end,
                BLOCK_LOGIC,
                NULL
            );

        if (close_idx >= 0)
            *ip = close_idx;

    } else if (
        starts_with_ci(work, "EndCheck") ||
        starts_with_ci(work, "Else") ||
        starts_with_ci(work, "EndCycle") ||
        starts_with_ci(work, "EndLogic") ||
        starts_with_ci(work, "EndClick") ||
        starts_with_ci(work, "EndWindow")
    ) {
        // Structural markers are consumed by
        // their owning block.

    } else {
        // Unknown lines remain harmless.
    }
}

static void execute_range(
    int start,
    int end
) {
    if (start < 0)
        start = 0;

    if (end > g_program.count)
        end = g_program.count;

    for (int i = start;
         i < end;
         ++i) {

        dispatch_line(
            g_program.lines[i],
            &i,
            end
        );

        if (g_return_active)
            break;
    }
}

// ============================================================================
// Main
// ============================================================================

static int load_program(
    const char *path
) {
    FILE *f =
        fopen(path, "r");

    if (!f) {
        fprintf(
            stderr,
            "error: could not open '%s': %s\n",
            path,
            strerror(errno)
        );

        return 0;
    }

    char line[MAX_LINE];

    while (fgets(
        line,
        sizeof(line),
        f
    )) {
        rstrip(line);

        if (
            g_program.count >=
            MAX_PROGRAM_LINES
        ) {
            fprintf(
                stderr,
                "error: program exceeds %d lines\n",
                MAX_PROGRAM_LINES
            );

            fclose(f);

            return 0;
        }

        program_add(
            &g_program,
            line
        );
    }

    fclose(f);

    return 1;
}

int main(
    int argc,
    char **argv
) {

#ifdef __APPLE__

    @autoreleasepool {

        if (
            argc < 3 ||
            strcmp(argv[1], "run") != 0
        ) {
            fprintf(
                stderr,
                "usage: mopl run <file>.mopl\n"
            );

            return 1;
        }

        [NSApplication sharedApplication];

        [NSApp setActivationPolicy:
            NSApplicationActivationPolicyRegular];

        g_gui_targets =
            [NSMutableArray array];

        srand(
            (unsigned int)time(NULL)
        );

        if (!load_program(argv[2]))
            return 1;

        index_logic_blocks();

        gui_index_events();

        execute_range(
            0,
            g_program.count
        );

        if (g_gui_window || mopl_gfx_is_active()) {
            [NSApp activateIgnoringOtherApps:YES];

            if (g_gui_window)
                [g_gui_window makeKeyAndOrderFront:nil];

            [NSApp run];
        }

        gui_free_events();

        gui_free_controls();

        if (g_gui_window) {
            [g_gui_window close];
            g_gui_window = nil;
        }

        mopl_gfx_shutdown();

        free_logic_table();
        free_program(&g_program);
        free_tags();

        return 0;
    }

#else

    if (
        argc < 3 ||
        strcmp(argv[1], "run") != 0
    ) {
        fprintf(
            stderr,
            "usage: mopl run <file>.mopl\n"
        );

        return 1;
    }

    srand(
        (unsigned int)time(NULL)
    );

    if (!load_program(argv[2]))
        return 1;

    index_logic_blocks();

    execute_range(
        0,
        g_program.count
    );

    free_logic_table();
    free_program(&g_program);
    free_tags();

    return 0;

#endif
}