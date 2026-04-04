#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdarg.h>
#include <ctype.h>
#include <signal.h>
#include <unistd.h>
#include <sys/stat.h>
#ifndef _WIN32
#  include <sys/ioctl.h>
#  include <poll.h>
#else
#  include <windows.h>
#  include <io.h>       /* _get_osfhandle */
#  include <direct.h>   /* _mkdir */
#  define POLLIN 1
struct pollfd { int fd; short events; short revents; };
static int poll(struct pollfd *fds, unsigned int nfds, int timeout_ms) {
    if (nfds == 0) return 0;
    HANDLE h = (HANDLE)_get_osfhandle(fds[0].fd);
    if (h == INVALID_HANDLE_VALUE) return -1;
    DWORD ms = timeout_ms < 0 ? INFINITE : (DWORD)timeout_ms;
    DWORD r = WaitForSingleObject(h, ms);
    if (r == WAIT_OBJECT_0) { fds[0].revents = POLLIN; return 1; }
    return 0;
}
static char * strndup(const char * s, size_t n) {
    size_t i = 0;
    while (i < n && s[i]) i++;
    char * p = malloc(i + 1);
    if (p) { memcpy(p, s, i); p[i] = '\0'; }
    return p;
}
/* io.h declares 1-arg mkdir; override with mode-ignoring 2-arg form */
#  define mkdir(path, mode) _mkdir(path)
/* POSIX sigaction not available; stub using signal() */
struct sigaction { void (*sa_handler)(int); int sa_flags; };
static int sigaction(int sig,
        const struct sigaction * act, struct sigaction * old) {
    (void)old; signal(sig, act->sa_handler); return 0;
}
#endif
#include <curl/curl.h>

/*
 # AGENTS MUST READ:
   AGENTS.md PLAN.md STYLE.md and SKILLS.md
   files before editing code
 preseve this comment on all commits!
*/

#define codable

#ifdef rtti

#include <assert.h>

struct type_info {
    const char * n;
    size_t o;
    size_t b;
    const struct type_info * r;
    char k;
    bool a;
};

struct field_info {
    char * type;
    char * name;
    bool is_ptr;
    bool is_struct;
};

struct struct_info {
    char * name;
    struct field_info * fields;
    int count;
    int cap;
};

static struct struct_info * structs = NULL;

static int s_count = 0;
static int s_cap = 0;

static void * oom(void * p) {
    if (!p) {
        perror("oom");
        abort();
    }
    return p;
}

static void trim(char * s) {
    int l = (int)strlen(s);
    while (l > 0 && isspace(s[l - 1])) {
        s[--l] = 0;
    }
    char * p = s;
    while (*p && isspace(*p)) {
        p++;
        l--;
    }
    memmove(s, p, (size_t)(l + 1));
}

static void parse_field(char * line) {
    char * semi = strchr(line, ';');
    if (semi) {
        *semi = 0;
    }
    struct struct_info * si = &structs[s_count];
    if (si->count >= si->cap) {
        si->cap = si->cap == 0 ? 16 : si->cap * 2;
        si->fields = oom(realloc(si->fields,
                                 sizeof(struct field_info) * si->cap));
    }
    struct field_info * f = &si->fields[si->count];
    f->is_ptr = false;
    f->is_struct = false;
    char * p = line;
    if (strncmp(p, "const ", 6) == 0) {
        p += 6;
    }
    if (strncmp(p, "struct ", 7) == 0) {
        f->is_struct = true;
        p += 7;
    }
    char * star = strchr(p, '*');
    if (star) {
        f->is_ptr = true;
        char * end = star;
        while (end > p && isspace(*(end - 1))) {
            end--;
        }
        f->type = strndup(p, (size_t)(end - p));
        char * name_start = star;
        while (*name_start == '*' || isspace(*name_start)) {
            name_start++;
        }
        f->name = strdup(name_start);
    } else {
        char * last = strrchr(p, ' ');
        if (last) {
            f->type = strndup(p, (size_t)(last - p));
            f->name = strdup(last + 1);
        }
    }
    trim(f->type);
    trim(f->name);
    si->count++;
}

static void handle_line(char * line, bool * in) {
    if (!*in) {
        if (strncmp(line, "codable struct", 14) == 0) {
            if (s_count >= s_cap) {
                s_cap = s_cap == 0 ? 16 : s_cap * 2;
                structs = oom(realloc(structs,
                                      sizeof(struct struct_info) * s_cap));
            }
            char * p = line + 14;
            while (isspace(*p)) {
                p++;
            }
            char * e = p;
            while (*e && *e != ' ' && *e != '{') {
                e++;
            }
            structs[s_count].name = strndup(p, (size_t)(e - p));
            structs[s_count].count = 0;
            structs[s_count].cap = 0;
            structs[s_count].fields = NULL;
            *in = strchr(line, '{') != NULL;
        }
    } else {
        if (strchr(line, '}')) {
            *in = false;
            s_count++;
        } else {
            parse_field(line);
        }
    }
}

static void parse_file(const char * fn) {
    FILE * f = fopen(fn, "r");
    if (f) {
        char line[4096];
        bool in = false;
        bool reading = true;
        while (reading) {
            if (fgets(line, sizeof(line), f) == NULL) {
                reading = false;
            } else {
                trim(line);
                if (line[0] && strncmp(line, "//", 2) != 0) {
                    handle_line(line, &in);
                }
            }
        }
        fclose(f);
    }
}

static void print_rtti_header(void) {
    printf("#include <stddef.h>\n#include <stdbool.h>\n\n");
    printf("#ifndef struct_type_info\n#define struct_type_info\n");
    printf("struct type_info { const char * n; size_t o; size_t b; ");
    printf("const struct type_info * r; char k; bool a; };\n#endif\n\n");
}

static char kind(const char * t, bool is_struct, bool is_pointer) {
    char r = 'i';
    if (is_struct) {
        r = '{';
    } else if (strcmp(t, "char") == 0 && is_pointer) {
        r = 's';
    } else if (strcmp(t, "double") == 0 || strcmp(t, "float") == 0) {
        r = 'd';
    } else if (strcmp(t, "bool") == 0) {
        r = 'b';
    }
    return r;
}

