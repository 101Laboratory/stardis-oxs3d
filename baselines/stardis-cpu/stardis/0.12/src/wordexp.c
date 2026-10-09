#include "wordexp.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static int is_bad_char(char c)
{
    return (c == '|' || c == '&' || c == ';' ||
        c == '<' || c == '>');
}

static char* str_append(char* s, size_t* len, char c)
{
    char* p = realloc(s, *len + 2);
    if (!p) {
        free(s);
        return NULL;
    }
    p[*len] = c;
    p[*len + 1] = '\0';
    (*len)++;
    return p;
}

static char* expand_var(const char** ps, int flags, int* err)
{
    const char* s = *ps;
    char name[128];
    size_t n = 0;

    if (*s == '{') {
        s++;
        while (*s && *s != '}' && n + 1 < sizeof(name))
            name[n++] = *s++;
        if (*s != '}') {
            *err = WRDE_SYNTAX;
            return NULL;
        }
        s++;
    }
    else {
        while ((isalnum((unsigned char)*s) || *s == '_') &&
            n + 1 < sizeof(name))
            name[n++] = *s++;
    }

    name[n] = 0;
    *ps = s;

    const char* val = getenv(name);
    if (!val) {
        if (flags & WRDE_UNDEF)
            *err = WRDE_BADVAL;
        return val ? strdup(val) : strdup("");
    }
    return strdup(val);
}

static char* parse_token(const char** ps, int flags, int* err)
{
    const char* s = *ps;
    char* out = NULL;
    size_t len = 0;

    while (*s && !isspace((unsigned char)*s)) {
        if (is_bad_char(*s)) {
            *err = WRDE_BADCHAR;
            goto fail;
        }

        if ((flags & WRDE_NOCMD) &&
            ((*s == '`') || (*s == '$' && s[1] == '('))) {
            *err = WRDE_CMDSUB;
            goto fail;
        }

        if (*s == '\'' || *s == '"') {
            char q = *s++;
            while (*s && *s != q) {
                if (*s == '\\' && q == '"' && s[1])
                    s++;
                out = str_append(out, &len, *s++);
                if (!out) {
                    *err = WRDE_NOSPACE;
                    goto fail;
                }
            }
            if (*s != q) {
                *err = WRDE_SYNTAX;
                goto fail;
            }
            s++;
            continue;
        }

        if (*s == '\\') {
            s++;
            if (*s) {
                out = str_append(out, &len, *s++);
                if (!out) {
                    *err = WRDE_NOSPACE;
                    goto fail;
                }
            }
            continue;
        }

        if (*s == '$') {
            s++;
            char* v = expand_var(&s, flags, err);
            if (!v) goto fail;
            for (char* p = v; *p; ++p) {
                out = str_append(out, &len, *p);
                if (!out) {
                    free(v);
                    *err = WRDE_NOSPACE;
                    goto fail;
                }
            }
            free(v);
            continue;
        }

        out = str_append(out, &len, *s++);
        if (!out) {
            *err = WRDE_NOSPACE;
            goto fail;
        }
    }

    *ps = s;
    return out;

fail:
    free(out);
    return NULL;
}

int wordexp(const char* s, wordexp_t* p, int flags)
{
    if (!s || !p)
        return WRDE_SYNTAX;

    if (flags & WRDE_REUSE)
        wordfree(p);
    else {
        p->we_wordc = 0;
        p->we_wordv = NULL;
        p->we_offs = 0;
    }

    while (*s) {
        while (isspace((unsigned char)*s)) s++;
        if (!*s) break;

        int err = 0;
        char* tok = parse_token(&s, flags, &err);
        if (!tok)
            return err;

        char** nv = realloc(p->we_wordv,
            (p->we_wordc + 1) * sizeof(char*));
        if (!nv) {
            free(tok);
            return WRDE_NOSPACE;
        }
        p->we_wordv = nv;
        p->we_wordv[p->we_wordc++] = tok;
    }
    return 0;
}

void wordfree(wordexp_t* p)
{
    if (!p || !p->we_wordv)
        return;
    for (size_t i = 0; i < p->we_wordc; ++i)
        free(p->we_wordv[i]);
    free(p->we_wordv);
    p->we_wordv = NULL;
    p->we_wordc = 0;
}

