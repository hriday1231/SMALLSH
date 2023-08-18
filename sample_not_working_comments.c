#define _POSIX_C_SOURCE 200809L
#include <stdint.h>
#include <signal.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <err.h>

// Global variables
int backgroundFlag = 0;
char *inputLine = NULL;
size_t inputLineSize = 0;
char *inputFile = NULL;
char *outputFile = NULL;
int childStatus = 0;
int backgroundChild = 0;
char *foregroundPid = "0";
char *backgroundPid = "";
int functionResult = 0;
int sourceFileDescriptor = 0;
int targetFileDescriptor = 0;

// String search and replace function
char *str_replace(char *str, const char *old, const char *new_str) {
    char *result;
    int i, cnt = 0;
    int new_len = strlen(new_str);
    int old_len = strlen(old);
    for (i = 0; str[i] != '\0'; i++) {
        if (strstr(&str[i], old) == &str[i]) {
            cnt++;
            i += old_len - 1;
        }
    }
    result = (char *)malloc(i + cnt * (new_len - old_len) + 1);
    i = 0;
    while (*str) {
        if (strstr(str, old) == str) {
            strcpy(&result[i], new_str);
            i += new_len;
            str += old_len;
        } else {
            result[i++] = *str++;
        }
    }
    result[i] = '\0';
    return result;
}
char *str_gsub(char **haystack, const char *needle, const char *sub) {
    char *str = *haystack;
    size_t needle_len = strlen(needle);
    size_t sub_len = strlen(sub);
    char *new_str = str_replace(str, needle, sub);
    if (!new_str) {
        return str;
    }
    free(str);
    *haystack = new_str;
    return new_str;
}
void manageBackgroundProcesses() {
    // Checking if any background processes have completed
    while ((backgroundChild = waitpid(0, &childStatus, WUNTRACED | 
WNOHANG)) > 0) {
        if (WIFEXITED(childStatus)) {
            printf("Child process %jd done. Exit status %d.\n", (intmax_t) 
backgroundChild, WEXITSTATUS(childStatus));
        }
        if (WIFSTOPPED(childStatus)) {
            kill(backgroundChild, SIGCONT);
            printf("Child process %jd stopped. Continuing.\n", (intmax_t) 
backgroundChild);
        }
        if (WIFSIGNALED(childStatus)) {
            printf("Child process %jd done. Signaled %d.\n", (intmax_t) 
backgroundChild, WTERMSIG(childStatus));
        }
    }
}
// Signal handler for SIGINT
void handle_SIGINT(int signo) {
    // Do nothing
}
// Signal handler for SIGTSTP
void handle_SIGTSTP(int signo) {
    // Ignore the signal
}
int main() {
// Register signal handlers
signal(SIGINT, handle_SIGINT);
signal(SIGTSTP, handle_SIGTSTP);
// Declarations for word pointers and counters
char **words = NULL;
char *temp = NULL;
size_t wordsSize = 0;
size_t wordsUsed = 0;
// Loop for accepting commands
for (;;) {
// Label to allow restarting command input
    start:
    // Resetting flags for background processes and I/O redirection
    backgroundFlag = 0;
    inputFile = NULL;
    outputFile = NULL;
    // Freeing up memory for previously entered command
    for (int i = 0; i < wordsUsed; i++) {
        free(words[i]);
        words[i] = NULL;
    }
    wordsUsed = 0;
    // Checking if any background processes have completed
    manageBackgroundProcesses();
    // Printing the command prompt
    char *PS1 = getenv("PS1");
    if (PS1 == NULL) {
        PS1 = "> ";
    }
    fprintf(stderr, "%s", PS1);
    // Reading a line of input from the user
    errno = 0;
    ssize_t lineLength = getline(&inputLine, &inputLineSize, stdin);
    if (feof(stdin) != 0) {
        fprintf(stderr, "\nexit\n");
        exit((int) *foregroundPid);
    }
    // Handling errors in input reading
    if (lineLength == -1 || errno != 0) {
        fprintf(stderr, "Error reading input.\n");
        goto start;
    }
    // Check if IFS environment variable is set, if not set it to default value
    if (getenv("IFS") == NULL) {
        setenv("IFS", " \t\n", 1);
    }
    // Word splitting
    // Get IFS environment variable
    char *IFS = getenv("IFS");
    // Parse the inputLine into words using strtok
    int i = 0;
    temp = strtok(inputLine, IFS);
    while (temp != NULL) {
        // Allocate memory for words and check if successful
        if (wordsUsed == wordsSize) {
            wordsSize += 10;
            words = realloc(words, wordsSize * sizeof(char *));
            if (words == NULL) {
                fprintf(stderr, "Error allocating memory for words.\n");
                exit(EXIT_FAILURE);
            }
        }
        // Copy the current token into a word
        words[wordsUsed++] = strdup(temp);
        i++;
        temp = strtok(NULL, IFS);
    }
    // Variable expansion
    // Iterate through the words, and substitute special characters with their corresponding values
    int j = 0;
    char *Pid = malloc(sizeof(int) * 8);
    sprintf(Pid, "%d", getpid());
    while (i > 0) {
        if (words[j] != NULL) {
            // Replace ~ with the value of the HOME environment variable
            if (strncmp(words[j], "~/", 2) == 0) {
                str_gsub(&words[j], "~", getenv("HOME"));
            }
            // Replace $$ with the process ID of the shell
            str_gsub(&words[j], "$$", Pid);
            // Replace $? with the exit status of the last foreground process
            str_gsub(&words[j], "$?", foregroundPid);
            // Replace $! with the process ID of the last background process
            str_gsub(&words[j], "$!", backgroundPid);
        }
        i--;
        j++;
    }
    // Parsing comments
    int y = 0;
    while (j > 0) {
        // Check if the current word is #
        if (strcmp(words[j - 1], "#") == 0) {
            // Set y to the index of the word before #
            y = j - 2;
            // Remove # from the words array
            words[j - 1] = NULL;
            // Decrement i to reflect the removal of #
            i--;
        }
        // Decrement j and increment i to continue iterating through the array
        j--;
        i++;
    }
    if (y > 0) {
        // Check if the word at index y is &
        if (strcmp(words[y], "&") == 0) {
            words[y] = NULL;
            backgroundFlag = 1;
            // Decrement y to reflect the removal of &
            y--;
        }
    }
    else {
        if (i > 0) {
            // Check if the word before the last word is &
            if (strcmp(words[i - 1], "&") == 0) {
                words[i - 1] = NULL;
                backgroundFlag = 1;
                // Decrement i to reflect the removal of &
                i--;
            }
        }
    }
    // Check if y is greater than 2 and the word before the current word is <
    if (y > 2 && strcmp(words[y - 1], "<") == 0) {
        // Set inputFile to the current word
        inputFile = words[y];
        // Set current word and the word before it to NULL
        words[y] = NULL;
        words[y - 1] = NULL;
        // Check if there are more than 4 words and the word 3 words before the current word is >
        if (y > 4 && strcmp(words[y - 3], ">") == 0) {
            // Set outputFile to the word 2 words before the current word
            outputFile = words[y - 2];
            // Set the word 2 and 3 words before the current word to NULL
            words[y - 2] = NULL;
            words[y - 3] = NULL;
        }
    }
    // Check if y is greater than 2 and the word before the current word is >
    else if (y > 2 && strcmp(words[y - 1], ">") == 0) {
        // Set outputFile to the current word
        outputFile = words[y];
        // Set current word and the word before it to NULL
        words[y] = NULL;
        words[y - 1] = NULL;
        // Check if there are more than 4 words and the word 3 words before the current word is <
        if (y > 4 && strcmp(words[y - 3], "<") == 0) {
            // Set inputFile to the word 2 words before the current word
            inputFile = words[y - 2];
            // Set the word 2 and 3 words before the current word to NULL
            words[y - 2] = NULL;
            words[y - 3] = NULL;
        }
    }
    // Check if i is greater than 3 and the word 2 words before the last word is <
    else if (i > 3 && strcmp(words[i - 2], "<") == 0) {
        // Set the input file to the last word
        inputFile = words[i - 1];
        words[i - 1] = NULL;
        words[i - 2] = NULL;
        // Check if there is a ">" two words before the input file
        if (i > 5 && strcmp(words[i - 4], ">") == 0) {
            // Set the output file to the word two places before the input file
            outputFile = words[i - 3];
            words[i - 3] = NULL;
            words[i - 4] = NULL;
        }
    }
    // Check if the second to last word is ">"
    else if (i > 3 && strcmp(words[i - 2], ">") == 0) {
        // Set the output file to the last word
        outputFile = words[i - 1];
        words[i - 1] = NULL;
        words[i - 2] = NULL;
        // Check if there is a "<" two words before the output file
        if (i > 5 && strcmp(words[i - 4], "<") == 0) {
            // Set the input file to the word two places before the output file
            inputFile = words[i - 3];
            words[i - 3] = NULL;
            words[i - 4] = NULL;
        }
    }
    // Execution
    // Built-in commands
    // Check if the first element in the 'words' array is null, then jump to the 'start' label
    if (words[0] == NULL) {
      goto start;
    }
    // Check if the first element in the 'words' array is not null
    if (words[0] != NULL) {
      
      // If the first element is 'exit'
      if (strcmp(words[0], "exit") == 0) {
        
        // If there are more than two elements in the 'words' array, print an error message and jump to the 'start' label
        if (words[2] != NULL) {
          fprintf(stderr, "Too many arguments.\n");
          goto start;
        }
        
        // If there is a second element in the 'words' array
        if (words[1] != NULL) {
          
          // Check if the second element is not a digit, print an error message and jump to the 'start' label
          if (!isdigit(*words[1])) {
            fprintf(stderr, "Argument is not an integer.\n");
            goto start;
          }
        }
        
        // If there is no second element in the 'words' array, print the 'exit' message and exit with the foreground process ID as the status code
        if (words[1] == NULL) {
          fprintf(stderr, "\nexit\n");
          exit((int) *foregroundPid);
        }
        
        // If there is a second element in the 'words' array, print the 'exit' message and exit with the second element as the status code
        fprintf(stderr, "\nexit\n");
        exit(atoi(words[1]));
      }
      
      // If the first element is 'cd'
      if (strcmp(words[0], "cd") == 0) {
        
        // If there are more than two elements in the 'words' array, print an error message and jump to the 'start' label
        if (words[2] != NULL) {
          fprintf(stderr, "Too many arguments.\n");
          goto start;
        }
        
        // If there is no second element in the 'words' array, set the second element to the value of the 'HOME' environment variable
        if (words[1] == NULL) {
          words[1] = getenv("HOME");
        }
        
        // Change the current working directory to the value of the second element in the 'words' array
        if (chdir(words[1]) == -1) {
          fprintf(stderr, "Error: Cannot change directory.\n");
        }
        goto start;
      }
    }
    // Non-built-in commands
    // Fork a new process
    pid_t spawnPid = fork();
    switch(spawnPid) {
      // Error occurred
      case -1:
        perror("fork()\n");
        exit(1);
        break;
      // Child process
      case 0:
        // If an input file is specified, open it and redirect input to it
        if (inputFile != NULL) {
          sourceFileDescriptor = open(inputFile, O_RDONLY);
          if (sourceFileDescriptor == -1) {
            perror("source open()");
            exit(1);
          } 
          functionResult = dup2(sourceFileDescriptor, 0);
          if (functionResult == -1) {
            perror("source dup2()");
            exit(2);
          }
        }
        // If an output file is specified, open it and redirect output to it
        if (outputFile != NULL) {
          targetFileDescriptor = open(outputFile, O_WRONLY | O_CREAT | 
O_TRUNC, 0777);
          if (targetFileDescriptor == -1) {
            perror("target open()");
            exit(1);
          }
          functionResult = dup2(targetFileDescriptor, 1);
          if (functionResult == -1) {
            perror("target dup2()");
            exit(2);
          }
        }
        // Execute the command with the specified arguments
        execvp(words[0], words);
        // Close any open file descriptors
        if (inputFile != NULL) {
          close(sourceFileDescriptor);
        }
        if (outputFile != NULL) {
          close(targetFileDescriptor);
        }
        // Exit the child process
        exit(0);
        break;
      // Parent process
      default:
        // If the command is not being run in the background, wait for it to finish
        if (backgroundFlag == 0) {
          spawnPid = waitpid(spawnPid, &childStatus, 0);
          // Get the exit status of the child process and store it in foregroundPid
          foregroundPid = malloc(10 * sizeof(int));
          sprintf(foregroundPid, "%d", WEXITSTATUS(childStatus));  
        } else {
          // Store the pid of the background process in backgroundPid
          backgroundPid = malloc(10 * sizeof(int));
          sprintf(backgroundPid, "%d", spawnPid);
        }
        // Jump back to the start of the loop
        goto start;
    }
  }
  return 0;
}




