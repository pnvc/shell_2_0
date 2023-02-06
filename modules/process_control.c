#include <string.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <inttypes.h>
#include <termios.h>
#include <stdlib.h>
#include "headers/Shell_2_0.h"
#include "headers/process_control.h"
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/mman.h>

typedef struct command_parts copa;
typedef struct process process;

extern char conveyor_part[];
extern char train_part[];
extern char or_part[]; //
extern char background_part[];
extern char and_part[]; //
extern char output_to_start_part[];
extern char output_to_end_part[];
extern char input_from_part[];
extern char bracket_left_part[]; // 
extern char bracket_right_part[]; // 
extern char quote_part[];
extern char shield_part[];

volatile sig_atomic_t child_ready, parent_ready;

void free_process (process *p)
{
	while (p) {
		free(p->name);
		process *tmp = p;
		p = p->next;
		free(tmp);
	}
}

void close_conveyor (const int8_t conv_init, const int32_t fd)
{
	if (conv_init)
		close(fd);
}

void check_pid_error (pid_t p)
{
	if (p == -1) {
		perror("FORK");
		exit(1);
	}
}

void c_r_handler (int s)
{
	signal(SIGCHLD, c_r_handler);
	int er = errno;
	child_ready = 1;
	errno = er;
}

void p_r_handler (int s)
{
	signal(SIGUSR2, p_r_handler);
	int er = errno;
	parent_ready = 1;
	errno = er;
}

void on_signals ()
{
	signal(SIGINT, SIG_DFL);
	signal(SIGQUIT, SIG_DFL);
	signal(SIGTSTP, SIG_DFL);
	signal(SIGTTIN, SIG_DFL);
	signal(SIGTTOU, SIG_DFL);
}

void off_signals ()
{
	signal(SIGINT, SIG_IGN);
	signal(SIGQUIT, SIG_IGN);
	signal(SIGTSTP, SIG_IGN);
	signal(SIGTTIN, SIG_IGN);
	signal(SIGTTOU, SIG_IGN);
}

char **copa_to_cmdline (const copa *t)
{
	int i = 0;
	char **cmdline = (char**)malloc(sizeof(char*) * copa_elements(t) + 1);
	for (; t; t = t->next, i++)
		cmdline[i] = t->part;
	cmdline[i] = NULL;
	return cmdline;
}

size_t copa_elements (const copa *t)
{
	size_t i = 0;
	for (; t; t = t->next, i++);
	return i;
}