static void print_struct_rtti(struct struct_info * s) {
    printf("const struct type_info %s_rtti[] = {\n", s->name);
    for (int j = 0; j < s->count; j++) {
        struct field_info * f = &s->fields[j];
        char k = kind(f->type, f->is_struct, f->is_ptr);
        bool arr = f->is_ptr && k != 's';
        printf("  { \"%s\", offsetof(struct %s, %s), ",
               f->name, s->name, f->name);
        if (arr) {
            if (f->is_struct) {
                printf("sizeof(struct %s), ", f->type);
            } else {
                printf("sizeof(%s), ", f->type);
            }
        } else {
            printf("sizeof(((struct %s*)0)->%s), ", s->name, f->name);
        }
        printf("%s%s, '%c', %s },\n",
               f->is_struct ? f->type : "NULL",
               f->is_struct ? "_rtti" : "", k,
               arr ? "true" : "false");
    }
    printf("  { NULL, 0, sizeof(struct %s),"
           " NULL, 0, false }\n};\n\n", s->name);
}

int main(int argc, char ** argv) {
    int res = 0;
    if (argc < 2) {
        res = 1;
    } else {
        parse_file(argv[1]);
        print_rtti_header();
        for (int i = 0; i < s_count; i++) {
            const char * nm = structs[i].name;
            printf("extern const struct type_info %s_rtti[];\n", nm);
        }
        printf("\n");
        for (int i = 0; i < s_count; i++) {
            print_struct_rtti(&structs[i]);
        }
    }
    return res;
}

#else

#ifndef struct_type_info
#define struct_type_info
struct type_info {
    const char * n;
    size_t o;
    size_t b;
    const struct type_info * r;
    char k;
    bool a;
};
#endif

// API Structs
codable struct shell_args {
    char * command;
};

codable struct ask_args {
    char * question;
};

codable struct web_search_args {
    char * query;
};

codable struct function_call {
    char * name;
    char * arguments;
};

codable struct tool_call {
    char * id;
    char * type;
    struct function_call function;
};

codable struct message {
    char * role;
    char * content;
    char * reasoning;
    char * reasoning_content;
    char * name;
    char * tool_call_id;
    struct tool_call * tool_calls;
};

codable struct delta {
    char * role;
    char * content;
    char * reasoning;
    char * reasoning_content;
    struct tool_call * tool_calls;
};

codable struct choice {
    int index;
    struct delta delta;
    char *finish_reason;
};

codable struct error_info {
    int code;
    char *message;
};

codable struct chunk {
    char *id;
    char *model;
    struct choice *choices;
    struct error_info error;
};

codable struct model_info {
    char * id;
    char * name;
    char * description;
};

codable struct model_list {
    struct model_info * data;
};

codable struct tool_desc {
    char * name;
    char * description;
    char * parameters;
};

codable struct tool {
    char * type;
    struct tool_desc function;
};

codable struct reasoning {
    char * effort;
};

codable struct hallucinated_tool {
    char *command;
    char *question;
};

codable struct request {
    char * model;
    struct message * messages;
    struct tool * tools;
    struct reasoning * reasoning;
    char * reasoning_effort;
    bool stream;
    int max_tokens;
};

codable struct context {
    char * api_key;
    char * model;
    struct message * messages;
    int message_count;
    int message_cap;
    bool free_mode;
    bool show_reasoning;
    int debug_level;
};

#include "build/rtti.h"

static size_t rtti_struct_bytes(const struct type_info * m) {
    const struct type_info * f = m;
    while (f->n) {
        f++;
    }
    return f->b;
}

static char * skip_ws(char * p) {
    while (*p && isspace((unsigned char)*p)) {
        p++;
    }
    return p;
}

static char * skip_value(char * j) {
    int d = 0;
    int in_str = 0;
    bool done = false;
    while (*j && !done) {
        if (*j == '"') {
            in_str = !in_str;
        } else if (*j == '\\' && in_str) {
            j++;
        } else if (!in_str) {
            if (*j == '{' || *j == '[') {
                d++;
            } else if (*j == '}' || *j == ']') {
                d--;
                if (d < 0) {
                    done = true;
                }
            } else if ((*j == ',' || *j == '}') && d == 0) {
                done = true;
            }
        }
        if (!done) {
            j++;
        }
    }
    return j;
}

struct measure_ctx {
    size_t array_bytes;
};

static char * measure_val(char * j, const struct type_info * m,
                         struct measure_ctx * ctx);

static char * measure_obj(char * j, const struct type_info * m,
                         struct measure_ctx * ctx) {
    if (*j == '{') {
        j = skip_ws(j + 1);
        while (*j && *j != '}') {
            if (*j == '"') {
                j++;
                const char * k = j;
                while (*j && *j != '"') {
                    j += (*j == '\\' && *(j + 1)) ? 2 : 1;
                }
                size_t klen = (size_t)(j - k);
                if (*j == '"') {
                    j++;
                }
                j = skip_ws(j);
                if (*j == ':') {
                    j = skip_ws(j + 1);
                }
                const struct type_info * f = m;
                int found = 0;
                while (f && f->n && !found) {
                    if (strlen(f->n) == klen &&
                            strncmp(f->n, k, klen) == 0) {
                        j = measure_val(j, f, ctx);
                        found = 1;
                    }
                    f++;
                }
                if (!found) {
                    j = skip_value(j);
                }
            }
            j = skip_ws(j);
            if (*j == ',') {
                j = skip_ws(j + 1);
            }
        }
        if (*j == '}') {
            j = skip_ws(j + 1);
        }
    }
    return j;
}

static char * measure_array(char * j, const struct type_info * m,
                           struct measure_ctx * ctx) {
    if (*j == '[') {
        j = skip_ws(j + 1);
        size_t n = 0;
        struct type_info em = *m;
        em.a = 0;
        while (*j && *j != ']') {
            char * prev = j;
            j = measure_val(j, &em, ctx);
            if (j != prev) {
                n++;
            } else {
                j++;
            }
            j = skip_ws(j);
            if (*j == ',') {
                j = skip_ws(j + 1);
            }
        }
        if (*j == ']') {
            j = skip_ws(j + 1);
        }
        ctx->array_bytes += (n + 1) * m->b;
    }
    return j;
}

