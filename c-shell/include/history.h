#ifndef HISTORY_H
#define HISTORY_H

#include <stddef.h>

void history_init(void);
char *history_read_line(char *buf, size_t bufsize, const char *prompt);
#endif