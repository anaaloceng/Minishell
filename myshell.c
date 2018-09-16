//Incluir libreri­as
#include <stdio.h>
#include "parser.h"
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <dirent.h>
#include <libgen.h>
#include <signal.h>

void unMandato (tline * line);
void redEntrada (tline * line);
void redSalida (tline * line);
void redError(tline * line);
void variosMandatos(tline * line);
void mandatoCd(tline * line);
void comprobarSenales(tline *line);

int main(void) {
	
	//VARIABLES
	char buf[1024];
	tline * line;
	int i,j;

	//Descriptores entrada y salida estandar
	int in = dup(0);	
	int out = dup(1);
	int error = dup(2);

	//Variable con el directorio actual
	char dActual[1024];
	getcwd(dActual, 1024);

	//Ignorar señales de salida y pausa
	signal(SIGINT, SIG_IGN);
	signal (SIGQUIT, SIG_IGN);

	//BUCLE MINISHELL
	printf("%s msh> ", dActual);	
	while (fgets(buf, 1024, stdin)) {

		line = tokenize(buf);
		//Si no se introduce nada, continua y vuelve a mostrar el prompt
		if (line==NULL) {
			continue;
		}
		//Si se ha introducido redireccion de entrada, se llama a la funcion redEntrada
		if (line->redirect_input != NULL) {
			redEntrada(line);
		}
		//Si se ha introducido redireccion de salida, se llama a la funcion redSalida
		if (line->redirect_output != NULL) {
			redSalida(line);
		}
		//Si se ha introducido redireccion de salida de error, se llama a la funcion redError
		if (line->redirect_error != NULL) {
			redError(line);
		}

		//Si solo hay un mandato, comprobar si es cd o exit. Si no, llama a la funcion "unMandato"
		if (line->ncommands == 1) {
			if (strcmp(line->commands[0].argv[0], "cd") == 0) {
				mandatoCd(line);
			}
			else if (strcmp(line->commands[0].argv[0], "exit") == 0) {
				exit(0);
			}
			else {
				unMandato(line);	
			}
				
		}
		
		//Si hay varios mandatos, llama a la funcion "variosMandatos"
		else if (line->ncommands>1) {
			variosMandatos(line);
		}
		
		//Restaurar E/S estandar si se ha modificado
		if (line->redirect_input != NULL) {
			dup2(in, 0);
		}
		if (line->redirect_output != NULL) {
			dup2(out, 1);
		}
		if (line->redirect_error != NULL) {
			dup2(error, 2);
		}
	
		//Actualizar directorio actual antes de mostrarlo en el prompt
		getcwd(dActual, 1024);

		//Volver a mostrar el prompt
		prompt: printf("%s msh> ", dActual);
	}
	return 0;
}

//Ejecutar un solo comando con 0-varios argumentos
void unMandato (tline * line) {

	pid_t pid;
	int status;

	
	//Crear hijo
	pid = fork();

	//Error
	if (pid < 0) { 
		fprintf(stderr, "Falló el fork(). %s\n", strerror(errno));
		exit(1);
	}
	//Hijo
	else if (pid == 0) { 
		//Comprobar bg para señales
		comprobarSenales(line);
		//Ejecutar el mandato con sus argumento
		execvp(line->commands[0].argv[0], line->commands[0].argv);
		//Error en el execvp
		fprintf(stderr, "%s: No se encuentra el mandato.\n", line->commands[0].argv[0]);
		exit(1);
	}
	//Padre
	else { 		
		//Esperar a que el hijo termine y comprobar el estado/exit 
    	wait (&status);
		if (WIFEXITED(status) != 0)
			if (WEXITSTATUS(status) != 0)
				printf("El comando no se ejecutó correctamente\n");
	}

}

//Redireccionar entrada
void redEntrada (tline * line) {
	//Comprobar que existe el archivo y se puede leer
	if (access(line->redirect_input, R_OK)!=-1) {
		//Abrir archivo y obtener descriptor
		int fdi = open(line->redirect_input, O_CREAT|O_RDWR, 0666);
		dup2(fdi, 0);
	}
	//Si falla, escribir por la salida de error que no existe
	else {
		fprintf(stderr, "%s: Error. %s.\n", line->redirect_input, strerror(errno));
	}
}

//Redireccionar salida
void redSalida (tline * line) {
	//Abrir archivo y obtener descriptor (si no existe, lo crea)
	int fdo = open(line->redirect_output, O_CREAT|O_RDWR, 0666);
	dup2(fdo,1);
}

//Redireccionar salida error
void redError(tline * line) {
	//Abrir archivo y obtener descriptor (si no existe, lo crea)
	int fde = open(line->redirect_error, O_CREAT | O_RDWR, 0666);
	dup2(fde, 2);
}