static char * measure_val(char * j, const struct type_info * m,
                         struct measure_ctx * ctx) {
    char * res = j;
    if (m->a && *j == '[') {
        res = measure_array(j, m, ctx);
    } else if (m->k == '{' && *j == '{') {
        res = measure_obj(j, m->r, ctx);
    } else if (m->k == 's' && *j == '"') {
        j++;
        while (*j && *j != '"') {
            j += (*j == '\\' && *(j + 1)) ? 2 : 1;
        }
        if (*j == '"') {
            j++;
        }
        res = skip_ws(j);
    } else {
        char * p = j;
        j = skip_value(j);
        if (j == p && *j) {
            j++;
        }
        res = j;
    }
    return res;
}

struct parse_ctx {
    char * bump;
    char * bump_end;
};

static char * parse_val(char * j, void * p, const struct type_info * m,
                       struct parse_ctx * ctx);

static char *parse_str(char *j, const char **output) {
    if (*j == '"') {
        j++;
        char *dest = j;
        *output = j;
        while (*j && *j != '"') {
            if (*j == '\\' && *(j + 1)) {
                j++;
                if (*j == 'n') {
                    *dest++ = '\n';
                } else if (*j == 'r') {
                    *dest++ = '\r';
                } else if (*j == 't') {
                    *dest++ = '\t';
                } else if (*j == '"') {
                    *dest++ = '"';
                } else if (*j == '\\') {
                    *dest++ = '\\';
                } else {
                    *dest++ = '\\';
                    *dest++ = *j;
                }
            } else {
                *dest++ = *j;
            }
            j++;
        }
        if (*j == '"') {
            j++;
        }
        *dest = '\0';
    }
    return skip_ws(j);
}

static char * parse_obj(char * j, void * p, const struct type_info * m,
                       struct parse_ctx * ctx) {
    if (*j == '{') {
        j = skip_ws(j + 1);
        while (*j && *j != '}') {
            if (*j == '"') {
                const char * k = NULL;
                j = parse_str(j, &k);
                j = skip_ws(j);
                if (*j == ':') {
                    j = skip_ws(j + 1);
                }
                const struct type_info * f = m;
                int found = 0;
                while (f && f->n && !found) {
                    if (strcmp(f->n, k) == 0) {
                        j = parse_val(j, (char*)p + f->o, f, ctx);
                        found = 1;
                    }
                    f++;
                }
                if (!found) {
                    j = skip_value(j);
                }
            }
            j = skip_ws(j);
            if (*j == ',') {
                j = skip_ws(j + 1);
            }
        }
        if (*j == '}') {
            j = skip_ws(j + 1);
        }
    }
    return j;
}

static char * parse_array(char * j, void ** output, const struct type_info * m,
                         struct parse_ctx * ctx) {
    if (*j == '[') {
        j = skip_ws(j + 1);
        size_t elem = m->b;
        size_t n = 0;
        char * scan = j;
        int depth = 0;
        int in_str = 0;
        while (*scan && !(*scan == ']' && depth == 0 && !in_str)) {
            if (*scan == '"') {
                in_str = !in_str;
            } else if (*scan == '\\' && in_str) {
                scan++;
            } else if (!in_str) {
                if (*scan == '{' || *scan == '[') {
                    depth++;
                } else if (*scan == '}' || *scan == ']') {
                    depth--;
                } else if (*scan == ',' && depth == 0) {
                    n++;
                }
            }
            scan++;
        }
        if (n > 0 || (j != scan && *j != ']')) {
            n++;
        }
        size_t need = (n + 1) * elem;
        void * data = NULL;
        if (ctx->bump + need <= ctx->bump_end) {
            data = ctx->bump;
            memset(data, 0, need);
            ctx->bump += need;
        }
        if (data) {
            struct type_info em = *m;
            em.a = 0;
            size_t i = 0;
            while (*j && *j != ']') {
                char * prev = j;
                j = parse_val(j, (char*)data + i * elem, &em, ctx);
                if (j != prev) {
                    i++;
                } else {
                    j++;
                }
                j = skip_ws(j);
                if (*j == ',') {
                    j = skip_ws(j + 1);
                }
            }
            if (*j == ']') {
                j = skip_ws(j + 1);
            }
            *output = data;
        }
    }
    return j;
}

static char * parse_val(char * j, void * p, const struct type_info * m,
                       struct parse_ctx * ctx) {
    if (j && *j) {
        if (m->a) {
            j = parse_array(j, (void**)p, m, ctx);
        } else {
            char * prev = j;
            if (m->k == 's') {
                if (strncmp(j, "null", 4) == 0) {
                    *(const char**)p = NULL;
                    j = skip_ws(j + 4);
                } else {
                    const char * s = NULL;
                    j = parse_str(j, &s);
                    *(const char**)p = s;
                }
            } else if (m->k == 'i') {
                long long v = 0;
                if (*j == '"') {
                    v = strtoll(j + 1, &j, 10);
                    j++;
                } else {
                    v = strtoll(j, &j, 10);
                }
                if (m->b == 1) {
                    *(int8_t*)p = (int8_t)v;
                } else if (m->b == 2) {
                    *(int16_t*)p = (int16_t)v;
                } else if (m->b == 4) {
                    *(int32_t*)p = (int32_t)v;
                } else {
                    *(int64_t*)p = (int64_t)v;
                }
                j = skip_ws(j);
            } else if (m->k == 'd') {
                double v = 0;
                if (*j == '"') {
                    v = strtod(j + 1, &j);
                    j++;
                } else {
                    v = strtod(j, &j);
                }
                if (m->b == sizeof(float)) {
                    *(float*)p = (float)v;
                } else {
                    *(double*)p = v;
                }
                j = skip_ws(j);
            } else if (m->k == 'b') {
                if (strncmp(j, "true", 4) == 0) {
                    *(bool*)p = true;
                    j += 4;
                } else if (strncmp(j, "false", 5) == 0) {
                    *(bool*)p = false;
                    j += 5;
                }
                j = skip_ws(j);
            } else if (m->k == '{') {
                j = parse_obj(j, p, m->r, ctx);
            }
            if (j == prev) {
                while (*j && *j != ',' && *j != '}' && *j != ']') {
                    j++;
                }
            }
        }
    }
    return j;
}

