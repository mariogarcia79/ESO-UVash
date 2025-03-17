#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/wait.h>

/*
 *      Definir los diferentes tamanyos para buffers. El buffer de getline() es
 *      de 20 caracteres, un tamanyo mas que razonable para un comando normal.
 *      De esta forma reducimos el numero de realloc() que hay que realizar.
 *      Por el contrario, el buffer de strtok() tiene 15 caracteres porque
 *      se quitan los espacios y es muy complicado que haya un argumento mas
 *      largo que 15 caracteres (a menos que sea el path a algun lado).
 */
#define GETLINE_BUFSIZE 20
#define STRTOK_BUFSIZE 15

/*      Delimitadores a contar para strtok. */
#define STRTOK_DELIMITERS " \t\n"

/*
 *      Funcion p_exit_error
 *      Toma como argumento el mensaje a imprimir.
 *	Escribe en la salida estandar el mensaje de error correctamente 
 *	formateado y sale del programa con codigo de error 1.
 */
void p_exit_error(void)
{
        char error_message[30] = "An error has occurred\n";
	fprintf(stderr, "%s", error_message); 
	exit(0);
}

/*
 *      Funcion get_handle
 *      Toma como argumentos los argumentos de main().
 *      Primero hace parsing a estos argumentos y realiza las comprobaciones
 *      oportunas de ficheros, numero de argumentos... etc.
 *      Si hay un argumento, se abre el fichero con su nombre. Si no hay
 *      argumento, se asigna stdin.
 *      Se devuelve el fichero abierto.
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
 *      Funcion p_prompt
 *      Escribe por pantalla el prompt y devuelve el puntero a la cadena
 *      obtenida de la lectura por teclado. Esta cadena ha sido reservada, asi
 *      que hay que liberarla al terminar su uso.
 *      El archivo se ha de pasar abierto, ya sea stdin o un batch file.
 *      Devuelve una cadena reservada de forma dinamica. Por tanto hay que
 *      liberarla cuando ya no se use. 
 */
char *p_prompt(FILE *handle)
{
        char    *line   = NULL;
        size_t  n       = GETLINE_BUFSIZE;

	line  = (char*)malloc(sizeof(char*)*n);

        /* Mostrar el prompt solo si los comandos se ejecutan desde stdin */
        if (handle == stdin)
                fprintf(stdout, "UVash> ");
        /* Leer la linea y devolverla */
        if (getline(&line, &n, handle) != -1)
                return line;

        /* Salida en caso de error en la lectura de getline() */
        exit(0);
}

/*
 *      Funcion tokenize
 *      Separa la linea por espacios. No importa el numero de espacios que haya
 *      separando un token del siguiente. Tambien elimina las tabulaciones y 
 *      los saltos de linea. Devuelve un array de cadenas reservado de forma
 *      dinamica.
 *      Hay que liberar el array cuando se deje de usar.
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
 *      Manages the redirection of stdout to another output using the dup2
 *      system call and appropiately treats file errors.
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
 *      Funcion exec_command
 *      Primero realiza un fork() y ejecuta el comando en el proceso hijo.
 *      El proceso padre espera a que el proceso hijo termine.
 */
void exec_command(char **args)
{
        pid_t   pid;
        pid_t   c_pid;
        char            *file_out       = NULL;
        
        for (int i = 0; args[i] != NULL; i++) {
                if (strcmp(args[i], ">") == 0) {
                        file_out = args[i+1];
                        if (file_out == NULL)
                                p_exit_error();
                        else if (args[i+2] != NULL)
                                p_exit_error();
                        args[i] = NULL;
                }
        }

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
 *      Funcion command_loop
 *      Es el bucle principal de programa. Primero muestra la prompt por
 *      pantalla (solo en el caso de stdin), espera a la entrada de usuario y
 *      tokeniza la linea. Despues ejecuta los comandos correspondientes.
 *
 *      Usar gotos en este caso esta justificado para centralizar la salida de
 *      la funcion en caso de error, ya que aunque realicemos un exit() y el
 *      sistema operativo se encargue del cleanup, es una buena practica
 *      liberar la memoria y los ficheros antes de salir. (Si un fichero no se
 *      cierra bien se puede corromper...).
 *
 *      Las sentencias if() antes de los free() se debe a que queremos evitar
 *      un double free si no se ha llegado a reservar la memoria. (Si se ha
 *      salido antes de tiempo).
 *
 *      Es buena practica hacer las cosas explicitas!
 */
void command_loop(FILE *handle)
{
        char            *line           = NULL;
        char            **tokens        = NULL;
        unsigned char   exit_loop       = 0;

        do {
                /* Leer la linea y comprobar los errores */
                if ((line = p_prompt(handle)) == NULL)
                        goto exit_error;

                /* Tokenizar la linea */
                tokens = tokenize(line);
                if (tokens == NULL)
                        goto exit_error;
                else if (strcmp(tokens[0], "\0") == 0)
                        continue;

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
                        exec_command(tokens);
                }
        } while (!exit_loop);

        free(line);
        free(tokens);
        return;

/* Salida de errores */
exit_error:
        fclose(handle);
        if (line != NULL)
                free(line);
        if (tokens != NULL)
                free(tokens);
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