process *execute_cmdline (char **cmdline, const struct termios *o, process *p)
{
	process *first, *last, *tmp;
	first = p;
	tmp = NULL;
	int32_t background_pos = 1;
	if (first)
		while (first->next)
			first = first->next;
	last = first;
	if (last)
		background_pos = last->bpos + 1;
	first = p;
	pid_t pid;
	pid_t main_pgid = getpgid(getpid());
	size_t i, start_i;
	int32_t old_pipe_fd0 = 0;
	tcsetattr(0, TCSADRAIN, o);
	for (start_i = i = 0; cmdline[i]; i++) {
		if (cmdline[i] == background_part) {
			sigset_t mask_usr1, mask_empty;
			sigemptyset(&mask_usr1);
			sigemptyset(&mask_empty);
			sigaddset(&mask_usr1, SIGCHLD);
			child_ready = 0;
			sigprocmask(SIG_SETMASK, &mask_usr1, NULL);
			signal(SIGCHLD, c_r_handler);
			child_ready = 0;
			cmdline[i] = NULL;
			pid = fork();
			if (pid < 0) {
				perror("Fork");
				fflush(stderr);
				signal(SIGCHLD, SIG_DFL);
				sigprocmask(SIG_SETMASK, &mask_empty, NULL);
				break;
			}
			if (!pid) {
				pid_t self_pid = getpid();
				setpgid(self_pid, self_pid);
				on_signals();
				if (old_pipe_fd0) {
					dup2(old_pipe_fd0, 0);
					close(old_pipe_fd0);
				}
				if (set_redirects((const char**)cmdline + start_i) == -1) {
					perror("Set redirects");
					fflush(stderr);
					_exit(1);
				}
				execvp((const char*)cmdline[start_i], (char * const*)cmdline + start_i);
				perror("Execvp");
				fflush(stderr);
				_exit(1);
			}
			if (old_pipe_fd0) {
				close(old_pipe_fd0);
				old_pipe_fd0 = 0;
			}
			tmp = malloc(sizeof(*tmp));
			tmp->next = NULL;
			size_t name_size = strlen((const char*)cmdline[start_i]);
			tmp->name = malloc(name_size + 1);
			memcpy(tmp->name, (const char*)cmdline[start_i], name_size);
			tmp->name[name_size] = 0;
			tmp->pid = pid;
			tmp->pgrp = pid;
			tmp->stopped = tmp->completed = 0;
			tmp->background = 1;
			tmp->bpos = background_pos++;
			while (!child_ready)
				sigsuspend(&mask_empty); // WHAT TO DO IF NOT HAVE?
			signal(SIGCHLD, SIG_DFL);
			sigprocmask(SIG_SETMASK, &mask_empty, NULL);
			int32_t wstatus;
			pid_t wr;
			wr = waitpid(pid, &wstatus, WNOHANG | WUNTRACED);
			if (wr > 0) {
				if (WIFEXITED(wstatus)) {
					tmp->completed = 1;
					printf("%d [%d] completed\n", tmp->bpos, tmp->pid);
					free(tmp);
				} else if (WIFSIGNALED(wstatus)) {
					tmp->completed = 1;
					printf("%d [%d] signaled\n", tmp->bpos, tmp->pid);
					free(tmp);
				} else if (WIFSTOPPED(wstatus)) {
					tmp->stopped = 1;
					printf("%d [%d] stopped\n", tmp->bpos, tmp->pid);
					if (!last)
						p = last = tmp;
					else {
						last->next = tmp;
						last = tmp;
					}
				}
			} if (wr == 0) {
				;
			}
			start_i = i + 1;
		} else if (!strcmp((const char*)cmdline[i], "cd\0")) {
			if (old_pipe_fd0) {
				close(old_pipe_fd0);
				old_pipe_fd0 = 0;
			}
			if (i == 0 || cmdline[i - 1] == train_part || cmdline[i - 1] == background_part) {
				if (!cmdline[i + 1] || cmdline[i + 1] == background_part || cmdline[i + 1] == train_part) {
					chdir(getenv("HOME"));
					start_i = i + 2;
				} else if (!cmdline[i + 2] || cmdline[i + 2] == background_part || cmdline[i + 2] == train_part) {
					chdir((const char*)cmdline[i + 1]);
					start_i = i + 3;
				} else
					break;
			} else
				break;
		} else if (!strcmp((const char*)cmdline[i], "jobs\0")) {
			if (old_pipe_fd0) {
				close(old_pipe_fd0);
				old_pipe_fd0 = 0;
			}
			if ((i == 0 || cmdline[i - 1] == train_part || cmdline[i - 1] == background_part) && (!cmdline[i + 1] || cmdline[i + 1] == train_part || cmdline[i + 1] == background_part)) {
				if (old_pipe_fd0) {
					close(old_pipe_fd0);
					old_pipe_fd0 = 0;
				}
				while (first) {
					char pos[11] = {0};
					char id[11] = {0};
					const char ns[] = "Not stopped";
					const char s[] = "Stopped";
					sprintf(pos, "%d", first->bpos);
					sprintf(id, "%d", first->pid);
					write(1, (const char*)pos, sizeof(pos));
					write(1, " ", 1);
					write(1, "[", 1);
					write(1, (const char*)id, sizeof(id));
					write(1, "]", 1);
					write(1, " ", 1);
					if (first->stopped)
						write(1, s, sizeof(s));
					else
						write(1, ns, sizeof(ns));
					write(1, " ", 1);
					write(1, (const char*)first->name, strlen(first->name));
					write(1, "\n", 1);
					first = first->next;
				}
				first = p;
			} else
				break;
		} else if (!strcmp((const char*)cmdline[i], "fg\0")) {
			if (old_pipe_fd0) {
				close(old_pipe_fd0);
				old_pipe_fd0 = 0;
			}
			if ((i == 0 || cmdline[i - 1] == train_part || cmdline[i - 1] == background_part) && cmdline[i + 1] && (!cmdline[i + 2] || cmdline[i + 2] == background_part || cmdline[i + 2] == train_part)) {
				tmp = NULL;
				while (first) {
					if (first->bpos == (pid_t)atoi(cmdline[i + 1]) || first->pid == (pid_t)atoi(cmdline[i + 1])) {
						pid_t pid_wait = first->pid;
						tcsetpgrp(0, first->pgrp);
						kill(first->pid, SIGCONT);
						if (tmp) {
							tmp->next = tmp->next->next;
							free(tmp->next->name);
							free(tmp->next);
						} else {
							free(first->name);
							free(first);
							p = first = NULL;
						}
						waitpid(pid_wait, NULL, 0);
						tcsetpgrp(0, main_pgid);
						break;
					} else {
						tmp = first;
						first = first->next;
					}
				}
			} else
				break;
		} else if (cmdline[i] == train_part) {
			cmdline[i] = NULL;
			pid  = fork();
			if (!pid) {
				pid_t self_pid = getpid();
				setpgid(self_pid, self_pid);
				tcsetpgrp(0, self_pid);
				on_signals();
				if (old_pipe_fd0) {
					dup2(old_pipe_fd0, 0);
					close(old_pipe_fd0);
				}
				if (set_redirects((const char**)cmdline + start_i) == -1) {
					perror("Set redirects");
					fflush(stderr);
					_exit(1);
				}
				execvp((const char*)cmdline[start_i], (char * const*)cmdline + start_i);
				perror("Execvp");
				fflush(stderr);
				_exit(1);
			}
			if (old_pipe_fd0) {
				close(old_pipe_fd0);
				old_pipe_fd0 = 0;
			}
			start_i = i + 1;
			waitpid(pid, NULL, 0);
			tcsetpgrp(0, main_pgid);
		} else if (cmdline[i] == conveyor_part) {
			child_ready = 0;
			cmdline[i] = NULL;
			int32_t pipe_fd[2];
			pipe(pipe_fd);
			pid = fork();
			if (!pid) {
				pid_t self_pid = getpid();
				setpgid(self_pid, self_pid);
				tcsetpgrp(0, self_pid);
				on_signals();
				if (old_pipe_fd0) {
					dup2(old_pipe_fd0, 0);
					close(old_pipe_fd0);
				} else
					close(pipe_fd[0]);
				dup2(pipe_fd[1], 1);
				close(pipe_fd[1]);
				if (set_redirects((const char**)cmdline + start_i) == -1) {
					perror("Set redirects");
					fflush(stderr);
					_exit(1);
				}
				execvp((const char*)cmdline[start_i], (char * const*)cmdline + start_i);
				perror("Execvp");
				fflush(stderr);
				_exit(1);
			}
			close(pipe_fd[1]);
			if (old_pipe_fd0) {
				close(old_pipe_fd0);
				old_pipe_fd0 = pipe_fd[0];
			} else
				old_pipe_fd0 = pipe_fd[0];
			start_i = i + 1;
			waitpid(pid, NULL, 0);
			tcsetpgrp(0, main_pgid);
		} else if (cmdline[i] == and_part) {
			; // HERE
		} else if (cmdline[i] == or_part) {
			; // HERE
		} else if (cmdline[i] == bracket_left_part) {
			; // HERE
		} else if (cmdline[i] == bracket_right_part) {
			; // HERE
		} else if (cmdline[i + 1] == NULL) {
			pid = fork();
			if (!pid) {
				pid_t self_pid = getpid();
				setpgid(self_pid, self_pid);
				tcsetpgrp(0, self_pid);
				on_signals();
				if (old_pipe_fd0) {
					dup2(old_pipe_fd0, 0);
					close(old_pipe_fd0);
				}
				if (set_redirects((const char**)cmdline + start_i) == -1) {
					perror("Set redirects");
					fflush(stderr);
					_exit(1);
				}
				execvp((const char*)cmdline[start_i], (char * const*)cmdline + start_i);
				perror("Execvp");
				fflush(stderr);
				_exit(1);
			}
			if (old_pipe_fd0) {
				close(old_pipe_fd0);
				old_pipe_fd0 = 0;
			}
			waitpid(pid, NULL, 0);
			tcsetpgrp(0, main_pgid);
		}
	}
	return p;
}