void * decode(const struct type_info * ti, const char * json) {
    void * r = NULL;
    if (ti && json) {
        size_t sb = rtti_struct_bytes(ti);
        size_t jlen = strlen(json);
        struct measure_ctx mctx = {0};
        char * dup = strdup(json);
        if (dup) {
            char * p = skip_ws(dup);
            if (*p == '{') {
                measure_obj(p, ti, &mctx);
            }
            free(dup);
            size_t total = sb + jlen + 1 + mctx.array_bytes;
            char * block = calloc(1, total);
            if (block) {
                char * jcopy = block + sb;
                memcpy(jcopy, json, jlen + 1);
                struct parse_ctx pctx;
                pctx.bump = block + sb + jlen + 1;
                pctx.bump_end = block + total;
                p = skip_ws(jcopy);
                if (*p == '{') {
                    parse_obj(p, block, ti, &pctx);
                }
                r = block;
            }
        }
    }
    return r;
}

struct str {
    char * data;
    size_t count;
    size_t capacity;
};

static void str_append(struct str * s, const char * text) {
    if (s->data && text) {
        size_t len = strlen(text);
        if (s->count + len + 1 > s->capacity) {
            s->capacity = (s->capacity + len + 1) * 2;
            s->data = realloc(s->data, s->capacity);
        }
        if (s->data) {
            strcpy(s->data + s->count, text);
            s->count += len;
        }
    }
}

static void encode_val(struct str * s, void * val, const struct type_info * m);

static void encode_obj(struct str * s, void * p, const struct type_info * m) {
    str_append(s, "{");
    bool first = true;
    for (const struct type_info * f = m; f && f->n; f++) {
        void * fp = (char*)p + f->o;
        if (!((f->k == 's' || f->a) && *(void**)fp == NULL)) {
            if (!first) {
                str_append(s, ",");
            }
            first = false;
            str_append(s, "\"");
            str_append(s, f->n);
            str_append(s, "\":");
            encode_val(s, fp, f);
        }
    }
    str_append(s, "}");
}

static void encode_array(struct str * s, void * a,
        const struct type_info * m) {
    str_append(s, "[");
    struct type_info em = *m;
    em.a = 0;
    size_t size = m->b;
    void * p = a;
    bool first = true;
    bool done = false;
    while (!done) {
        bool is_null = true;
        if (m->k == 's') {
            is_null = *(char**)p == NULL;
        } else {
            for (size_t i = 0; i < size && is_null; i++) {
                if (((char*)p)[i] != 0) {
                    is_null = false;
                }
            }
        }
        if (is_null) {
            done = true;
        } else {
            if (!first) {
                str_append(s, ",");
            }
            first = false;
            encode_val(s, p, &em);
            p = (char*)p + size;
        }
    }
    str_append(s, "]");
}

static void encode_val(struct str * s, void * val,
        const struct type_info * m) {
    if (m->a) {
        encode_array(s, *(void**)val, m);
    } else {
        char buf[64];
        if (m->k == 's') {
            str_append(s, "\"");
            const char * src = *(const char**)val;
            while (src && *src) {
                if (*src == '"' || *src == '\\') {
                    str_append(s, "\\");
                }
                char t[2] = {*src++, '\0'};
                str_append(s, t);
            }
            str_append(s, "\"");
        } else if (m->k == 'i') {
            if (m->b == 1) {
                snprintf(buf, sizeof(buf), "%d", *(int8_t*)val);
            } else if (m->b == 2) {
                snprintf(buf, sizeof(buf), "%d", *(int16_t*)val);
            } else if (m->b == 4) {
                snprintf(buf, sizeof(buf), "%d", *(int32_t*)val);
            } else {
                snprintf(buf, sizeof(buf), "%lld", *(long long*)val);
            }
            str_append(s, buf);
        } else if (m->k == 'd') {
            snprintf(buf, sizeof(buf), "%g",
                     (m->b == sizeof(float)) ? *(float*)val : *(double*)val);
            str_append(s, buf);
        } else if (m->k == 'b') {
            str_append(s, *(bool*)val ? "true" : "false");
        } else if (m->k == '{') {
            encode_obj(s, val, m->r);
        }
    }
}

char * encode(const struct type_info * rtti, void * p) {
    char * r = NULL;
    if (p && rtti) {
        struct str s = {NULL, 0, 256};
        s.data = malloc(s.capacity);
        if (s.data) {
            s.data[0] = '\0';
            encode_obj(&s, p, rtti);
            r = s.data;
        }
    }
    return r;
}

struct sb {
    char * data;
    size_t count;
    size_t cap;
};

static void sb_init(struct sb * b) {
    b->count = 0;
    b->cap = 256;
    b->data = malloc(b->cap);
    b->data[0] = 0;
}

static void sb_put(struct sb * b, const char * s, size_t n) {
    if (b->count + n + 1 > b->cap) {
        while (b->count + n + 1 > b->cap) {
            b->cap *= 2;
        }
        b->data = realloc(b->data, b->cap);
    }
    memcpy(b->data + b->count, s, n);
    b->count += n;
    b->data[b->count] = 0;
}

static void sb_free(struct sb * b) {
    free(b->data);
    b->data = NULL;
    b->count = 0;
    b->cap = 0;
}

struct stream_state {
    struct sb buf;
    struct sb tool_args;
    struct sb content;
    struct sb reasoning;
    char tool_name[256];
    char tool_id[256];
    bool in_tool_call;
    bool show_reasoning;
    bool timed_out;
    struct context * ctx;
    time_t start_time;
    time_t last_chunk_time;
    double avg_interval; /* EMA of inter-chunk gap in seconds */
    int chunk_count;
};

static void add_message(struct context * ctx, const char * role,
                        const char * content, const char * name,
                        const char * tool_call_id) {
    if (ctx->message_count >= ctx->message_cap - 1) {
        ctx->message_cap = ctx->message_cap == 0 ? 8 : ctx->message_cap * 2;
        ctx->messages = realloc(ctx->messages,
                                ctx->message_cap * sizeof(struct message));
    }
    struct message * m = &ctx->messages[ctx->message_count++];
    m->role = role ? strdup(role) : NULL;
    m->content = content ? strdup(content) : NULL;
    m->reasoning = NULL;
    m->reasoning_content = NULL;
    m->name = name ? strdup(name) : NULL;
    m->tool_call_id = tool_call_id ? strdup(tool_call_id) : NULL;
    m->tool_calls = NULL;
    memset(&ctx->messages[ctx->message_count], 0, sizeof(struct message));
}

