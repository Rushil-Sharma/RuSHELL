#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

#include "peek.h"

#define CHUNK_SIZE 4096
#define INITIAL_LINE_CAP 128

typedef struct {
    char *data;
    int len;
    long orig_number; // non zero line number (index)
} PeekLine;

typedef struct {
    PeekLine *lines;
    int count;
    int cap;
} PeekLineList;

typedef struct { // ops = options 
    bool numbered;
    bool reversed;
} PeekOptions;

static bool peek_is_flag_arg(const char *arg) { // anything other than -n, -r is ret(false)
    if (arg[0] != '-') return false;
    if (arg[1] == '\0') return false;
    for (const char *p = arg + 1; *p; p++) if (*p != 'n' && *p != 'r') return false;
    return true;
}

static int peek_parse_args(int argc, char *argv[], PeekOptions *ops,const char **filenames_out) {
    ops->numbered = false;
    ops->reversed = false;
    int fcount = 0;
    for (int i = 1; i < argc; i++) {
        char *arg = argv[i];
        if (peek_is_flag_arg(arg)) {
            for (const char *p = arg + 1; *p; p++) { // iteration through characters
                if (*p == 'n') ops->numbered = true;
                else if (*p == 'r') ops->reversed = true;
            }
        } else {
            filenames_out[fcount++] = arg;
        }
    }
    return fcount;
}

static void peek_linelist_init(PeekLineList *ll) {
    ll->lines = NULL;
    ll->count = 0;
    ll->cap = 0;
}

static void peek_linelist_push(PeekLineList *ll, char *data, int len) {
    if (ll->count == ll->cap) {
        int new_cap = ll->cap == 0 ? INITIAL_LINE_CAP : ll->cap * 2;
        PeekLine *new_lines = realloc(ll->lines, new_cap * sizeof(PeekLine));
        if (new_lines == NULL) {
            fprintf(stderr, "peek: out of memory\n");
            exit(1);
        }
        ll->lines = new_lines;
        ll->cap = new_cap;
    }
    ll->lines[ll->count].data = data;
    ll->lines[ll->count].len = len;
    ll->lines[ll->count].orig_number = 0;
    ll->count++;
}

static void peek_linelist_free(PeekLineList *ll) {
    for (int i = 0; i < ll->count; i++) free(ll->lines[i].data);
    free(ll->lines);
    ll->lines = NULL;
    ll->count = 0;
    ll->cap = 0;
}

static bool peek_line_is_nonempty(const char *data, int len) {
    if (len == 0) return false;
    if (len == 1 && data[0] == '\n') return false; // might be that it has only \n len = 1
    return true;
}

static void peek_linelist_assign_numbers(PeekLineList *ll, bool stored_in_forward_order,long *running_no) {
    if (stored_in_forward_order) {
        for (int i = 0; i < ll->count; i++) {
            if (peek_line_is_nonempty(ll->lines[i].data, ll->lines[i].len)) {
                ll->lines[i].orig_number = ++(*running_no);
            }
        }
    } else {
        for (int i = ll->count; i > 0; i--) {
            int idx = i - 1;
            if (peek_line_is_nonempty(ll->lines[idx].data, ll->lines[idx].len)) {
                ll->lines[idx].orig_number = ++(*running_no);
            }
        }
    }
}

static void peek_print_line(const PeekLine *ln, const PeekOptions *ops) {
    if (ops->numbered && ln->orig_number > 0) {
        printf("%ld ", ln->orig_number);
    }
    fwrite(ln->data, 1, ln->len, stdout);
}

static void print_and_maybe_number(const char *data, int len,const PeekOptions *ops, long *running_no) {
    long num = 0;
    if (ops->numbered && peek_line_is_nonempty(data, len)) {
        num = ++(*running_no);
    }
    if (ops->numbered && num > 0) printf("%ld ", num);
    fwrite(data, 1, len, stdout);
}

