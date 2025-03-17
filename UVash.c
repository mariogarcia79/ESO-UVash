#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/wait.h>

/*
 *      Define the different buffer sizes.
 *      This is in order to reduce the number of realloc() that are needed.
 */
#define GETLINE_BUFSIZE 20
#define STRTOK_BUFSIZE 15

#define STRTOK_DELIMITERS " \t\n"

/*
 *      Function p_exit_error
 *
 *      Arguments:      None.
 *      Description:    It prints in standard error the correctly formatted
 *                      error, and then exits the program with return code 1.
 */
void p_exit_error(void)
{
        char error_message[30] = "An error has occurred\n";
	fprintf(stderr, "%s", error_message); 
	exit(0);
}

/*
 *      Function get_handle
 *
 *      Arguments:      argc: argument count from main().
 *                      argv: argument list from main().
 *      Description:    First the function parses the arguments and checks for
 *                      file errors, and number of arguments.
 *                      If a file is taken as an argument, it is opened. If
 *                      there are no arguments, then stdin is used as the file.
 *      Returns:        Opened file.
 */
FILE *get_handle(int  argc,
                 char ** argv)
{
        FILE *handle = NULL;
        
        switch (argc) {
        case 1: handle = stdin; break;
        case 2: 
                if ((handle = fopen(argv[1], "r")) == NULL) {
                        fprintf(stderr, "An error has occurred\n");
                        exit(1);
                }
                break;
        default:
                fprintf(stderr, "An error has occurred\n");
                exit(1);
        }

        return handle;
}

/*
 *      Function p_prompt
 *      
 *      Arguments:      handle: an opened file for printing.
 *      Description:    If the handle is stdin, then a prompt is printed.
 *                      However, if the handle is another file (a batch file),
 *                      then the prompt is not printed.
 *                      Then the program reads from the file.
 *      Returns:        The line read dynamically allocated.
 *                      MUST BE FREED.
 */
char *p_prompt(FILE *handle)
{
        char    *line   = NULL;
        size_t  n       = GETLINE_BUFSIZE;

	line  = (char*)malloc(sizeof(char*)*n);

        if (handle == stdin)
                fprintf(stdout, "UVash> ");

        if (getline(&line, &n, handle) != -1)
                return line;

        exit(0);
}

/*
 *      Function tokenize
 *
 *      Arguments:      line: Line to tokenize.
 *      Description:    Separates the line into tokens, removing spaces,
 *                      tabulations and line feeds.
 *      Returns:        A token array dynamically allocated.
 *                      MUST BE FREED.
 */
char **tokenize(char *line)
{
        char     **tokens    = NULL; 
        int      pos         = 0;
        int      bufsize     = STRTOK_BUFSIZE;

        tokens = (char **)malloc(sizeof(char*) * bufsize);
        if (tokens == NULL)
                return NULL;

        while (*line == '\n' ||
               *line == '\b' ||
               *line == ' '   )
                line++;

        if (*line == '\0') {
                tokens[0] = "\0";
                return tokens;
        }

        tokens[pos] = strtok(line, STRTOK_DELIMITERS);

        while (tokens[pos] != NULL) {
                pos++;
                if (pos >= bufsize) {
                        bufsize += GETLINE_BUFSIZE;
                        tokens = realloc(tokens, sizeof(char*) * bufsize);
                }
                tokens[pos] = strtok(NULL, STRTOK_DELIMITERS);
        }

        return tokens;
}

/*
 *      Function redir
 *      
 *      Arguments:      file_out: the file to redirect output.
 *      Description:    Manages the redirection of stdout to another output 
 *                      using the dup2 system call and appropiately treats file 
 *                      errors.
 *      Returns:        None.
 */
void redir(char *file_out)
{
        int fout;
        FILE *f = fopen(file_out, "w");
        if (f == NULL)
                p_exit_error();

        if ((fout = fileno(f)) < 0) {
                fclose(f);
                p_exit_error();
        }

        dup2(fout, 1);
        dup2(fout, 2);

        fclose(f);
}