static char * execute_shell(const char * args) {
    char * res = NULL;
    struct shell_args * a = decode(shell_args_rtti, args);
    if (!a || !a->command) {
        res = strdup("Error: JSON/args");
    } else {
        printf("\n\033[36m> %s\033[0m\n", a->command);
        struct sb out;
        sb_init(&out);
        FILE * fp = popen(a->command, "r");
        if (fp) {
            char buf[1024];
            bool reading = true;
            while (reading) {
                if (fgets(buf, sizeof(buf), fp) == NULL) {
                    reading = false;
                } else {
                    sb_put(&out, buf, strlen(buf));
                    printf("%s", buf);
                }
            }
            pclose(fp);
            res = strdup(out.data);
        } else {
            res = strdup("Error: popen");
        }
        sb_free(&out);
    }
    free(a);
    return res;
}

static char * ask_user(const char * args) {
    char * res = NULL;
    struct ask_args * a = decode(ask_args_rtti, args);
    if (!a || !a->question) {
        res = strdup("Error: JSON/args");
    } else {
        printf("\n\033[33m? %s\033[0m\n", a->question);
        printf("\033[32m[user input]>\033[0m\n");
        fflush(stdout);
        char line[4096];
        if (fgets(line, sizeof(line), stdin)) {
            size_t l = strlen(line);
            while (l > 0 && (line[l - 1] == '\n' || line[l - 1] == '\r')) {
                line[--l] = 0;
            }
            res = strdup(line);
        } else {
            res = strdup("Error: input EOF");
        }
    }
    free(a);
    return res;
}

static size_t sb_write_cb(
        void * ptr, size_t sz, size_t nm, void * up) {
    sb_put((struct sb *)up, (char*)ptr, sz * nm);
    return sz * nm;
}

static char * web_search(const char * args) {
    char * res = NULL;
    struct web_search_args * a =
        decode(web_search_args_rtti, args);
    if (!a || !a->query) {
        res = strdup("Error: missing query");
    } else {
        CURL * curl = curl_easy_init();
        if (!curl) {
            res = strdup("Error: curl init");
        } else {
            char * eq = curl_easy_escape(curl, a->query, 0);
            char url[2048];
            snprintf(url, sizeof(url),
                "https://api.duckduckgo.com/?q=%s"
                "&format=json&no_html=1&skip_disambig=1",
                eq);
            curl_free(eq);
            struct sb out;
            sb_init(&out);
            curl_easy_setopt(curl, CURLOPT_URL, url);
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,
                sb_write_cb);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &out);
            curl_easy_setopt(curl,
                CURLOPT_FOLLOWLOCATION, 1L);
            curl_easy_setopt(curl, CURLOPT_USERAGENT,
                "orca/1.0");
            CURLcode rc = curl_easy_perform(curl);
            curl_easy_cleanup(curl);
            if (rc == CURLE_OK && out.count > 0) {
                res = out.data;
                out.data = NULL;
            } else {
                sb_free(&out);
                res = strdup("Error: search failed");
            }
        }
    }
    free(a);
    return res;
}

static char * execute_tool(const char * name, const char * args) {
    char * res = NULL;
    if (strcmp(name, "run_shell_command") == 0) {
        res = execute_shell(args);
    } else if (strcmp(name, "ask_user") == 0) {
        res = ask_user(args);
    } else if (strcmp(name, "web_search") == 0) {
        res = web_search(args);
    } else {
        res = strdup("Error: Unknown tool");
    }
    return res;
}

volatile sig_atomic_t thinking = 0;
volatile sig_atomic_t exit_requested = 0;
static time_t last_sigint = 0;

static void handle_sigint(int sig) {
    (void)sig;
    if (exit_requested) {
        _exit(0);
    } else if (thinking) {
        thinking = 0;
        last_sigint = time(NULL);
        const char * m = "\n\033[33m[Interrupted]\033[0m\n";
        write(1, m, strlen(m));
    } else {
        time_t now = time(NULL);
        if (last_sigint > 0 && now - last_sigint < 2) {
            exit_requested = 1;
        } else {
            const char * m = "\nPress Ctrl+C again to exit.\n";
            write(1, m, strlen(m));
            last_sigint = now;
        }
    }
}

static void parse_tool_call(struct stream_state * s, struct tool_call * tc) {
    if (tc->function.name) {
        strncpy(s->tool_name, tc->function.name, sizeof(s->tool_name) - 1);
        if (tc->id) {
            strncpy(s->tool_id, tc->id, sizeof(s->tool_id) - 1);
        }
        s->in_tool_call = true;
    }
    if (tc->function.arguments) {
        sb_put(&s->tool_args, tc->function.arguments,
               strlen(tc->function.arguments));
    }
}

#define ANSI_COLOR_GRAY  "\x1b[90m"
#define ANSI_COLOR_RESET "\x1b[0m"

static void parse_delta(struct stream_state * s, struct delta * d) {
    char * rea = d->reasoning
        ? d->reasoning : d->reasoning_content;
    if (rea && s->show_reasoning) {
        sb_put(&s->reasoning, rea, strlen(rea));
        printf(ANSI_COLOR_GRAY "%s", rea);
        fflush(stdout);
    }
    if (d->content && d->content[0] != '\0') {
        if (s->reasoning.count > 0 && s->content.count == 0) {
            printf(ANSI_COLOR_RESET "\n");
        }
        printf(ANSI_COLOR_RESET "%s", d->content);
        fflush(stdout);
        sb_put(&s->content, d->content, strlen(d->content));
    }
    if (d->tool_calls) {
        parse_tool_call(s, &d->tool_calls[0]);
    }
}