//Función para ejecutar VARIOS mandatos
void variosMandatos(tline * line) {

	//Guardar número de mandatos
	int numMandatos = line->ncommands;
	//Array de pipes, cada pipe es un vector de 2 enteros (int pipe[2])
	int pipes[numMandatos-1][2];	
	
	//Crear primer pipe y añadirlo al array
	pipe(pipes[0]);

	//Primer hijo
	int pid=fork();
	if (pid==0) {
		//Comprobar bg para señales
		comprobarSenales(line);
		//Cerrar salida (sólo escribe)
		close(pipes[0][0]);
		//Redireccionar salida a la entrada del pipe
		dup2(pipes[0][1], 1);
		//Sustituir el código
		execvp(line->commands[0].argv[0], line->commands[0].argv);
		//Si ocurre algún error
		fprintf(stderr, "%s: No se encuentra el mandato.\n", line->commands[0].argv[0]);
		exit(1);		
	}

	//Si hay más de dos mandatos
	if (numMandatos > 2) {
		//Crear pipes para los hijos
		for (int i=1; i<(numMandatos-1); i++) {
			pipe(pipes[i]);
		}

		//Hijos intermedios 
		for (int i=1; i<(numMandatos-1); i++) {	

			//Crea un hijo
			pid=fork();
			if(pid==0) {
				//Comprobar bg para señales
				comprobarSenales(line);
				//Cerrar entrada del pipe (i-1) (solo va a leer)
				close(pipes[i-1][1]);
				//Cierra la salida del pipe i (solo va a escribir)
				close(pipes[i][0]);
				//Cierra el resto de tuberías que no utiliza
				for(int j=0; j<(numMandatos-1); j++) {
					if(j!=i && j!=(i-1)) {
						close(pipes[j][1]);
						close(pipes[j][0]);
					}
				}
				//Redirecciona su entrada a la salida del pipe (i-1)
				dup2(pipes[i-1][0], 0);
				//Redirecciona su salida a la entrada del pipe i
				dup2(pipes[i][1], 1);
				//Sustituir el código
				execvp(line->commands[i].argv[0], line->commands[i].argv);
				//Si ocurre algún error
				fprintf(stderr, "%s: No se encuentra el mandato.\n", line->commands[i].argv[0]);
				exit(1);
			}
		}
	}
	
	//Ultimo hijo
	pid=fork();
	if(pid==0) {
		//Comprobar bg para señales
		comprobarSenales(line);
		//Cerrar entrada del ultimo pipe (nMandatos-1), solo va a leer
		close(pipes[numMandatos-2][1]);
		//Cerrar el resto de pipes (no los usa)
		for (int j=0; j<(numMandatos-2); j++) {
			close(pipes[j][1]);
			close(pipes[j][0]);
		}
		//Redireccionar entrada a salida del ultimo pipe
		dup2(pipes[numMandatos-2][0], 0);
		//Sustituir codigo	
		execvp(line->commands[numMandatos-1].argv[0], line->commands[numMandatos-1].argv);
		//Si ocurre algún error
		fprintf(stderr, "%s: No se encuentra el mandato.\n", line->commands[numMandatos-1].argv[0]);
		exit(1);
	}
	
	//Padre
	//Cerrarlo todo
	for (int j=0; j<(numMandatos-1); j++) {
		close(pipes[j][1]);
		close(pipes[j][0]);
	}
	//Esperar a que terminen todos los hijos
	for(int i=0; i<numMandatos; i++) {
		wait(NULL);
	}
		
}

//Función para ejecutar el mandato "cd"
void mandatoCd(tline * line) {

	//Si sólo hay un argumento ('cd'), cambia a $HOME 
	if (line->commands[0].argc == 1) {
		chdir(getenv("HOME"));
		printf("El directorio se ha cambiado a HOME: %s\n", getenv("HOME"));
	}
	//Si hay dos argumentos ('cd' y 'directorio'), se cambia al directorio indicado
	else if (line->commands[0].argc == 2) {

		char directorio[1024];
		strcpy(directorio, line->commands[0].argv[1]);
		
		//Comprobar si el directorio existe primero
		if (opendir(directorio)) {
			//El directorio existe, cambiar al mismo
			chdir(directorio);
			getcwd(directorio, 1024);
			printf("Se ha cambiado al directorio %s\n", directorio);
		}
		//Si falla, escribir por la salida de error que no es un directorio
		else if (ENOENT == errno) {
			fprintf(stderr, "Error: %s no es un directorio.\n", directorio);
		}
		
	}

	//Si hay más de 2 directorios, muestra un error
	else {
		fprintf(stderr, "Uso: cd [argumento1].\n");
	}
}

void comprobarSenales (tline *line) {
	//Si el proceso va a ejecutarse en background, se ignoran las señales
		if (line->background==1) {
			signal(SIGINT, SIG_IGN);
			signal (SIGQUIT, SIG_IGN);
		}
		//Si no, realizan su acción por defecto
		else {
			signal(SIGINT, SIG_DFL);
			signal (SIGQUIT, SIG_DFL);
		}
}

