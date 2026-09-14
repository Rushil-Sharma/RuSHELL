#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <errno.h>
#include "jobs.h"

#include "history.h"

#define HISTFILE "./instructions.txt"
#define MAX_HIST 1024

static char *hist_lines[MAX_HIST];
static int hist_count = 0;

// clears the on-disk history file at shell startup, and resets the in-memory cache so a fresh session starts with no history
void history_init(void) {
    FILE *f = fopen(HISTFILE, "w");
    if (f) fclose(f);

    for (int i = 0; i < hist_count; i++) free(hist_lines[i]);
    hist_count = 0;
}

// appends a non-empty line to the in-memory history and to disk
static void history_append(const char *line) {
    if (line[0] == '\0') return;
    if (hist_count < MAX_HIST) {
        hist_lines[hist_count++] = strdup(line);
    } else {
        // drop oldest, shift up
        free(hist_lines[0]);
        memmove(hist_lines, hist_lines + 1, sizeof(char *) * (MAX_HIST - 1));
        hist_lines[MAX_HIST - 1] = strdup(line);
    }
    FILE *f = fopen(HISTFILE, "a");
    if (f) {
        fprintf(f, "%s\n", line);
        fclose(f);
    }
}

// redraws the current input line in place: clears the terminal line, reprints buf, and positions the cursor at the end
static void redraw_line(const char *prompt, const char *buf, size_t len) {
    printf("\r\x1b[K%s ", prompt); // clear line, reprint prompt
    fwrite(buf, 1, len, stdout);
    fflush(stdout);
}

char *history_read_line(char *buf, size_t bufsize, const char *prompt) {
    struct termios orig, raw;
    if (tcgetattr(STDIN_FILENO, &orig) == -1) {
        // not a real terminal (e.g. piped input) fall back to fgets
        return fgets(buf, (int)bufsize, stdin);
    }

    raw = orig;
    raw.c_lflag &= ~(ICANON | ECHO);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);

    size_t len = 0;
    int hist_pos = hist_count;
    char saved_current[4096];
    saved_current[0] = '\0';
    buf[0] = '\0';

    int done = 0;
    int got_eof = 0;

    while (!done) {
        char c;
        ssize_t n = read(STDIN_FILENO, &c, 1);
        if (got_sigint) {
            got_sigint = 0;
            len = 0;
            buf[0] = '\0';
            done = 1;
            got_eof = 0;
            break;
        }
        if (n <= 0) {
            if (n == 0 || errno != EINTR) { got_eof = 1; break; }
            continue;
        }

        if (c == '\n' || c == '\r') {
            write(STDOUT_FILENO, "\n", 1);
            done = 1;
        } else if (c == 4 && len == 0) {
            // Ctrl-D on an empty line = EOF
            got_eof = 1;
            done = 1;
        } else if (c == 127 || c == 8) {
            // backspace
            if (len > 0) {
                len--;
                buf[len] = '\0';
                redraw_line(prompt, buf, len);
            }
        } else if (c == 27) {
            char seq[2];
            if (read(STDIN_FILENO, &seq[0], 1) <= 0) continue;
            if (seq[0] != '[') continue;
            if (read(STDIN_FILENO, &seq[1], 1) <= 0) continue;

            if (seq[1] == 'A') { // up arrow
                if (hist_count == 0) continue;
                if (hist_pos == hist_count) {
                    // first time browsing: stash what the user had typed
                    strncpy(saved_current, buf, sizeof(saved_current) - 1);
                    saved_current[sizeof(saved_current) - 1] = '\0';
                }
                if (hist_pos > 0) {
                    hist_pos--;
                    strncpy(buf, hist_lines[hist_pos], bufsize - 1);
                    buf[bufsize - 1] = '\0';
                    len = strlen(buf);
                    redraw_line(prompt, buf, len);
                }
            } else if (seq[1] == 'B') { // down arrow
                if (hist_pos < hist_count) {
                    hist_pos++;
                    if (hist_pos == hist_count) {
                        strncpy(buf, saved_current, bufsize - 1);
                        buf[bufsize - 1] = '\0';
                    } else {
                        strncpy(buf, hist_lines[hist_pos], bufsize - 1);
                        buf[bufsize - 1] = '\0';
                    }
                    len = strlen(buf);
                    redraw_line(prompt, buf, len);
                }
            }
            // any other escape sequence (left/right/etc.) is ignored
        } else if (c >= 32 && c < 127) {
            // printable character
            if (len + 1 < bufsize) {
                buf[len++] = c;
                buf[len] = '\0';
                write(STDOUT_FILENO, &c, 1);
            }
        }
        // other control chars are ignored
    }

    tcsetattr(STDIN_FILENO, TCSANOW, &orig); // restore cooked mode

    if (got_eof && len == 0) {
        return NULL;
    }

    buf[len++] = '\n';
    buf[len] = '\0';

    // record non-empty lines into history (strip trailing \n for storage)
    if (len > 1) {
        char tmp[4096];
        strncpy(tmp, buf, sizeof(tmp) - 1);
        tmp[sizeof(tmp) - 1] = '\0';
        size_t tl = strlen(tmp);
        if (tl > 0 && tmp[tl - 1] == '\n') tmp[tl - 1] = '\0';
        history_append(tmp);
    }

    return buf;
}