static size_t write_cb(void * ptr, size_t sz, size_t nm, void * up) {
    size_t realsz = sz * nm;
    struct stream_state * s = (struct stream_state*)up;
    time_t now = time(NULL);
    if (s->last_chunk_time > 0) {
        double dt = difftime(now, s->last_chunk_time);
        if (dt > 0) {
            double r = s->avg_interval;
            s->avg_interval = r > 0 ? r * 0.8 + dt * 0.2 : dt;
        }
    }
    s->last_chunk_time = now;
    s->chunk_count++;
    if (s->ctx->debug_level > 1) {
        fprintf(stderr, "[DEBUG] write_cb got %zu bytes\n", realsz);
    }
    sb_put(&s->buf, ptr, realsz);
    char * start = s->buf.data;
    char * nl = strchr(start, '\n');
    while (nl && !exit_requested) {
        *nl = 0;
        char * line = start;
        if (s->ctx->debug_level > 1) {
            fprintf(stderr, "[DEBUG] line: %s\n", line);
        }
        if (strncmp(line, "data: ", 6) == 0 &&
                strcmp(line + 6, "[DONE]") != 0) {
            if (s->ctx->debug_level > 0) {
                fprintf(stderr, "[DEBUG] SSE data: %s\n", line + 6);
            }
            struct chunk * c = decode(chunk_rtti, line + 6);
            if (c) {
                if (c->error.code != 0) {
                    fprintf(stderr,
                        "\n\033[31m[API Error %d]: %s\033[0m\n",
                        c->error.code,
                        c->error.message ? c->error.message
                                         : "Unknown");
                    thinking = 0;
                } else if (c->choices) {
                    parse_delta(s, &c->choices[0].delta);
                }
                free(c);
            }
        } else if (line[0] == '{') {
            struct chunk * ec = decode(chunk_rtti, line);
            if (ec && ec->error.code != 0) {
                fprintf(stderr,
                    "\n\033[31m[API Error %d]: %s\033[0m\n",
                    ec->error.code,
                    ec->error.message ? ec->error.message
                                      : "Unknown");
                thinking = 0;
            } else if (s->ctx->debug_level > 0) {
                fprintf(stderr, "[DEBUG] JSON response: %s\n", line);
            }
            free(ec);
        }
        start = nl + 1;
        nl = strchr(start, '\n');
    }
    size_t rem = s->buf.count - (start - s->buf.data);
    if (rem > 0) {
        memmove(s->buf.data, start, rem);
    }
    s->buf.count = rem;
    s->buf.data[rem] = 0;
    size_t ret = realsz;
    if (!thinking || exit_requested) {
        ret = 0;
    }
    return ret;
}

static int progress_cb(void * clientp, curl_off_t dltotal,
        curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow) {
    (void)dltotal;
    (void)dlnow;
    (void)ultotal;
    (void)ulnow;
    if (!thinking || exit_requested) {
        return 1;
    }
    struct stream_state * s = (struct stream_state *)clientp;
    if (!s) {
        return 0;
    }
    time_t now = time(NULL);
    /* no first chunk within 30s: server not responding */
    if (s->chunk_count == 0 &&
            difftime(now, s->start_time) > 30.0) {
        s->timed_out = true;
        return 1;
    }
    /* stall: no chunk for max(avg*8, 15s), capped at 60s */
    if (s->chunk_count > 0 && s->last_chunk_time > 0) {
        double elapsed = difftime(now, s->last_chunk_time);
        double thresh = s->avg_interval > 0
            ? s->avg_interval * 8.0 : 15.0;
        if (thresh < 15.0) {
            thresh = 15.0;
        }
        if (thresh > 60.0) {
            thresh = 60.0;
        }
        if (elapsed > thresh) {
            s->timed_out = true;
            return 1;
        }
    }
    return 0;
}

static struct curl_slist * setup_headers(const char * key) {
    char auth[512];
    snprintf(auth, sizeof(auth), "Authorization: Bearer %s", key);
    struct curl_slist * h = curl_slist_append(NULL,
                                             "Content-Type: application/json");
    h = curl_slist_append(h, auth);
    h = curl_slist_append(h, "HTTP-Referer: https://github.com/leok7v/orca");
    h = curl_slist_append(h, "X-Title: ORCA C Agent");
    return h;
}

static char * setup_payload(struct context * ctx) {
    struct request req = {0};
    const char * def_model = "openrouter/free";
    req.model = ctx->model ? ctx->model : (char*)def_model;
    req.stream = true;
    req.max_tokens = 4096;
    req.messages = ctx->messages;
    char * pay = encode(request_rtti, &req);
    if (pay) {
        size_t len = strlen(pay);
        if (len > 2 && pay[len - 1] == '}') {
            char * new_pay = malloc(len + 2048);
            pay[len - 1] = '\0';
            sprintf(new_pay, "%s,\"tools\":["
                    "{\"type\":\"function\",\"function\":{"
                    "\"name\":\"run_shell_command\","
                    "\"description\":\"Exec shell\","
                    "\"parameters\":{\"type\":\"object\",\"properties\":{"
                    "\"command\":{\"type\":\"string\"}},"
                    "\"required\":[\"command\"]}}},"
                    "{\"type\":\"function\",\"function\":{"
                    "\"name\":\"ask_user\",\"description\":\"Ask user\","
                    "\"parameters\":{\"type\":\"object\",\"properties\":{"
                    "\"question\":{\"type\":\"string\"}},"
                    "\"required\":[\"question\"]}}}"
                    ","
                    "{\"type\":\"function\",\"function\":{"
                    "\"name\":\"web_search\","
                    "\"description\":\"Search the web\","
                    "\"parameters\":{\"type\":\"object\","
                    "\"properties\":{"
                    "\"query\":{\"type\":\"string\"}},"
                    "\"required\":[\"query\"]}}}"
                    "],\"tool_choice\":\"auto\"}", pay);
            free(pay);
            pay = new_pay;
        }
    }
    return pay;
}

