#include <stdio.h>
#include <stdlib.h> /* pour exit */
#include <unistd.h> /* pour read, write, close, sleep */
#include <sys/types.h>
#include <sys/socket.h>
#include <string.h> /* pour memset */
#include <netinet/in.h> /* pour struct sockaddr_in */
#include <arpa/inet.h> /* pour htons et inet_aton */

int main(int argc, char *argv[]){
	int descripteurSocket;
	struct sockaddr_in sockaddrDistant;
	socklen_t longueurAdresse;

	char buffer[]="Hello server!"; // buffer stockant le message
	int nb; /* nombre d'octets écrits et lus */

	char ip_dest[16];
	int  port_dest;

	if (argc>1) {
		strncpy(ip_dest,argv[1],16);
		sscanf(argv[2],"%d",&port_dest);
	}else{
		printf("USAGE : %s ip port\n",argv[0]);
		exit(-1);
	}

	/* Création du socket */
	descripteurSocket = socket(AF_INET, SOCK_STREAM, 0);
	if(descripteurSocket < 0){
		perror("Erreur lors de la création de la socket");
		exit(-1);
	}
	printf("Socket créée (%d)\n", descripteurSocket);

	/* Remplissage de sockaddrDistant */
	longueurAdresse = sizeof(sockaddrDistant);
	memset(&sockaddrDistant, 0x00, longueurAdresse);
	sockaddrDistant.sin_family = AF_INET;
	sockaddrDistant.sin_port = htons(port_dest);
	inet_aton(ip_dest, &sockaddrDistant.sin_addr);

	/* Connexion */
	if((connect(descripteurSocket, (struct sockaddr *)&sockaddrDistant,longueurAdresse)) == -1){
		perror("Erreur de connexion avec le serveur distant");
		close(descripteurSocket);
		exit(-2);
	}
	printf("Connexion au serveur %s:%d réussie.\n",ip_dest,port_dest);

 	/* Envoi */
	switch(nb = send(descripteurSocket, buffer, strlen(buffer)+1,0)){
		case -1 :
     			perror("Erreur lors de l'écriture");
		     	close(descripteurSocket);
		     	exit(-3);
		case 0 :
			fprintf(stderr, "La socket a été fermée par le serveur.\n\n");
			return 0;
		default:
			printf("Message '%s' envoyé (%d octets)\n\n", buffer, nb);
	}

	close(descripteurSocket);

	return 0;
}
