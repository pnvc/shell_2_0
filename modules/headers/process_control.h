#ifndef PROCESS_CONTROL_H
#define PROCESS_CONTROL_H

typedef struct process {
	struct process *next;
	char *name;
	pid_t pid;
	pid_t pgrp;
	int8_t completed; // true or false
	int8_t stopped;	// true or false
	int8_t background;
	int32_t bpos;
} process;

void free_process (process *p);
void close_conveyor (const int8_t conv_init, const int32_t fd);
void check_pid_error (pid_t);
void c_r_handler (int s);
void p_r_handler (int s);
void on_signals ();
void off_signals ();
char **copa_to_cmdline (const copa *t);
size_t copa_elements (const copa *t);
process *execute_cmdline (char **cmdline, const struct termios *o, process *p);
int8_t cmdline_has_ (const char *_item, const char **cmdline);
int32_t set_redirects (const char **cmdline);

#endif
