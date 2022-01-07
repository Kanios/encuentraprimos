#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/types.h>
#include <sys/wait.h>

#define LONGITUD_MSG 100           // Payload del mensaje
#define LONGITUD_MSG_ERR 200       // Mensajes de error por pantalla

// Códigos de exit por error
#define ERR_ENTRADA_ERRONEA 2
#define ERR_SEND 3
#define ERR_RECV 4
#define ERR_FSAL 5

#define NOMBRE_FICH "primos.txt"
#define NOMBRE_FICH_CUENTA "cuentaprimos.txt"
#define CADA_CUANTOS_ESCRIBO 5

// rango de búsqueda, desde BASE a BASE+RANGO
#define BASE 800000000
#define RANGO 2000

// Intervalo del temporizador para RAIZ
#define INTERVALO_TIMER 5

// Códigos de mensaje para el campo mesg_type del tipo T_MESG_BUFFER
#define COD_ESTOY_AQUI 5           // Un calculador indica al SERVER que está preparado
#define COD_LIMITES 4              // Mensaje del SERVER al calculador indicando los límites de operación
#define COD_RESULTADOS 6           // Localizado un primo
#define COD_FIN 7                  // Final del procesamiento de un calculador

// Mensaje que se intercambia

typedef struct {
    long mesg_type;
    char mesg_text[LONGITUD_MSG];
} T_MESG_BUFFER;

int Comprobarsiesprimo(long int numero);
void Informar(char *texto, int verboso);
void Imprimirjerarquiaproc(int pidraiz,int pidservidor, int *pidhijos, int numhijos);
int ContarLineas();
static void alarmHandler(int signo);
//void primoHandler(int msgid, long int numero, int nrango, T_MESG_BUFFER message);

//void functest();

int cuentasegs;                   // Variable para el cómputo del tiempo total

int main(int argc, char* argv[])
{	
	int i,j;
	long int numero;
	long int numprimrec;
    long int nbase;
    int nrango;
    int nfin;
    time_t tstart,tend; 
	
	key_t key;
    int msgid;    
    int pid, pidservidor, pidraiz, parentpid, mypid, pidcalc;
    int *pidhijos;
    int intervalo,inicuenta;
    int verbosity;
    T_MESG_BUFFER message;
    char info[LONGITUD_MSG_ERR];
    FILE *fsal, *fc;
    int numhijos;

    // Control de entrada, después del nombre del script debe figurar el número de hijos y el parámetro verbosity
    
    //numhijos = (int)argv[0];
    //verbosity = (int)argv[1];
    
    numhijos = (int)argv[0] - '0';
    verbosity = (int)argv[1] - '0';

    numhijos = 3;     // SOLO para el esqueleto, en el proceso  definitivo vendrá por la entrada
    verbosity = 1;

    pid=fork();       // Creación del SERVER
    
    if (pid == 0)     // Rama del hijo de RAIZ (SERVER)
    {
		pid = getpid();
		pidservidor = pid;
		mypid = pidservidor;	   
		
		// Petición de clave para crear la cola
		if ( ( key = ftok( "/tmp", 'C' ) ) == -1 ) {
		  perror( "Fallo al pedir ftok" );
		  exit( 1 );
		}
		
		printf( "Server: System V IPC key = %u\n", key );

        // Creación de la cola de mensajería
		if ( ( msgid = msgget( key, IPC_CREAT | 0666 ) ) == -1 ) {
		  perror( "Fallo al crear la cola de mensajes" );
		  exit( 2 );
		}
		printf("Server: Message queue id = %u\n", msgid );

        i = 0;
        // Creación de los procesos CALCuladores
		while(i < numhijos) {
		 if (pid > 0) { // Solo SERVER creará hijos
			 pid=fork(); 
			 if (pid == 0) 
			   {   // Rama hijo
				parentpid = getppid();
				mypid = getpid();
			   } 
			 /*else{
				 kill(pid, SIGSTOP);
			 }*/
		 }
		 i++;  // Número de hijos creados
		}

        // AQUI VA LA LOGICA DE NEGOCIO DE CADA CALCulador. 
		if (mypid != pidservidor)
		{
			//printf("Soy el hijo %d\n", mypid);
			
			message.mesg_type = COD_ESTOY_AQUI;
			sprintf(message.mesg_text, "%d", mypid);
			msgsnd( msgid, &message, sizeof(message), IPC_NOWAIT);
			
			printf("Pauso CALCulador %d\n", mypid);
			kill(mypid, SIGSTOP); //Intuyo que si un proceso se SIGSTOP a si mismo su padre no puede SIGCONT
			printf("CALCulador %d despausado con exito\n", mypid); //SERVER NO llega a despausar a los hijos

			// Un montón de código por escribir
			
			//signal(SIGUSR1, functest);
			//signal(SIGALRM, primoHandler(msgid, numero, nrango, message));
			//sigwait(SIGUSR1, dump);
			
			msgrcv(msgid, &message, sizeof(message), 0, 0);
			sscanf(message.mesg_text, "%ld %d", &numero, &nrango); 
			printf("\nEl CALCulador %d empieza a calcular desde %ld con un rango de %d\n", mypid, numero, nrango);
			
			//Bucle de busqueda de primos
			//SIN TERMINAR
			message.mesg_type = COD_RESULTADOS;
			for(i = 0; i < nrango; i++){
				//Si numero es primo lo mando por la cola de mensajeria
				if(Comprobarsiesprimo(numero)){
					sprintf(message.mesg_text, "%d %ld", mypid, numero); //mando la pid de hijo que encuentra el primo y el primo encontrado
					msgsnd( msgid, &message, sizeof(message), IPC_NOWAIT);
					if(verbosity > 0) printf("El hijo %d a encontrado el primo %ld\n", mypid, numero);
				}
				numero++;
			}
			printf("Soy el hijo %d y he terminado de calcular y me muero\n", mypid);
			
			//sleep(60); // Esto es solo para que el esqueleto no muera de inmediato, quitar en el definitivo

			exit(0);
		}
		
		// SERVER
		
		else
		{ 
		  // Pide memoria dinámica para crear la lista de pids de los hijos CALCuladores
		  pidhijos = malloc(sizeof(int) * numhijos); //FALTA poner free(pidhijos) en algun lado
		  sleep(2); //Testing
		  // Recepción de los mensajes COD_ESTOY_AQUI de los hijos
		  for (j=0; j <numhijos; j++)
		  {
			  msgrcv(msgid, &message, sizeof(message), 0, 0);
			  sscanf(message.mesg_text,"%d",&pid); // Tendrás que guardar esa pid 
			  printf("\nMe ha enviado un mensaje el hijo %d\n", pid);
			  pidhijos[j] = pid; //guardo esa pid
			  printf("Guardado pid hijo: %d\n", pidhijos[j]); //Comentado porque el malloc() funciona correctamente
		  }
		  
		  //Imprimo la jerarquia de procesos 
		  Imprimirjerarquiaproc(getppid(), pidservidor, pidhijos, numhijos);
		  
		  // Envio del numero por el que empieza cada hijo y cuantos valores debe recorrer
		  message.mesg_type = COD_LIMITES;
		  for (j = 0; j < numhijos; j++){
			sprintf(message.mesg_text, "%ld %d", (long)(j * RANGO / numhijos) + BASE, RANGO / numhijos); //Esto SI funciona, lo he probado
			msgsnd( msgid, &message, sizeof(message), IPC_NOWAIT);
			printf("Envio numero inicial %d y el rango %d\n", (j * RANGO / numhijos) + BASE, RANGO / numhijos);
			
			printf("Despauso CALCuladores...\n");
			kill(pidhijos[j], SIGCONT); //ESTO NO FUNCIONA, NO REVIVE A LOS CALCuladores
		  }
		  
			//sleep(60); // Esto es solo para que el esqueleto no muera de inmediato, quitar en el definitivo

		  
		  // Mucho código con la lógica de negocio de SERVER
		  
		  // Borrar la cola de mensajería, muy importante. No olvides cerrar los ficheros
		  msgctl(msgid,IPC_RMID,NULL);
		  free(pidhijos);
		  
	   }
    }

    // Rama de RAIZ, proceso primigenio
    
    else
    {
	  pidraiz=getpid();
      alarm(INTERVALO_TIMER);
      signal(SIGALRM, alarmHandler);
      for (;;)    // Solo para el esqueleto
		sleep(1); // Solo para el esqueleto
	  // Espera del final de SERVER
      // ...
      // El final de todo
    }
}

