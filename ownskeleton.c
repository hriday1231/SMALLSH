#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE
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
#include <sys/stat.h>


#ifndef MAX_WORDS
#define MAX_WORDS 512
#endif

pid_t background_pid = -1;
int background = 0;
int exit_status = 0;

char *words[MAX_WORDS];
size_t wordsplit(char const *line);
char * expand(char const *word);

void sigint_handler(int signum) {
}

int main(int argc, char *argv[])
{
  FILE *input = stdin;
  char *input_fn = "(stdin)";
  if (argc == 2) {
    input_fn = argv[1];
    input = fopen(input_fn, "re");
    if (!input) err(1, "%s", input_fn);
  } else if (argc > 2) {
    errx(1, "too many arguments");
  }

  char *line = NULL;
  size_t n = 0;
  int is_eof = 0;
  for(;;){
  //prompt:;
    /* TODO: Manage background processes */

    signal(SIGTSTP, SIG_IGN);
    signal(SIGINT, sigint_handler);

    for (size_t i = 0; i < MAX_WORDS; ++i) {
        free(words[i]);
        words[i] = NULL;
    }

    /* TODO: prompt */
    if (input == stdin) {
        signal(SIGINT, SIG_IGN);
        // Get PS1 environment variable or set default value
        char *ps1 = getenv("PS1");
        // if (ps1 == NULL || *ps1 == '\0') {
        //   ps1 = "$"; // Default prompt if PS1 is not set or empty
        // }
        // printf("%s", ps1);
        if (ps1 == NULL || strlen(ps1) == 0) {
          printf("$");
        } else {
          printf("%s", ps1);
        }
    }

    ssize_t line_len = getline(&line, &n, input);
    if (line_len == -1) {
      if (feof(input)) {
        // EOF condition
        is_eof = 1;
        break;
      } else {
        // Error condition
        err(1, "%s", input_fn);
      }
    }
    
    size_t nwords = wordsplit(line);
    for (size_t i = 0; i < nwords; ++i) {
      // fprintf(stderr, "Word %zu: %s  -->  ", i, words[i]);
      char *exp_word = expand(words[i]);
      free(words[i]);
      words[i] = exp_word;
      // fprintf(stderr, "%s\n", words[i]);
    }

    // exit functionality
    if (nwords > 0 && strcmp(words[0], "exit") == 0) {
      if (nwords == 1) {
          int exit_status = atoi(expand("$?")); // Get the exit status of the last command
          exit(exit_status);
      } else if (nwords == 2) {
          int exit_status = atoi(words[1]);
          exit(exit_status);
      } else if (nwords > 2){
          errx(1, "exit: too many arguments");
      }
    }
    // implement cd functionality here
    else if (nwords > 0 && strcmp(words[0], "cd") == 0){
       if (nwords == 1) {
          // No argument provided, use HOME environment variable
          char *home_path = getenv("HOME");
          if (home_path == NULL) {
              errx(1, "cd: HOME environment variable not set");
          }
          if (chdir(home_path) != 0) {
              err(1, "cd: %s", home_path);
          }
        } else if (nwords == 2) {
          // Change directory to the specified path
          if (chdir(words[1]) != 0) {
              err(1, "cd: %s", words[1]);
          }
        } else if (nwords > 2){
          errx(1, "cd: too many arguments");
        }
    }
    // Implement non-inbuilt functions
    else {
        pid_t child_pid = fork();
        if (child_pid == -1) {
            perror("fork");
            exit(1);
        } 
        else if (child_pid == 0) {
            // Child process
            // Restore signals to their original dispositions
            signal(SIGTSTP, SIG_IGN);
            signal(SIGINT, SIG_DFL);
            
            // Check for background execution
            int background = 0;
            if (nwords > 0 && strcmp(words[nwords - 1], "&") == 0) {
                background = 1;
                background_pid = getpid();
                words[--nwords] = NULL; // Remove the "&" operator
            }
            
            // Handle redirection
            char *input_file = NULL;
            char *output_file = NULL;
            int redirect_input = 0;
            int redirect_output = 0;
            
            for (size_t i = 0; i < nwords; ++i) {
              if (strcmp(words[i], "<") == 0) {
                if (i + 1 < nwords) {
                    input_file = words[i + 1];
                    redirect_input = 1;
                    words[i] = NULL;
                    words[i + 1] = NULL;
                    ++i;
                } else {
                    fprintf(stderr, "Syntax error: Missing input file after '<'\n");
                    exit(1);
                }
              } else if (strcmp(words[i], ">") == 0) {
                if (i + 1 < nwords) {
                    output_file = words[i + 1];
                    redirect_output = 1;
                    words[i] = NULL;
                    words[i + 1] = NULL;
                    ++i;
                } else {
                    fprintf(stderr, "Syntax error: Missing output file after '>'\n");
                    exit(1);
                }
              } else if (strcmp(words[i], ">>") == 0) {
                if (i + 1 < nwords) {
                    output_file = words[i + 1];
                    redirect_output = 2;
                    words[i] = NULL;
                    words[i + 1] = NULL;
                    ++i;
                } else {
                    fprintf(stderr, "Syntax error: Missing output file after '>>'\n");
                    exit(1);
                }
              }
            }
            
            // Redirect input and output if necessary
            if (redirect_input) {
              freopen(input_file, "r", stdin);
            }
            if (redirect_output) {
              if (redirect_output == 1) {
                  FILE *output = fopen(output_file, "w");
                  if (output) {
                      fclose(output);
                      chmod(output_file, 0777); // Set permissions to 0644
                      freopen(output_file, "w", stdout);
                  } else {
                      perror("fopen");
                      exit(1);
                  }
              } else if (redirect_output == 2) {
                  FILE *output = fopen(output_file, "a");
                  if (output) {
                      fclose(output);
                      chmod(output_file, 0777); 
                      freopen(output_file, "a", stdout);
                  } else {
                      perror("fopen");
                      exit(1);
                  }
              }
            }
            
            // Execute the command
            execvp(words[0], words);
            
            // If execvp fails, print an error message and exit
            perror("execvp");
            exit(1);
        } else {
            // Parent process
            if (!background) {
                // Wait for the child to complete if not running in the background
                int status;
                waitpid(child_pid, &status, 0);

                if (WIFEXITED(status)) {
                    exit_status = WEXITSTATUS(status);
                } else if (WIFSIGNALED(status)) {
                    exit_status = 128 + WTERMSIG(status);
                } 
            }
            
        }
    }
  }
  if (!is_eof) {
    // Perform cleanup before exiting
    free(line);
    fclose(input);
  }
  return 0;
}