static void perform_completion(
        struct context * ctx, bool * loop, int * nudges) {
    if (ctx->debug_level > 0) {
        fprintf(stderr, "[DEBUG] Entering perform_completion\n");
    }
    CURL * curl = curl_easy_init();
    const char * key = ctx->api_key;
    if (!key) {
        key = getenv("OPENROUTER_API_KEY");
    }
    if (curl && key) {
        struct curl_slist * h = setup_headers(key);
        char * pay = setup_payload(ctx);
        if (ctx->debug_level > 0) {
            fprintf(stderr, "[DEBUG] Request payload: %s\n", pay);
        }
        curl_easy_setopt(curl, CURLOPT_URL,
                         "https://openrouter.ai/api/v1/chat/completions");
        if (ctx->debug_level > 1) {
            curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
        }
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, h);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, pay);
        struct stream_state s = {0};
        sb_init(&s.buf);
        sb_init(&s.tool_args);
        sb_init(&s.content);
        sb_init(&s.reasoning);
        s.show_reasoning = ctx->show_reasoning;
        s.ctx = ctx;
        s.start_time = time(NULL);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progress_cb);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &s);
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        CURLcode res = curl_easy_perform(curl);
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        if (ctx->debug_level > 0) {
            fprintf(stderr,
                "[DEBUG] curl returned %d (%s) HTTP %ld\n",
                res, curl_easy_strerror(res), http_code);
        }
        if (res != CURLE_OK && res != CURLE_WRITE_ERROR) {
            fprintf(stderr, "curl error: %s\n", curl_easy_strerror(res));
        }
        if (s.buf.count > 0 && s.buf.data[0] == '{') {
            struct chunk * ec = decode(chunk_rtti, s.buf.data);
            if (ec && ec->error.code != 0) {
                fprintf(stderr,
                    "\n\033[31m[API Error %d]: %s\033[0m\n",
                    ec->error.code,
                    ec->error.message ? ec->error.message
                                      : "Unknown");
            }
            free(ec);
        }
        printf(ANSI_COLOR_RESET "\n");
        fflush(stdout);
        if (s.timed_out) {
            printf("\033[33m[Stream stalled — "
                "server stopped responding]\033[0m\n");
            fflush(stdout);
            *loop = false;
        } else if (s.in_tool_call && thinking) {
            add_message(ctx, "assistant",
                        s.tool_args.data, s.tool_name, s.tool_id);
            char * tr = execute_tool(s.tool_name, s.tool_args.data);
            add_message(ctx, "tool", tr, s.tool_name, s.tool_id);
            free(tr);
        } else {
            bool hallucinated = false;
            if (s.content.count > 0 && thinking) {
                char * json_start = s.content.data;
                while ((json_start = strchr(json_start, '{')) != NULL) {
                    struct shell_args * sa =
                        decode(shell_args_rtti, json_start);
                    struct ask_args * aa =
                        decode(ask_args_rtti, json_start);
                    if (sa && sa->command) {
                        add_message(ctx, "assistant", json_start,
                            "run_shell_command", "call_hal");
                        char * tr = execute_tool(
                            "run_shell_command", json_start);
                        add_message(ctx, "tool", tr,
                            "run_shell_command", "call_hal");
                        free(tr);
                        hallucinated = true;
                        if (aa) free(aa);
                        if (sa) free(sa);
                        break;
                    } else if (aa && aa->question) {
                        add_message(ctx, "assistant", json_start,
                            "ask_user", "call_hal");
                        char * tr = execute_tool("ask_user", json_start);
                        add_message(ctx, "tool", tr, "ask_user", "call_hal");
                        free(tr);
                        hallucinated = true;
                        if (aa) free(aa);
                        if (sa) free(sa);
                        break;
                    }
                    if (sa) free(sa);
                    if (aa) free(aa);
                    json_start++;
                }
                if (!hallucinated) {
                    add_message(ctx, "assistant", s.content.data, NULL, NULL);
                    bool has_rea = s.reasoning.count > 0;
                    char * rea = has_rea ? s.reasoning.data : NULL;
                    if (rea) {
                        int idx = ctx->message_count - 1;
                        ctx->messages[idx].reasoning = strdup(rea);
                    }
                    *loop = false;
                }
            } else if (s.reasoning.count > 0 && *nudges < 2) {
                /* model reasoned but called no tool and wrote no
                   content — nudge it to act */
                add_message(ctx, "user",
                    "You must call a tool. "
                    "Use run_shell_command to execute commands.",
                    NULL, NULL);
                (*nudges)++;
            } else {
                printf("\033[33m[No action after reasoning"
                    " - try rephrasing or /clear]\033[0m\n");
                fflush(stdout);
                *loop = false;
            }
        }
        sb_free(&s.buf);
        sb_free(&s.tool_args);
        sb_free(&s.content);
        sb_free(&s.reasoning);
        free(pay);
        curl_slist_free_all(h);
        curl_easy_cleanup(curl);
    } else {
        if (!key) {
            fprintf(stderr,
                "\n\033[31m[Error]: OPENROUTER_API_KEY not set\033[0m\n");
            fflush(stderr);
        }
        *loop = false;
        printf("\n");
        fflush(stdout);
        if (curl) {
            curl_easy_cleanup(curl);
        }
    }
}

static void chat_completion(struct context * ctx) {
    bool loop = true;
    int nudges = 0;
    while (thinking && loop) {
        perform_completion(ctx, &loop, &nudges);
    }
}

static void save_history(struct context * ctx) {
    mkdir(".orca", 0755);
    char * s = encode(context_rtti, ctx);
    if (s) {
        FILE * f = fopen(".orca/history.json", "w");
        if (f) {
            fputs(s, f);
            fclose(f);
        }
        free(s);
    }
}

static void context_free(struct context * ctx) {
    for (int i = 0; i < ctx->message_count; i++) {
        free(ctx->messages[i].role);
        free(ctx->messages[i].content);
        free(ctx->messages[i].reasoning);
        free(ctx->messages[i].reasoning_content);
        free(ctx->messages[i].name);
        free(ctx->messages[i].tool_call_id);
    }
    free(ctx->messages);
    ctx->messages = NULL;
    ctx->message_count = 0;
    ctx->message_cap = 0;
}