int8_t cmdline_has_ (const char *_item, const char **cmdline)
{
	size_t i = 0;
	int8_t item = 0;
	for (; cmdline[i]; i++)
		if (cmdline[i] == _item) item++;
	return item;
}

int32_t set_redirects (const char **cmdline)
{
	size_t i = 0;
	int32_t fd;
	for (; cmdline[i];) {
		if (cmdline[i] == output_to_start_part) {
			cmdline[i] = NULL;
			if (!cmdline[i + 1]) return-1;
			fd = open(cmdline[i + 1], O_WRONLY | O_CREAT | O_TRUNC, 0664);
			if (fd == -1)
				return -1;
			dup2(fd, 1);
			close(fd);
		}
		if (cmdline[i] == output_to_end_part) {
			cmdline[i] = NULL;
			if (!cmdline[i + 1]) return-1;
			fd = open(cmdline[i + 1], O_WRONLY | O_CREAT | O_APPEND, 0664);
			if (fd == -1)
				return -1;
			dup2(fd, 1);
			close(fd);
		}
		if (cmdline[i] == input_from_part) {
			cmdline[i] = NULL;
			if (!cmdline[i + 1]) return-1;
			fd = open(cmdline[i + 1], O_RDONLY);
			if (fd == -1)
				return -1;
			dup2(fd, 0);
			close(fd);
		}
		i++;
	}
	return 1;
}