char *words[MAX_WORDS] = {0};

size_t wordsplit(char const *line) {
  size_t wlen = 0;
  size_t wind = 0;

  char const *c = line;
  for (;*c && isspace(*c); ++c); /* discard leading space */

  for (; *c;) {
    if (wind == MAX_WORDS) break;
    if (*c == '#') break;
    for (;*c && !isspace(*c); ++c) {
      if (*c == '\\') ++c;
      void *tmp = realloc(words[wind], sizeof **words * (wlen + 2));
      if (!tmp) err(1, "realloc");
      words[wind] = tmp;
      words[wind][wlen++] = *c; 
      words[wind][wlen] = '\0';
    }
    ++wind;
    wlen = 0;
    for (;*c && isspace(*c); ++c);
  }
  return wind;
}

char param_scan(char const *word, char **start, char **end)
{
  static char *prev;
  if (!word) word = prev;
  
  char ret = 0;
  *start = NULL;
  *end = NULL;
  char *s = strchr(word, '$');
  if (s) {
    char *c = strchr("$!?", s[1]);
    if (c) {
      ret = *c;
      *start = s;
      *end = s + 2;
    }
    else if (s[1] == '{') {
      char *e = strchr(s + 2, '}');
      if (e) {
        ret = '{';
        *start = s;
        *end = e + 1;
      }
    }
  }
  prev = *end;
  return ret;
}

char *
build_str(char const *start, char const *end)
{
  static size_t base_len = 0;
  static char *base = 0;

  if (!start) {
    char *ret = base;
    base = NULL;
    base_len = 0;
    return ret;
  }
  size_t n = end ? end - start : strlen(start);
  size_t newsize = sizeof *base *(base_len + n + 1);
  void *tmp = realloc(base, newsize);
  if (!tmp) err(1, "realloc");
  base = tmp;
  memcpy(base + base_len, start, n);
  base_len += n;
  base[base_len] = '\0';

  return base;
}

char *
expand(char const *word)
{
  char const *pos = word;
  char *start, *end;
  char c = param_scan(pos, &start, &end);
  build_str(NULL, NULL);
  build_str(pos, start);
  while (c) {
    if (c == '!') build_str("<BGPID>", NULL);
    else if (c == '$') {
      int curr_PID = getpid();
      char pid_str[100];
      snprintf(pid_str, sizeof(pid_str), "%d", curr_PID);
      build_str(pid_str, NULL);
    }
    else if (c == '?'){
      char status[100];
      snprintf(status, sizeof(status), "%d", exit_status);
      build_str(status, NULL);
    }
    else if (c == '{') {
      char param_name[end - start - 2];
      strncpy(param_name, start + 2, end - start - 3);
      param_name[end - start - 3] = '\0';
      char *env_value = getenv(param_name);
      if (env_value) {
          build_str(env_value, NULL);
      } 
      // else {
      //     build_str("<Parameter: ", NULL);
      //     build_str(param_name, NULL);
      //     build_str(">", NULL);
      // }
    }
    pos = end;
    c = param_scan(pos, &start, &end);
    build_str(pos, start);
  }
  return build_str(start, NULL);
}