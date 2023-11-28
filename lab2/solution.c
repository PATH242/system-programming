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
static void execute_expression (struct expr *e, struct command_line* line, struct parser *p)
{
	// Add command as first arg, and null to end of args.
	e->cmd.arg_capacity =  e->cmd.arg_count + 2;
	e->cmd.args = realloc(e->cmd.args, sizeof(e->cmd.args) * e->cmd.arg_capacity);
	e->cmd.args[e->cmd.arg_count+1] = NULL;
	for(int i = e->cmd.arg_count-1; i >= 0; i--)
	{
		e->cmd.args[i+1] = e->cmd.args[i];
	}
	e->cmd.args[0] = strdup(e->cmd.exe);
	e->cmd.arg_count += 2;
	// Handle CD exception.
	if (!strcmp(e->cmd.exe, "cd") && e->cmd.arg_count == 3) {
		command_line_delete(line);
		parser_delete(p);
		exit(CD_CODE);
	}
	else {
		execvp(e->cmd.exe, e->cmd.args);
		command_line_delete(line);
		parser_delete(p);
		exit(1);
	}
}

int get_exit_code(struct expr* e)
{
	int exit_code = 0;
	if (e->type == EXPR_TYPE_COMMAND)
	{
		if(!strcmp(e->cmd.exe, "exit")
			&& e->cmd.arg_count == 1)
		{
			char* end;
			exit_code = strtol(e->cmd.args[0], &end, 0);
		}
	}
	if(e)
	{
		e = e->next;
	}
	return exit_code;
}

// This function handles the execution of expressions and their connections by
// 1) Redirecting stdout to a pipe if we have | after an expression (in child process)
// 2) Save the input pipe associated with the prior fd
// 3) Redirecting stdin to that pipe in the following expression
// 4) Consecutive pipes are handled by using different FDs
// 5) Handling ||, and && by keeping track of the exit code of the prior process.
static char* execute_list_of_expressions(const struct expr *e, struct command_line *line, struct parser *p, int* exit_code) {
	int status_code = -1;
	*exit_code = 0;
	char* final_directory = NULL;
	int stdout_fd = -1, stdin_fd = -1, tmp_stdin_fd;
	int fd[2];
	int n_non_checked_processes = 0;
	int* non_checked_processes = NULL;

	// Get parameters:
	while (e != NULL)
	{
		// redirect output to pipe
		if(e->next && e->next->type == EXPR_TYPE_PIPE) {
			if (pipe(fd) == -1){
				printf("An error occurred while opening the pipe\n");
			}
			stdout_fd = dup(fd[1]);
			tmp_stdin_fd = dup(fd[0]);
			close(fd[0]);
			close(fd[1]);
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
				}
				if(stdout_fd != -1) {
					close(tmp_stdin_fd);
					dup2(stdout_fd, STDOUT_FILENO);
					close(stdout_fd);
				}
				if(non_checked_processes)
				{
					free(non_checked_processes);
				}
				execute_expression((struct expr*)e, line, p);
			}
			else {
				// if next is pipe, don't wait and start next process, else, wait
				if(e->next && e->next->type == EXPR_TYPE_PIPE)
				{
					if(!strcmp(e->cmd.exe, "cd") && e->cmd.arg_count == 1) {
						chdir(e->cmd.args[0]);
						final_directory = e->cmd.args[0];
					}
					n_non_checked_processes ++;
					non_checked_processes = realloc(non_checked_processes, n_non_checked_processes * sizeof(int));
					non_checked_processes[n_non_checked_processes-1] = pid2;
				}
				else
				{
					waitpid(pid2, &status_code, 0);
					status_code = WEXITSTATUS(status_code);
					// Handle CD exception
					if(status_code == CD_CODE && e->cmd.arg_count == 1)
					{
						chdir(e->cmd.args[0]);
						final_directory = e->cmd.args[0];
					}
					else
					{
						*exit_code = get_exit_code((struct expr*)e);
						if(!(*exit_code))
						{
							*exit_code = status_code;
						}
					}
				}
				if(stdin_fd != -1){
					close(stdin_fd);
					stdin_fd = -1;
				}
			}
		}
		else if (e->type == EXPR_TYPE_PIPE)
		{
			close(stdout_fd);
			stdout_fd = -1;
			stdin_fd = tmp_stdin_fd;
		}
		else if (e->type == EXPR_TYPE_AND)
		{
			// If last command wasn't a success, skip next.
			if(status_code != 0 && status_code != CD_CODE){
				e = e->next;
			}
		}
		else if (e->type == EXPR_TYPE_OR)
		{
			// If last one didn't fail, skip next command.
			if(status_code == 0 || status_code == CD_CODE) {
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
	while(n_non_checked_processes--)
	{
		waitpid(non_checked_processes[n_non_checked_processes], NULL, 0);
	}
	if(non_checked_processes)
	{
		free(non_checked_processes);
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
static int
execute_command_line(struct command_line *line, struct parser *p, int* exit_code)
{
	assert(line != NULL);
	if(line->head == line->tail 
		&& line->head->type == EXPR_TYPE_COMMAND
		&& !strcmp(line->head->cmd.exe, "exit")
		&& line->head->cmd.arg_count <= 1)
	{
		return EXIT_CODE;
	}
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
		char* final_directory = execute_list_of_expressions(line->head, line, p, exit_code);
		close(fd[0]);
		if(final_directory && fd_open){
			int size = (int)strlen(final_directory);
			write(fd[1], &size, sizeof(int));
			write(fd[1], final_directory, size);
			command_line_delete(line);
			parser_delete(p);
			exit(CD_CODE);
		}
		close(fd[1]);
		command_line_delete(line);
		parser_delete(p);
		free(final_directory);
		close(fd[1]);
		// Should be fine..
		exit(*exit_code);
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
				
				close(fd[1]);
				char* final_directory = NULL;
				int size;
				if(fd_open && status_code == CD_CODE){
					if(read(fd[0], &size, sizeof(int))) {
						final_directory = malloc(size +1);
					}
					if(read(fd[0], final_directory, size)) {
						*(final_directory + size) = '\0';
						chdir(final_directory);
					}
					free(final_directory);
				}
				else
				{
					*exit_code = status_code;
				}
				close(fd[0]);
			}
		}
		else
		{
			return 1;
		}
	}
	// close the output path to a file if necessary.
	if (line->out_type != OUTPUT_TYPE_STDOUT)
	{
		dup2(original_stdout, STDOUT_FILENO);
	}
	return 0;
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
	int exit = 0, exit_code = 0;
	int n_background_lines = 0;
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
			int status = execute_command_line(line, p, &exit_code);
			if(status == EXIT_CODE)
			{
				exit = 1;
				exit_code = 0;
				if(line->head->cmd.arg_count == 1)
				{
					exit_code = atoi(line->head->cmd.args[0]);
				}
				command_line_delete(line);
				break;
			}
			n_background_lines += status;
			command_line_delete(line);
		}
		if(exit)
		{
			break;
		}
	}
	while(n_background_lines --)
	{
		waitpid(-1, NULL, 0);
	}
	parser_delete(p);
	return exit_code;
}
