#include "parser.h"

#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <stdlib.h>

static void execute_expression (const struct expr *e)
{
	// Handle exit exception 
	if(!strcmp(e->cmd.exe, "exit") && e->cmd.arg_count == 1) {
		// Unique exit code
		exit(5);
	}
	else
	if (!strcmp(e->cmd.exe, "cd") && e->cmd.arg_count == 2) {
		chdir(e->cmd.args[1]);
	}
	else {
		char* arg = NULL;
		e->cmd.args[e->cmd.arg_count] = arg;
		execvp(e->cmd.exe, e->cmd.args);
		printf("This shouldn't be printed while executing the command\n");
		exit(1);
	}
}

static int execute_list_of_expressions(const struct expr *e) {
	int wstatus = -1;
	int status_code = -1;
	// Get parameters:
	while (e != NULL)
	{
		if (e->type == EXPR_TYPE_COMMAND)
		{
			int pid2 = fork();
			if (pid2 == -1) {
				printf("An error occured with the second fork for execution\n");
				return 1;
			}
			if(pid2 == 0){
				execute_expression(e);
			}
			else {
				wait(&wstatus);
				if(WIFEXITED(wstatus)) {
					status_code = WEXITSTATUS(wstatus);
					if (status_code == 5) {
						// Our unique exit code.
						printf("Exiting terminal\n");
						exit(5);
					}
					else
					if (status_code != 0) {
						printf("Execution of command failed with code %d\n", status_code);
					}
				}
				else {
					printf("Waited for command execution result but none\n");
				}
			}
		}
		else if (e->type == EXPR_TYPE_PIPE)
		{
			// Create a pipe to connect current output with input of next expression
		}
		else if (e->type == EXPR_TYPE_AND)
		{
			// Execute next command if last one was sucess
		}
		else if (e->type == EXPR_TYPE_OR)
		{
			// Execute next command if last one was failure
		}
		else
		{
			assert(false);
		}
		e = e->next;
	}
	exit(0);
}

static int
execute_command_line(const struct command_line *line)
{
	assert(line != NULL);
	// Redirect output path to a file if necessary.
	if (line->out_type == OUTPUT_TYPE_FILE_NEW)
	{
		int file = open(line->out_file, O_WRONLY | O_CREAT, 0777);
		dup2(file, STDOUT_FILENO);
		close(file);
	}
	else if (line->out_type == OUTPUT_TYPE_FILE_APPEND)
	{
		int file = open(line->out_file, O_WRONLY | O_CREAT| O_APPEND, 0777);
		dup2(file, STDOUT_FILENO);
		close(file);
	}

	// Create child process to execute list of expressions
	int pid = fork();
	if (pid == -1)
	{
		printf("An error occured with fork at command line execution\n");
		return 1;
	}

	// child process
	if (pid == 0)
	{
		const struct expr *e = line->head;
		execute_list_of_expressions(e);
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
				// Our unique exit code.
				if (status_code == 5)
				{
					exit(0);
				}
			}
		}
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
