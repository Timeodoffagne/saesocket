#include <stdio.h>
#include <stdlib.h> /* pour exit */
#include <unistd.h> /* pour read, write, close, sleep */
#include <sys/types.h>
#include <sys/socket.h>
#include <string.h> /* pour memset */
#include <netinet/in.h> /* pour struct sockaddr_in */
#include <arpa/inet.h> /* pour htons et inet_aton */

#define PORT 5000 //(ports >= 5000 réservés pour usage explicite)

#define LG_MESSAGE 256

int main(int argc, char *argv[]){
	int socketEcoute;

	struct sockaddr_in pointDeRencontreLocal;
	socklen_t longueurAdresse;

	int socketDialogue;
	struct sockaddr_in pointDeRencontreDistant;
	char messageRecu[LG_MESSAGE]; /* le message de la couche application */
	int lus; /* nombre d'octets lus */

	/* Créer un socket de communication */
	socketEcoute = socket(AF_INET, SOCK_STREAM, 0); 
	if(socketEcoute < 0){
		perror("socket"); /* message d'erreur système */
		exit(-1);
	}
	printf("Socket créée (%d)\n", socketEcoute); /* préparation de l'adresse locale */

	/* Remplissage de la structure d'attachement locale */
	longueurAdresse = sizeof(pointDeRencontreLocal);
	memset(&pointDeRencontreLocal, 0x00, longueurAdresse);
	pointDeRencontreLocal.sin_family = PF_INET;
	pointDeRencontreLocal.sin_addr.s_addr = htonl(INADDR_ANY);
	pointDeRencontreLocal.sin_port = htons(PORT);
	
	/* Attachement local de la socket */
	if((bind(socketEcoute, (struct sockaddr *)&pointDeRencontreLocal, longueurAdresse)) < 0) {
		perror("bind");
		exit(-2); 
	}
	printf("Socket attachée.\n");

	/* Mise en écoute passive */
	if(listen(socketEcoute, 5) < 0){
   		perror("listen");
   		exit(-3);
	}
	printf("Socket mise en écoute passive.\n");
	
	while(1){
		memset(messageRecu, 'a', LG_MESSAGE*sizeof(char));
		printf("Attente d'une demande de connexion (Ctrl-C pour quitter)\n\n");
		
		socketDialogue = accept(socketEcoute, (struct sockaddr *)&pointDeRencontreDistant, &longueurAdresse);
		if (socketDialogue < 0) {
   			perror("accept");
			close(socketDialogue);
   			close(socketEcoute);
   			exit(-4);
		}
		
		/* Réception des données client (appel bloquant) */
		lus = recv(socketDialogue, messageRecu, LG_MESSAGE*sizeof(char), 0);
		switch(lus) {
			case -1 : 
				  perror("read"); 
				  close(socketDialogue); 
				  exit(-5);
			case 0  : 
				  fprintf(stderr, "La socket a été fermée par le client.\n\n");
   				  close(socketDialogue);
   				  return 0;
			default:  
				  printf("Message reçu : %s (%d octets)\n\n", messageRecu, lus);
		}

	}
	close(socketEcoute);
	return 0; 
}
