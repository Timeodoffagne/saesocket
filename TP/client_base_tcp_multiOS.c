#include <stdio.h>
#include <stdlib.h> /* pour exit */
#include <string.h> /* pour memset */

//#define WIN32

#ifdef WIN32 /* si vous êtes sous Windows */

#include <winsock2.h>
//#include <ws2tcpip.h>
//#include <ws2spi.h>

#elif defined(linux) /* si vous êtes sous Linux */

#include <sys/types.h>
#include <unistd.h> /* pour read, write, close, sleep */
#include <sys/socket.h>
#include <netinet/in.h> /* pour struct sockaddr_in */
#include <arpa/inet.h>	/* pour htons et inet_aton */

#else /* sinon vous êtes sur une plateforme non supportée */

// #error not defined for this platform

#endif

#define PORT 5000
#define LG_MESSAGE 256

static void init(void)
{
#ifdef WIN32
	WSADATA wsa;
	int err = WSAStartup(MAKEWORD(2, 2), &wsa);
	if (err < 0)
	{
		puts("WSAStartup failed !");
		exit(EXIT_FAILURE);
	}
	printf("Librairie Winsock2 initialisée.\n");
#endif
}

static void end(void)
{
#ifdef WIN32
	WSACleanup();
	printf("Librairie Winsock2 libérée.\n");
#endif
}

static void closeMP(int socket)
{
#ifdef WIN32
	closesocket(socket);
#else
	close(socket);
#endif
}

void inet_pton_MOS(char ip[],struct sockaddr_in *sockaddrDistant,int *ip_length){
#ifdef WIN32
    //sockaddrDistant.sin_addr.s_addr =inet_addr(ip_dest);
    WSAStringToAddressA(ip,AF_INET,NULL,(struct sockaddr*) sockaddrDistant,ip_length);
#else
	inet_pton(AF_INET,ip, sockaddrDistant->sin_addr);
#endif
}


int main(int argc, char *argv[]){
	int descripteurSocket;
	struct sockaddr_in sockaddrDistant;
	int longueurAdresse;

	char buffer[]="Hello server!"; // buffer stockant le message
	int nb; /* nb d’octets écrits et lus */

	char ip_dest[16];
    int ip_length;
	int  port_dest;

	init();

	// Pour pouvoir contacter le serveur, le client doit connaître son adresse IP et le port de comunication
	// Ces 2 informations sont passées sur la ligne de commande
	// Si le serveur et le client tournent sur la même machine alors l'IP locale fonctionne : 127.0.0.1
	// Le port d'écoute du serveur est 5000 dans cet exemple, donc en local utiliser la commande :
	// ./client_base_tcp 127.0.0.1 5000
	if (argc>1) { // si il y a au moins 2 arguments passés en ligne de commande, récupération ip et port
		strncpy(ip_dest,argv[1],16);
		sscanf(argv[2],"%d",&port_dest);
	}else{
		printf("USAGE : %s ip port\n",argv[0]);
		exit(-1);
	}

	// Crée un socket de communication
	descripteurSocket = socket(AF_INET, SOCK_STREAM, 0);
	// Teste la valeur renvoyée par l’appel système socket()
	if(descripteurSocket == -1){
		perror("Erreur en créant le socket"); // Affiche le message d’erreur
		exit(-1); // On sort en indiquant un code erreur
	}
	printf("Socket créé! (%d)\n", descripteurSocket);


	// Remplissage de sockaddrDistant (structure sockaddr_in identifiant la machine distante)
	// Obtient la longueur en octets de la structure sockaddr_in
	longueurAdresse = sizeof(sockaddrDistant);
	// Initialise à 0 la structure sockaddr_in
	// memset sert à faire une copie d'un octet n fois à partir d'une adresse mémoire donnée
	// ici l'octet 0 est recopié longueurAdresse fois à partir de l'adresse &sockaddrDistant
	memset(&sockaddrDistant, 0x00, longueurAdresse);
	// Renseigne la structure sockaddr_in avec les informations du serveur distant
	sockaddrDistant.sin_family = AF_INET;
	// On choisit le numéro de port d’écoute du serveur
	sockaddrDistant.sin_port = htons(port_dest);
	// On choisit l’adresse IPv4 du serveur
    inet_pton_MOS(ip_dest,&sockaddrDistant,&ip_length);


	// Débute la connexion vers le processus serveur distant
	if((connect(descripteurSocket, (struct sockaddr *)&sockaddrDistant,longueurAdresse)) == -1){
		perror("Erreur de connection avec le serveur distant...");
		closeMP(descripteurSocket);
		exit(-2); // On sort en indiquant un code erreur
	}
	printf("Connexion au serveur %s:%d réussie!\n",ip_dest,port_dest);

 	// Envoi du message
	switch(nb = send(descripteurSocket, buffer, strlen(buffer),0)){
		case -1 : /* une erreur ! */
     			perror("Erreur en écriture...");
		     	closeMP(descripteurSocket);
		     	exit(-3);
		case 0 : /* le socket est fermée */
			fprintf(stderr, "Le socket a été fermée par le serveur !\n\n");
			return 0;
		default: /* envoi de n octets */
			printf("Message %s envoyé! (%d octets)\n\n", buffer, nb);
	}

	// On ferme la ressource avant de quitter
	closeMP(descripteurSocket);

	end();

	return 0;
}
