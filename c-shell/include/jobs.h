#ifndef JOBS_H
#define JOBS_H

#include <sys/types.h>
#include <signal.h>

void jobs_init();
int job_add(pid_t pid, const char *cmd_name);
void set_fg_active(int active);
void set_at_prompt(int flag);
void flush_pending_bg_messages(void);

#endif