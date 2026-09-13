#ifndef JOBS_H
#define JOBS_H

#include <sys/types.h>
#include <signal.h>

void jobs_init();
int job_add(pid_t pid, const char *cmd_name);
void set_fg_active(int active);
void set_at_prompt(int flag);
void flush_pending_bg_messages(void);

void group_add(pid_t pgid);
void group_add_member(pid_t pgid, pid_t pid, const char *cmd_name);
void activities_print(void);

void terminal_init(void); // save shell pgid, take terminal, ignore/handle signals
void give_terminal_to(pid_t pgid); // tcsetpgrp to job
void reclaim_terminal(void); // tcsetpgrp back to shell
int  mark_group_stopped(pid_t pgid); // find group by pgid, mark Stopped, return job_id (-1 if not found)
int  has_stopped_jobs(void); // 1 if any tracked group is currently Stopped
void hangup_all_jobs(void); // SIGHUP every tracked group's pgid, no waiting

int group_lookup(int job_id, pid_t *pgid, int *stopped, char *cmd_name, size_t cmd_name_len);
void mark_group_running(pid_t pgid);
void group_remove(pid_t pgid);

int pid_is_tracked(pid_t pid);// Returns 1 if pid belongs to a process this shell spawned and is still tracking (i.e. still active in the internal proc table), else 0.

#endif