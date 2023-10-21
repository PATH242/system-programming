#include "parser.h"

#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <stdlib.h>

// constants used in code
#define EXIT_CODE 	5
#define CD_CODE 	6

// Simple execution of one expression.
static void execute_expression (const struct expr *e)
{
	// Handle exit exception 
	if(!strcmp(e->cmd.exe, "exit") && e->cmd.arg_count == 1) {
		exit(EXIT_CODE);
	}
	else
	if (!strcmp(e->cmd.exe, "cd") && e->cmd.arg_count == 2) {
		exit(CD_CODE);
	}
	else {
		e->cmd.args[e->cmd.arg_count] = NULL;
		execvp(e->cmd.exe, e->cmd.args);
		exit(1);
	}
}

// This function handles the execution of expressions and their connections by
// 1) Redirecting stdout to a pipe if we have | after an expression (in child process)
// 2) Save the input pipe associated with the prior type
// 3) Redirecting stdin to that pipe in the following expression
// 4) Consecutive pipes are handled by using different FDs
// 5) Handling ||, and && by keeping track of the exit code of the prior process.
static char* execute_list_of_expressions(const struct expr *e) {
	int wstatus = -1;
	int status_code = -1;
	char* final_directory = NULL;
	// int original_stdout = -1, original_stdin = -1;
	int stdout_fd = -1, stdin_fd = -1;
	int fd[2];
	// Get parameters:
	while (e != NULL)
	{
		// redirect output to pipe
		if(e->next && e->next->type == EXPR_TYPE_PIPE) {
			if (pipe(fd) == -1){
				printf("An error occurred while opening the pipe\n");
			}
			stdout_fd = fd[1];
		}

		if (e->type == EXPR_TYPE_COMMAND)
		{
			int pid2 = fork();
			if (pid2 == -1) {
				printf("An error occurred with the second fork for execution\n");
				exit(1);
			}
			if(pid2 == 0) {
				if(stdin_fd != -1){
					dup2(stdin_fd, STDIN_FILENO);
					close(stdin_fd);
					//printf("Input redirected to pipe\n");
				}
				if(stdout_fd != -1) {
					//fprintf(stderr, "output redirected to pipe\n");
					dup2(stdout_fd, STDOUT_FILENO);
					close(stdout_fd);
				}
				execute_expression(e);
			}
			else {
				// if next is pipe, don't wait and start next process, else, wait
				if(e->next && e->next->type == EXPR_TYPE_PIPE)
				{
					if(!strcmp(e->cmd.exe, "cd") && e->cmd.arg_count == 2) {
						chdir(e->cmd.args[1]);
						final_directory = e->cmd.args[1];
					}
				}
				else
				{
					wait(&wstatus);
					if(WIFEXITED(wstatus)) {
						status_code = WEXITSTATUS(wstatus);
						if (status_code == EXIT_CODE) {
							// printf("Exiting terminal\n");
							exit(0);
						}
						else
						if(status_code == CD_CODE) {
							chdir(e->cmd.args[1]);
							final_directory = e->cmd.args[1];
						}
						// else
						// if (status_code != 0) {
						// 	//printf("Execution of command failed with code %d\n", status_code);
						// }
					}

				}

			}
		}
		else if (e->type == EXPR_TYPE_PIPE)
		{
			close(stdout_fd);
			stdout_fd = -1;
			stdin_fd = fd[0];
		}
		else if (e->type == EXPR_TYPE_AND)
		{
			// If last command wasn't a success, skip next.
			if(status_code != 0 && status_code != 6){
				e = e->next;
			}
		}
		else if (e->type == EXPR_TYPE_OR)
		{
			// If last one didn't fail, skip next command.
			if(status_code == 0 || status_code == 6) {
				e = e->next;
			}
		}
		else
		{
			assert(false);
		}
		if(e)
		{
			e = e->next;
		}
	}
	return final_directory;
}

