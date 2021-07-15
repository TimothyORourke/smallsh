/**
 * Name: Timothy O'Rourke
 * Class: CS 344 Winter 2019
 * Assignment: Program 3 - smallsh
 * Date: 3 March 2019
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>

const int MAX_COMMAND_CHARS = 2048;
int sigint_caught = 0;				// Stores SIGINT number caught by parent shell.
bool foreground_mode = false;		// Determines if shell is in foreground mode or not.

void DisplayPrompt();
bool IsBuiltInCommand(char* command);
void ParseCommand(char* command_line, int* counter, char command[64]);
void ParseArguments(char* command_line, int* counter, char* args[512], char command[64]);
void ParseInputFile(char* command_line, int* counter, char input_file[32]);
void ParseOutputFile(char* command_line, int* counter, char output_file[32]);
void ParseCommandLine(char* command_line, char command[64], char* args[512], char input_file[32], char output_file[32], bool* is_background, int* exit_status, int* exit_signal_num);
void DecideOnAction(char command[64], char* args[512], char input_file[32], char output_file[32], bool* is_background, int* exit_status, int* exit_signal_num, int child_processes[100]);
int RedirectInputAndOutput(char input_file[32], char output_file[32], bool* is_background, int* exit_status);
void AddChildProcess(int child_processes[100], int id);
void RemoveChildProcess(int child_processes[100], int id);
void CheckBackgroundProcesses(int child_processes[100]);

// Signal handling functions.
void TerminateForegroundProcess(int signal);
void ToggleForegroundMode(int signal);

// Built-in commands.
void ChangeDirectory(char* path[512]);
void ExitShell(int child_processes[100]);
void Status(int* exit_status, int* exit_signal_num);

int main(int argc, char* argv[]) {
	char* command_line = NULL;				// Used to store the user input command line.
	size_t size = MAX_COMMAND_CHARS;
	int command_line_size = 0;				
	int exit_status = 0;					// Used to store exit number.
	int exit_signal_num = -1;				// Used to store exit signal number.

	char command[64] = "";					// Used to hold the command.
	char* args[512];						// Used to hold the list of arguments.
	char input_file[32] = "";				// Used to store an input file.
	char output_file[32] = "";				// Used to store an output file.
	bool is_background;						// Used to tell if this command should be processed in the background.
	int child_processes[100];				// Used to keep track of child processes.

	int i;
	for (i = 0; i < 100; i++)
		child_processes[i] = -1;			// Initializing child process array.

	// Setup of the signal handlers for SIGINT and SIGTSTP.
	struct sigaction SIGINT_action = {0};
	SIGINT_action.sa_handler = TerminateForegroundProcess;	// Only parent catches. So it lives, and child processes die by default.
	SIGINT_action.sa_flags = SA_RESTART;
	sigaction(SIGINT, &SIGINT_action, NULL);

	struct sigaction SIGTSTP_action = {0};
	SIGTSTP_action.sa_handler = ToggleForegroundMode;		// Toggle foreground mode on SIGTSTP received.
	SIGTSTP_action.sa_flags = SA_RESTART;
	sigaction(SIGTSTP, &SIGTSTP_action, NULL);

	// Main program loop.
	while (true) {
		memset(command, '\0', sizeof(command));			// Reset command array.
		memset(args, '\0', sizeof(args));				// Reset arguments array.
		memset(input_file, '\0', sizeof(input_file));	// Reset input file array.
		memset(output_file, '\0', sizeof(output_file));	// Reset output file array.
		is_background = false;							// Set background boolean to false.

		DisplayPrompt();								// Display prompt.
		
		// Read in the command line from the user.
		command_line_size = getline(&command_line, &size, stdin);
	
		// Removes the newline character from the input and fixes size variable.
		if (strlen(command_line) == 1)
			command_line[0] = '\0';
		else
			strtok(command_line, "\n");
	
		command_line_size--;

		// Parses the line given to determine what action to take.
		ParseCommandLine(command_line, command, args, input_file, output_file, &is_background, &exit_status, &exit_signal_num);

		// Decide on the action to take.
		DecideOnAction(command, args, input_file, output_file, &is_background, &exit_status, &exit_signal_num, child_processes);
	}

	return 0;
}

void DisplayPrompt() {
	char* m = ": ";
	write(STDOUT_FILENO, m, 2);
}

bool IsBuiltInCommand(char* command) {
	if ( strcmp(command, "exit") == 0 || strcmp(command, "cd") == 0 || strcmp(command, "status") == 0 )
		return true;
	else
		return false;	
}

void ParseCommand(char* command_line, int* counter, char command[64]) {
	while ( command_line[*counter] != ' ' && command_line[*counter] != '\0' ) { // While command isn't fully read.
			char c = command_line[(*counter)++];	// Retrieve next character.
			command[strlen(command)] = c;			// Append it to the command.
		}
		(*counter)++;
}

void ParseArguments(char* command_line, int* counter, char* args[512], char command[64]) {
	int num_of_args = 0;
	args[num_of_args++] = command;	// Place command in first position of args for use with exec family.

	// Loops through the arguments.
	while ( command_line[*counter] != '<' && command_line[*counter] != '>' && command_line[*counter] != '\0'  ) {	
		char* temp_arg = malloc(sizeof(char) * 64);
		memset(temp_arg, '\0', sizeof(temp_arg));

		if (command_line[*counter] == '&' && command_line[*counter + 1] == '\0') {	// If an '&' at the end of the command line, then break.
			break;
		}

		// Loops through each char of an argument.
		while ( command_line[*counter] != ' ' && command_line[*counter] != '\0' ) {								

			// If a '$$' expansion is found, expand it into the PID.
			if (command_line[*counter] == '$' && command_line[(*counter) + 1] == '$') {
				char* temp = malloc(sizeof(char) * 64);
				memset(temp, '\0', sizeof(temp));

				sprintf(temp, "%s%d", temp_arg, getpid());	// Get the PID. Append to the argument.
				free(temp_arg);
				temp_arg = temp;							// Set to temp_arg.
				fflush(stdout);
				(*counter) += 2;							// Skip these two characters.
				continue;
			}

			char c = command_line[(*counter)++];	// Retrieve next character of the arg.
			temp_arg[strlen(temp_arg)] = c;			// Append it to the arg string.
		}
		(*counter)++;
		args[num_of_args++] = temp_arg;				// Add the arg string to the list of args.
	}
	args[num_of_args] = NULL;		// Place NULL in case args is called with exec family.
}

void ParseInputFile(char* command_line, int* counter, char input_file[32]) {
	if ( command_line[*counter] != '>' && command_line[*counter] != '&' && command_line[*counter] != '\0' ) {
		(*counter) += 2; // Skip the '<' character and space.
		while ( command_line[*counter] != ' ' && command_line[*counter] != '\0' ) {
			char c = command_line[(*counter)++];	// Retrieve the next character of the input file.
			input_file[strlen(input_file)] = c;		// Append it to the string.
		}
		(*counter)++; // Skip space.
	}	
}

void ParseOutputFile(char* command_line, int* counter, char output_file[32]) {
	if ( command_line[*counter] != '&' && command_line[*counter] != '\0' ) {
		(*counter) += 2; // Skip the '>' character and space.
		while ( command_line[*counter] != ' ' && command_line[*counter] != '\0' ) {
			char c = command_line[(*counter)++];	// Retrieve the next character of the output file.
			output_file[strlen(output_file)] = c;	// Append it to the string.
		}
		(*counter)++; // Skip space.
	}
}

int RedirectInputAndOutput(char input_file[32], char output_file[32], bool* is_background, int* exit_status) {
	int value = 1;	// Return value of 1 means files could be opened. 0 if they can't.

	if (strcmp(input_file, "") != 0) {			// If an input file was given.
		int iFile = open(input_file, O_RDONLY);	// Open.
		if (iFile >= 0) {						// If success, redirect.
			dup2(iFile, 0);
			close(iFile);
		}
		else {									// Else, display error.
			printf("cannot open file for input\n");
			fflush(stdout);
			value = 0;
		}
	}
	else if (strcmp(input_file, "") == 0 && *is_background == true) {	// No file given and is a background process.
		int iFile = open("/dev/null", O_RDONLY);						// Open /dev/null.
		if (iFile >=0) {												// If success, redirect.
			dup2(iFile, 0);
			close(iFile);
		}
		else {															// Else, display error.
			printf("cannot open file for input\n");
			fflush(stdout);
			value = 0;
		}
	}

	if (strcmp(output_file, "") != 0 ) {												// If an output file was given.
		int oFile = open(output_file, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);	// Open.
		if (oFile >= 0) {																// If success, redirect.
			dup2(oFile, 1);
			close(oFile);
		}
		else {																			// Else, display error.
			printf("cannot open file for output\n");
			fflush(stdout);
			value = 0;
		}
	}
	else if (strcmp(output_file, "") == 0 && *is_background == true) {	// No file given and is a background process.
		int oFile = open("/dev/null", O_WRONLY | O_TRUNC);				// Open /dev/null.
		if (oFile >= 0) {												// If success, redirect.
			dup2(oFile, 1);					
			close(oFile);
		}
		else {															// Else, display error.
			printf("cannot open file for output\n");
			fflush(stdout);
			value = 0;
		}
	}

	return value;
}

void ParseCommandLine(char* command_line, char command[64], char* args[512], char input_file[32], char output_file[32], bool* is_background, int* exit_status, int* exit_signal_num) {
	int counter = 0;						// Used to traverse the command line during parsing.
	
	// Check if this is a comment line.
	if ( command_line[counter] == '#' ) {
		char c = command_line[counter];
		command[0] = c;	// Set command to '#' if this is a comment line.
	}
	else {		// If not, parse the command.
		ParseCommand(command_line, &counter, command);
	}

	ParseArguments(command_line, &counter, args, command);	// Parse the arguments.
	ParseInputFile(command_line, &counter, input_file);		// Parse the input file.
	ParseOutputFile(command_line, &counter, output_file);	// Parse the output file.

	// Parse the ampersand if it exists and mode is not foreground only. 
	if ( command_line[counter] == '&' && foreground_mode == false ) {
		*is_background = true;
	}	
}

void DecideOnAction(char command[64], char* args[512], char input_file[32], char output_file[32], bool* is_background, int* exit_status, int* exit_signal_num, int child_processes[100]) {

	// Check if any background child processes have finished.
	CheckBackgroundProcesses(child_processes);

	// Determine if comment, built-in, or external command.
	if (strcmp(command, "#") == 0 || strcmp(command, "") == 0) {
		// Do nothing on comment or blank. Ends up displaying prompt again.
	}
	else if ( IsBuiltInCommand(command) ) {				// Is a built in command. Calls the current command.
		if ( strcmp(command, "cd") == 0 ) {
			ChangeDirectory(args);
		}
		else if ( strcmp(command, "exit") == 0 ) {
			ExitShell(child_processes);
		}
		else if ( strcmp(command, "status") == 0 ) {
			Status(exit_status, exit_signal_num);
		}
	}
	else {												// Isn't built in. Forks and executes the command.
		pid_t spawnPid = -5;
		int childExitValue = -5;

		spawnPid = fork();		// Fork.
		switch (spawnPid) {
			case -1 :
				perror("spawnPid: -1\n");
				exit(1);
				break;
			case 0 :	// Child ends up in this case.
				// If no errors occurr with file redirecting.
				if (RedirectInputAndOutput(input_file, output_file, is_background, exit_status)) {
					// Set to IGNORE SIGTSTP for child process.
					struct sigaction ignore_SIGTSTP = {0};
					ignore_SIGTSTP.sa_handler = SIG_IGN;
					sigaction(SIGTSTP, &ignore_SIGTSTP, NULL);

					execvp(args[0], args);	// Execute the command with the arguments.
					printf("%s: no such file or directory\n", args[0]);	// If failure, display error and exit with '1'.
					fflush(stdout);
					exit(1);
				}
				else {	// Exit if file redirecting errors occurred.
					exit(1);
				}
				break;
			default :
				break;
		}

		// If this is a foreground process, the shell waits for it to finish.
		if (*is_background == false) {
			sigint_caught = 0;	// Set sigint_caught to 0. This is so that foreground process termination is only displayed when there is a process.

			waitpid(spawnPid, &childExitValue, 0);

			if (sigint_caught) {	// If SIGINT, display signal number that terminated the child foreground process.
				printf("terminated by signal %d\n", sigint_caught);
				fflush(stdout);
				*exit_status = -1;					// Set to -1 so that the program knows most recent exit value is a signal.
				*exit_signal_num = sigint_caught;	// Set the most recent signal number that is returned by Status().
				sigint_caught = 0;					
			}
			else {										// Child process was not terminated by SIGINT.
				if (WIFEXITED(childExitValue) != 0) { 	// If it ended by exit, set the previous exit status.
					*exit_status = WEXITSTATUS(childExitValue);
				}
				else {					   	// Else, set the previous terminating signal number.
					*exit_status = -1; 		// Setting this to -1 is what Status() uses to determine whether to show exit status or signal number.
					*exit_signal_num = WTERMSIG(childExitValue);
				}
			}
		}
		else {	// If this is a background process, display the PID.
			AddChildProcess(child_processes, spawnPid);	// Add to list of running background processes.
			printf("background pid is %d\n", spawnPid);	// Display the PID of this background process.
			fflush(stdout);
		}
			
	}
}

void CheckBackgroundProcesses(int child_processes[100]) {
	int i;
	for (i = 0; i < 100; i++) {
		if (child_processes[i] != -1) {	// Wait with NOHANG for each existing background process.
			int childExitMethod = -5;
			pid_t childPid = waitpid(child_processes[i], &childExitMethod, WNOHANG);
			if (childPid > 0) { // A process has finished.
				if (WIFEXITED(childExitMethod) != 0) { // Ended by return.
					printf("background pid %d is done: exit value %d\n", childPid, WEXITSTATUS(childExitMethod));
					fflush(stdout);
				}
				else { // Ended by signal.
					printf("background pid %d is done: terminated by signal %d\n", childPid, WTERMSIG(childExitMethod));
					fflush(stdout);
				}
				RemoveChildProcess(child_processes, child_processes[i]);
			}
		}
	}
}

void ChangeDirectory(char* path[512]) {
	
	if ( path[1] == NULL ) {	// If cd is called with no arguments,
		chdir( getenv("HOME") );	// set current directory to HOME.
	}
	else {						// Else, set to the given path.
		chdir( path[1] );
	}

}

void ExitShell(int child_processes[100]) {
	int i;
	for (i = 0; i < 100; i++) {
		if (child_processes[i] != -1) {
			kill(child_processes[i], SIGKILL);
		}
	}
	printf("\n");
	exit(0);
}

void Status(int* exit_status, int* exit_signal_num) {
	if (*exit_status != -1) {	// If exit_status is not -1, then last foreground process ended by exit.
		printf("exit value %d\n", *exit_status);	// Display exit value.
		fflush(stdout);
	}
	else {						// Else, exited by signal.
		printf("terminated by signal %d\n", *exit_signal_num);	// Display terminating signal number.
		fflush(stdout);
	}
}

void AddChildProcess(int child_processes[100], int id) {
	int i;
	for (i = 0; i < 100; i++) {
		if (child_processes[i] == -1) {	// Place at next position with value of '-1'.
			child_processes[i] = id;
			break;
		}
	}
}

void RemoveChildProcess(int child_processes[100], int id) {
	int i;
	for (i = 0; i < 100; i++) {
		if (child_processes[i] == id) {	// Find process id and replace with '-1'.
			child_processes[i] = -1;
			break;
		}
	}
}

void TerminateForegroundProcess(int signal) {
	// This function gets called by parent on SIGINT to avoid termination.
	// Child processes get terminated by default.
	sigint_caught = signal;
}

void ToggleForegroundMode(int signal) {
	// This function gets called by parent only on SIGTSTP. Ignored by child processes.
	// Toggles the foreground mode. Also displays message that mode was changed.
	if (foreground_mode == false) {
		foreground_mode = true;
		char* m1 = "\nEntering foreground-only mode (& is now ignored)\n";
		write(STDOUT_FILENO, m1, 50);
	}
	else {
		foreground_mode = false;
		char* m2 = "\nExiting foreground-only mode\n";
		write(STDOUT_FILENO, m2, 30);
	}
	DisplayPrompt();
}