/*
 *      Function exec_command
 *
 *      Arguments:      args: the command arguments.
 *      Description:    Manages redirections and executes the command specified
 *                      in the arguments (args[0]). Also passes the arguments
 *                      for the specific command.
 *      Returns:        None.
 */
void exec_command(char **args) 
{
        pid_t   pid;
        pid_t   c_pid;
        char    *file_out = NULL;
        
        /* Parse the redirections and manage files */
        for (int i = 0; args[i] != NULL; i++) {
                if (strcmp(args[i], ">") == 0) {
                        file_out = args[i+1];
                        if (file_out == NULL)
                                p_exit_error();
                        else if (args[i+2] != NULL &&
                                 strcmp(args[i+2], "&"))
                                p_exit_error();
                        args[i] = NULL;
                }
        }

        /* Execute the command */
        pid = fork();
        if (pid < 0) {
                p_exit_error();
        } else if (pid == 0) {
                if (file_out != NULL)
                        redir(file_out);
                if (execvp(args[0], args) < 0)
                        p_exit_error();
                p_exit_error();
        } else {
                c_pid = pid;
                waitpid(c_pid, NULL, 0);
        }
}

/*
 *      Function exec_from_prompt
 *
 *      Arguments:      tokens: the tokens from the command line.
 *      Description:    Separates the different commands from the prompt and
 *                      then calls exec_command() for executing them.
 *      Returns:        None.
 */
void exec_from_prompt(char **tokens)
{
        if (strcmp(tokens[0], "&") == 0)
                p_exit_error();

        exec_command(tokens);
        while (*tokens != NULL) {
                if (strcmp(*tokens, "&") == 0) {
                        /* Jump over the & token */
                        tokens++;
                        exec_command(tokens);
                }
                tokens++;
        }
}

/*
 *      Function command_loop
 *
 *      Arguments:      handle: the input file handle.
 *      Description:    Main loop of the program. Firstly shows a prompt in
 *                      stdout (Only when handle is stdin), waits for user
 *                      input and then parses the line.
 *                      After this, it executes the corresponding commands.
 *
 *      Notes:          The use of gotos in this case is justified because
 *                      it centralizes error managing and cleaning files and
 *                      pointers. That way, code is not repeated and it is much
 *                      more clear.
 *
 *                      if() sentences just before free() calls are in place in
 *                      order to avoid double-freeing resources. This could be
 *                      the case if the function returned before expected.
 *      Returns:        None.
 */
void command_loop(FILE *handle)
{
        char            *line           = NULL;
        char            **tokens        = NULL;
        unsigned char   exit_loop       = 0;

        do {
                /* Read first line */
                if ((line = p_prompt(handle)) == NULL)
                        goto exit_error;

                /* Tokenize the line */
                tokens = tokenize(line);
                if (tokens == NULL)
                        goto exit_error;
                else if (strcmp(tokens[0], "\0") == 0)
                        continue;
                /* Check for special tokens */
                if (strcmp(tokens[0], ">") == 0) {
                        goto exit_error;
                } else if (strcmp(tokens[0], "exit") == 0) {
                        if (tokens[1] != NULL)
                                goto exit_error;
                        exit_loop = 1;
                } else if (strcmp(tokens[0], "cd") == 0) {
                        if (tokens[1] == NULL ||
                            tokens[2] != NULL)
                                goto exit_error;
                        else    chdir(tokens[1]);
                } else {
                        /* Execute the commands from command line */
                        exec_from_prompt(tokens);
                }
        } while (!exit_loop);

        /* Free resources */
        free(line);
        free(tokens);
        return;

exit_error:
        /* Clean resources */
        fclose(handle);
        if (line != NULL)
                free(line);
        if (tokens != NULL)
                free(tokens);
        /* Exit */
        p_exit_error();
}


int main (int  argc,
          char **argv)
{
        FILE *handle = NULL;
        /* Abrir el fichero correspondiente */
        handle = get_handle(argc, argv);

        /* Ejecutar el bucle principal */
        command_loop(handle);

        /* Liberar recursos y salir del programa */
        fclose(handle);
        return 0;
}