//Streams forward by printing each line as its read
static int peek_forward_fd(int fd, const PeekOptions *ops, long *running_no) {
    char buf[CHUNK_SIZE];
    int n;
    char *pending = NULL;
    int pending_len = 0;

    while ((n = read(fd, buf, sizeof(buf))) != 0) {
        if (n < 0) {
            if (errno == EINTR) continue;
            free(pending);
            return -1;
        }
        int start = 0;
        for (int i = 0; i < n; i++) {
            if (buf[i] == '\n') {
                int seg_len = (int)(i - start) + 1;
                if (pending_len > 0) {
                    char *combined = malloc(pending_len + seg_len);
                    if (!combined) { fprintf(stderr, "peek: out of memory\n"); exit(1); }
                    memcpy(combined, pending, pending_len);
                    memcpy(combined + pending_len, buf + start, seg_len);
                    print_and_maybe_number(combined, pending_len + seg_len, ops, running_no);
                    free(combined);
                    free(pending);
                    pending = NULL;
                    pending_len = 0;
                } else {
                    print_and_maybe_number(buf + start, seg_len, ops, running_no);
                }
                start = (int)i + 1;
            }
        }
        if ((int)n > start) {
            int rem = (int)n - start;
            char *np = realloc(pending, pending_len + rem);
            if (!np) { fprintf(stderr, "peek: out of memory\n"); exit(1); }
            memcpy(np + pending_len, buf + start, rem);
            pending = np;
            pending_len += rem;
        }
    }

    if (pending_len > 0) {
        print_and_maybe_number(pending, pending_len, ops, running_no);
        free(pending);
    }
    return 0;
}

// Reads one line (inlcudes '\n') from fd into a malloc buffer
static char *read_line_fd(int fd, int *out_len, bool *got_any) {
    int cap = 256, len = 0;
    char *buf = malloc(cap);
    if (!buf) { fprintf(stderr, "peek: out of memory\n"); exit(1); }
    *got_any = false;

    while (1) {
        char c;
        int r = read(fd, &c, 1);
        if (r < 0) {
            if (errno == EINTR) continue;
            free(buf);
            *out_len = 0;
            return NULL;
        }
        if (r == 0) break;
        *got_any = true;
        if (len + 1 > cap) {
            cap *= 2;
            char *nb = realloc(buf, cap);
            if (!nb) { fprintf(stderr, "peek: out of memory\n"); exit(1); }
            buf = nb;
        }
        buf[len++] = c;
        if (c == '\n') break;
    }

    if (!*got_any) { free(buf); *out_len = 0; return NULL; }
    *out_len = len;
    return buf;
}

// Reverse path for unseekable input buffer fully, then print back to front
static int peek_reverse_buffered_fd(int fd, PeekLineList *ll, long *running_no) {
    while (1) {
        int len;
        bool got_any;
        char *line = read_line_fd(fd, &len, &got_any);
        if (!got_any) break;
        peek_linelist_push(ll, line, len); // stored in forward order
    }
    peek_linelist_assign_numbers(ll, true, running_no);
    return 0;
}