// Manejador de la alarma en el RAIZ
static void alarmHandler(int signo)
{
//...
    printf("SOLO PARA EL ESQUELETO... Han pasado 5 segundos\n");
    alarm(INTERVALO_TIMER);

}

//Funcion que
/*void primoHandler(int msgid, long int numero, int nrango, T_MESG_BUFFER message, int mypid){
	msgrcv(msgid, &message, sizeof(message), 0, 0);
	sscanf(message.mesg_text, "%d %d", &numero, &nrango);
	printf("\nEl CALCulador %d empieza a calcular desde %d con un rango de %d\n", mypid, numero, nrango);
			
	//Bucle de busqueda de primos
	message.mesg_type = COD_RESULTADOS;
	for(i = 0; i < (RANGO / numhijos); i++){
		//Si numero es primo lo mando por la cola de mensajeria
		if(Comprobarsiesprimo(numero)){
			sprintf(message.mesg_text, "%d %d", mypid, numero); //mando la pid de hijo que encuentra el primo y el primo encontrado
			msgsnd( msgid, &message, sizeof(message), IPC_NOWAIT);
			if(verbosity > 0) printf("El hijo %d a encontrado el primo %d\n", mypid, numero);
		}
		numero++;
	}
	printf("Soy el hijo %d y he terminado de calcular y me muero\n", mypid)
}*/

//Funcion que imprime las pid de todos los procesos
void Imprimirjerarquiaproc(int pidraiz, int pidservidor, int *pidhijos, int numhijos){
	printf("\nJerarquia de procesos:");
	printf("\nRAIZ		SERV		CALC\n");
	printf("%d%16d%16d\n", pidraiz, pidservidor, pidhijos[0]);
	for(int i = 1; i < numhijos; i++) printf("				%d\n",pidhijos[i]);
	printf("\n");
}

//Funcion que comprueba si un numero es primo 
//devuelve 1 si es primo
int Comprobarsiesprimo(long int numero) {
  if(numero < 2) return 0; // Por convenio 0 y 1 no son primos ni compuestos
  else
	for(int x = 2; x <= (numero / 2); x++)
		if(numero % x == 0) return 0;
  return 1;
}

/*void functest(){
	printf("esto se despausa :D\n");
}*/