// This function handles the external effects of a command line
// 1) Redirecting ouput to a file if necessary
// 2) If process is not a background process, 
// 	  it waits for it and processes its exit code.
// 2) Exits code if it gets a special exit code.
// 3) Changing directory (by using pipes between child and parent).
// 4) Redirecting output back to STDOUT
static void
execute_command_line(const struct command_line *line)
{
	assert(line != NULL);
	// Redirect output path to a file if necessary.
	int original_stdout;
	if (line->out_type == OUTPUT_TYPE_FILE_NEW)
	{
		int file = open(line->out_file, O_WRONLY | O_CREAT |  O_TRUNC, 0777);
		original_stdout = dup(STDOUT_FILENO);
		dup2(file, STDOUT_FILENO);
		close(file);
	}
	else if (line->out_type == OUTPUT_TYPE_FILE_APPEND)
	{
		int file = open(line->out_file, O_WRONLY | O_CREAT| O_APPEND, 0777);
		original_stdout = dup(STDOUT_FILENO);
		dup2(file, STDOUT_FILENO);
		close(file);
	}

	// Create pipe for cd path info 
	int fd[2];
	bool fd_open = true;
	if (pipe(fd) == -1){
		printf("An error occurred while opening the pipe\n");
		fd_open = false;
	}

	// Create child process to execute list of expressions
	int pid = fork();
	if (pid == -1)
	{
		printf("An error occurred with fork at command line execution\n");
		exit(1);
	}

	// child process
	if (pid == 0)
	{
		const struct expr *e = line->head;
		char* final_directory = execute_list_of_expressions(e);
		close(fd[0]);
		if(final_directory && fd_open){
			int size = (int)strlen(final_directory);
			write(fd[1], &size, sizeof(int));
			write(fd[1], final_directory, size);
			close(fd[1]);
			free(final_directory);
			exit(6);
		}
		close(fd[1]);
		exit(0);
	}
	// Parent process.
	else
	{
		// Check if process should be in background or not, if not, wait for it to execute.
		if (!line->is_background)
		{
			int wstatus;
			wait(&wstatus);
			if (WIFEXITED(wstatus))
			{
				int status_code = WEXITSTATUS(wstatus);
				if (status_code == EXIT_CODE)
				{
					exit(0);
				}
				close(fd[1]);
				char* final_directory = NULL;
				int size;
				if(fd_open && status_code == CD_CODE){
					if(read(fd[0], &size, sizeof(int))) {
						final_directory = malloc(size+1);
					}
					if(read(fd[0], final_directory, size)) {
						chdir(final_directory);
						// printf("directory is changed to %s\n", final_directory);
					}
					free(final_directory);
				}
				close(fd[0]);
			}
		}
	}
	// close the output path to a file if necessary.
	if (line->out_type != OUTPUT_TYPE_STDOUT)
	{
		dup2(original_stdout, STDOUT_FILENO);
	}
	return;
}

// Utility function to print command line, uncomment for use.
// static void print_command_line(const struct command_line* line)
// {
// 	assert(line != NULL);
// 	printf("================================\n");
// 	printf("Command line:\n");
// 	printf("Is background: %d\n", (int)line->is_background);
// 	printf("Output: ");
// 	if (line->out_type == OUTPUT_TYPE_STDOUT) {
// 		printf("stdout\n");
// 	} else if (line->out_type == OUTPUT_TYPE_FILE_NEW) {
// 		printf("new file - \"%s\"\n", line->out_file);
// 	} else if (line->out_type == OUTPUT_TYPE_FILE_APPEND) {
// 		printf("append file - \"%s\"\n", line->out_file);
// 	} else {
// 		assert(false);
// 	}
// 	printf("Expressions:\n");
// 	const struct expr *e = line->head;
// 	while (e != NULL) {
// 		if (e->type == EXPR_TYPE_COMMAND) {
// 			printf("\tCommand: %s", e->cmd.exe);
// 			for (uint32_t i = 0; i < e->cmd.arg_count; ++i)
// 				printf(" %s", e->cmd.args[i]);
// 			printf("\n");
// 		} else if (e->type == EXPR_TYPE_PIPE) {
// 			printf("\tPIPE\n");
// 		} else if (e->type == EXPR_TYPE_AND) {
// 			printf("\tAND\n");
// 		} else if (e->type == EXPR_TYPE_OR) {
// 			printf("\tOR\n");
// 		} else {
// 			assert(false);
// 		}
// 		e = e->next;
// 	}
// }

int main(void)
{
	const size_t buf_size = 1024;
	char buf[buf_size];
	int rc;
	struct parser *p = parser_new();
	while ((rc = read(STDIN_FILENO, buf, buf_size)) > 0) {
		parser_feed(p, buf, rc);
		struct command_line *line = NULL;
		while (true) {
			enum parser_error err = parser_pop_next(p, &line);
			if (err == PARSER_ERR_NONE && line == NULL)
				break;
			if (err != PARSER_ERR_NONE) {
				printf("Error: %d\n", (int)err);
				continue;
			}
			execute_command_line(line);
			command_line_delete(line);
		}
	}
	parser_delete(p);
	return 0;
}