// walk backward in fixed-size lseek/read chunks rather than loading the whole file into memory.
static int peek_reverse_seek_fd(int fd, PeekLineList *ll, long *running_no) {
    off_t file_size = lseek(fd, 0, SEEK_END);
    if (file_size < 0) {
        if (lseek(fd, 0, SEEK_SET) < 0) return -1;
        return peek_reverse_buffered_fd(fd, ll, running_no);
    }
    if (file_size == 0) return 0;

    off_t pos = file_size;
    char *tail = NULL;
    int tail_len = 0;
    char chunk[CHUNK_SIZE];

    while (pos > 0) {
        int to_read = (int)((pos >= CHUNK_SIZE) ? CHUNK_SIZE : pos);
        pos -= (off_t)to_read;

        if (lseek(fd, pos, SEEK_SET) < 0) { free(tail); return -1; }

        int total_read = 0;
        while (total_read < to_read) {
            int r = read(fd, chunk + total_read, to_read - total_read);
            if (r < 0) {
                if (errno == EINTR) continue;
                free(tail);
                return -1;
            }
            if (r == 0) break;
            total_read += (int)r;
        }

        int combined_len = total_read + tail_len;
        char *combined = malloc(combined_len);
        if (!combined) { fprintf(stderr, "peek: out of memory\n"); exit(1); }
        memcpy(combined, chunk, total_read);
        if (tail_len > 0) memcpy(combined + total_read, tail, tail_len);
        free(tail);
        tail = NULL;
        tail_len = 0;

        int seg_end = combined_len;
        long i = (long)combined_len - 1;
        while (i >= 0) {
            if (combined[i] == '\n') {
                int seg_start = (int)i + 1;
                int seg_len = seg_end - seg_start;
                if (seg_len > 0) {
                    char *lb = malloc(seg_len);
                    if (!lb) { fprintf(stderr, "peek: out of memory\n"); exit(1); }
                    memcpy(lb, combined + seg_start, seg_len);
                    peek_linelist_push(ll, lb, seg_len); /* reverse order */
                }
                seg_end = (int)i + 1;
            }
            i--;
        }

        if (pos == 0) {
            if (seg_end > 0) {
                char *lb = malloc(seg_end);
                if (!lb) { fprintf(stderr, "peek: out of memory\n"); exit(1); }
                memcpy(lb, combined, seg_end);
                peek_linelist_push(ll, lb, seg_end);
            }
            free(combined);
        } else {
            if (seg_end > 0) {
                tail = malloc(seg_end);
                if (!tail) { fprintf(stderr, "peek: out of memory\n"); exit(1); }
                memcpy(tail, combined, seg_end);
                tail_len = seg_end;
            }
            free(combined);
        }
    }

    peek_linelist_assign_numbers(ll, false, running_no); /* bottom-to-top */
    return 0;
}

static bool peek_fd_is_seekable(int fd) {
    return lseek(fd, 0, SEEK_CUR) != (off_t)-1;
}

static int peek_process_source(const char *name, bool is_stdin,const PeekOptions *ops, long *running_no) {
    int fd;
    if (is_stdin) {
        fd = STDIN_FILENO;
    } else {
        struct stat st;
        if (stat(name, &st) != 0) {
            fprintf(stderr, "peek: no such file or directory\n");
            return -1;
        }
        if (S_ISDIR(st.st_mode)) {
            fprintf(stderr, "peek: is a directory\n");
            return -1;
        }
        fd = open(name, O_RDONLY);
        if (fd < 0) {
            fprintf(stderr, "peek: no such file or directory\n");
            return -1;
        }
    }

    int rc = 0;
    if (ops->reversed) {
        PeekLineList ll;
        peek_linelist_init(&ll);
        bool seekable = !is_stdin && peek_fd_is_seekable(fd);
        if (seekable) {
            rc = peek_reverse_seek_fd(fd, &ll, running_no);
            for (int k = 0; k < ll.count; k++) peek_print_line(&ll.lines[k], ops);
        } else {
            rc = peek_reverse_buffered_fd(fd, &ll, running_no);
            for (int k = ll.count; k > 0; k--) peek_print_line(&ll.lines[k - 1], ops);
        }
        peek_linelist_free(&ll);
    } 
    else rc = peek_forward_fd(fd, ops, running_no);

    if (!is_stdin) close(fd);
    return rc;
}

int peek_command(int argc, char *argv[]) {
    const char **filenames = malloc(sizeof(char *) * (int)(argc > 0 ? argc : 1));
    if (filenames == NULL) {
        fprintf(stderr, "peek: out of memory\n");
        return 1;
    }

    PeekOptions ops;
    int fcount = peek_parse_args(argc, argv, &ops, filenames);

    bool any_error = false;
    long running_no = 0;

    if (fcount == 0) {
        if (peek_process_source(NULL, true, &ops, &running_no) != 0) any_error = true;
    } else {
        for (int i = 0; i < fcount; i++) {
            bool is_stdin = (strcmp(filenames[i], "-") == 0);
            if (peek_process_source(is_stdin ? NULL : filenames[i], is_stdin, &ops, &running_no) != 0) {
                any_error = true;
            }
        }
    }

    free(filenames);
    return any_error ? 1 : 0;
}