static void context_reset(struct context * ctx) {
    context_free(ctx);
    add_message(ctx, "system",
        "You are Orca, an agentic AI with full shell "
        "access via run_shell_command and web_search. "
        "First detect the platform and shell: on macOS "
        "and Linux check $SHELL (zsh or bash); on "
        "Windows use cmd.exe or powershell. "
        "Install missing tools with the right package "
        "manager: 'brew install' on macOS, "
        "'apt install' on Debian/Ubuntu Linux, "
        "'dnf install' on Fedora/RHEL Linux, "
        "'winget install' on Windows 11. "
        "If a package is not found, use web_search "
        "to find installation instructions. "
        "Use the shell for date, time, math, or any "
        "task lacking a built-in tool. "
        "Use ask_user when you need clarification. "
        "Always reply with a tool call for any action.",
        NULL, NULL);
}

static void load_history(struct context * ctx) {
    FILE * f = fopen(".orca/history.json", "r");
    if (f) {
        fseek(f, 0, SEEK_END);
        long len = ftell(f);
        fseek(f, 0, SEEK_SET);
        char * data = malloc((size_t)len + 1);
        if (data) {
            fread(data, 1, (size_t)len, f);
            data[len] = 0;
            struct context * loaded = decode(context_rtti, data);
            if (loaded) {
                ctx->free_mode = loaded->free_mode;
                ctx->show_reasoning = loaded->show_reasoning;
                ctx->debug_level = loaded->debug_level;
                for (int i = 0; i < loaded->message_count; i++) {
                    struct message * m = &loaded->messages[i];
                    if (m->role) {
                        add_message(ctx, m->role, m->content, m->name,
                                    m->tool_call_id);
                        if (m->reasoning) {
                            ctx->messages[ctx->message_count - 1].reasoning =
                                strdup(m->reasoning);
                        }
                    }
                }
                free(loaded);
            }
            free(data);
        }
        fclose(f);
    }
}

static void show_help(void) {
    printf("Commands:\n");
    printf("  /free       Toggle free models\n");
    printf("  /models     List some models\n");
    printf("  /reasoning  Toggle reasoning visibility\n");
    printf("  /debug      Cycle debug levels\n");
    printf("  /clear      Reset conversation\n");
    printf("  /exit       Quit agent\n");
    printf("  /help       Show this help\n");
    printf("  ! <cmd>     Direct shell command\n");
}

static void list_models(struct context * ctx) {
    (void)ctx;
    printf("Available models: https://openrouter.ai/models\n");
}

static void handle_command(struct context * ctx, char * line, bool * active) {
    if (!strcmp(line, "/exit")) {
        *active = false;
    } else if (!strcmp(line, "/help")) {
        show_help();
    } else if (!strcmp(line, "/models")) {
        list_models(ctx);
    } else if (!strcmp(line, "/free")) {
        ctx->free_mode = !ctx->free_mode;
        printf("Free mode: %s\n", ctx->free_mode ? "ON" : "OFF");
    } else if (!strcmp(line, "/reasoning")) {
        ctx->show_reasoning = !ctx->show_reasoning;
        printf("Reasoning: %s\n", ctx->show_reasoning ? "VISIBLE" : "HIDDEN");
    } else if (!strncmp(line, "/debug", 6)) {
        char * args = skip_ws(line + 6);
        if (*args) {
            if (!strcmp(args, "off")) {
                ctx->debug_level = 0;
            } else if (!strcmp(args, "on")) {
                ctx->debug_level = 9;
            } else {
                ctx->debug_level = atoi(args);
            }
        } else {
            ctx->debug_level = (ctx->debug_level > 0) ? 0 : 9;
        }
        printf("Debug level: %d\n", ctx->debug_level);
    } else if (!strcmp(line, "/clear")) {
        context_reset(ctx);
        printf("History cleared.\n");
    } else if (line[0] == '!') {
        printf("\n\033[36m> %s\033[0m\n", line + 1);
        FILE * fp = popen(line + 1, "r");
        if (fp) {
            char buf[1024];
            bool reading = true;
            while (reading) {
                if (fgets(buf, sizeof(buf), fp) == NULL) {
                    reading = false;
                } else {
                    printf("%s", buf);
                }
            }
            pclose(fp);
        }
    } else {
        int prev = ctx->message_count;
        add_message(ctx, "user", line, NULL, NULL);
        thinking = 1;
        chat_completion(ctx);
        thinking = 0;
        if (ctx->message_count > prev + 1) {
            save_history(ctx);
        } else {
            free(ctx->messages[ctx->message_count - 1].role);
            free(ctx->messages[ctx->message_count - 1].content);
            ctx->messages[ctx->message_count - 1].role = NULL;
            ctx->messages[ctx->message_count - 1].content = NULL;
            ctx->message_count = prev;
        }
    }
}

int main(int argc, char ** argv) {
    (void)argc;
    (void)argv;
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_sigint;
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
    printf("🐋 O.R.C.A. Open Router C Agent "
           "(/help for commands, TAB to toggle input)\n");
    struct context ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.show_reasoning = true;
    load_history(&ctx);
    if (!ctx.message_count) {
        context_reset(&ctx);
    }
    char line[4096];
    bool active = true;
    bool raw_input = false;
    bool prompt_needed = true;
    struct pollfd fds[1];
    fds[0].fd = STDIN_FILENO;
    fds[0].events = POLLIN;
    while (active && !exit_requested) {
        if (prompt_needed) {
            if (!raw_input) {
                printf("\033[32m%s%s%s>\033[0m ",
                       ctx.free_mode ? "[free] " : "",
                       ctx.show_reasoning ? "[reason] " : "",
                       ctx.debug_level > 0 ? "[debug] " : "");
            } else {
                printf("\033[31m[raw]>\033[0m ");
            }
            fflush(stdout);
            prompt_needed = false;
        }
        int pr = poll(fds, 1, 10);
        if (exit_requested) {
            active = false;
        } else if (pr > 0) {
            if (!fgets(line, sizeof(line), stdin)) {
                active = false;
            } else {
                if (line[0] == '\t') {
                    raw_input = !raw_input;
                    printf("Switched to %s mode\n",
                           raw_input ? "RAW" : "COMMAND");
                } else {
                    size_t l = strlen(line);
                    while (l > 0 && (line[l - 1] == '\n' ||
                                     line[l - 1] == '\r')) {
                        line[--l] = 0;
                    }
                    if (l > 0) {
                        handle_command(&ctx, line, &active);
                    }
                }
                prompt_needed = true;
            }
        }
    }
    context_free(&ctx);
    return 0;
}

#endif
