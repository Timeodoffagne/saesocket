#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <string.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define PORT 5000
#define LG_MESSAGE 256
#define LISTE_MOTS "mots.txt"

void creationSocket(int *socketEcoute,
                    socklen_t *longueurAdresse,
                    struct sockaddr_in *pointDeRencontreLocal)
{
    // Création du socket
    *socketEcoute = socket(AF_INET, SOCK_STREAM, 0);
    if (*socketEcoute < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }
    printf("Socket créée avec succès ! (%d)\n", *socketEcoute);

    // Préparation adresse locale
    *longueurAdresse = sizeof(struct sockaddr_in);
    memset(pointDeRencontreLocal, 0, *longueurAdresse);

    pointDeRencontreLocal->sin_family = AF_INET;
    pointDeRencontreLocal->sin_addr.s_addr = htonl(INADDR_ANY);
    pointDeRencontreLocal->sin_port = htons(PORT);

    // bind
    if (bind(*socketEcoute,
             (struct sockaddr *)pointDeRencontreLocal,
             *longueurAdresse) < 0) {
        perror("bind");
        exit(EXIT_FAILURE);
    }
    printf("Socket attachée avec succès !\n");

    // listen
    if (listen(*socketEcoute, 5) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }
    printf("Socket placée en écoute passive...\n");
}

void recevoirMessage(int socketDialogue)
{
	char messageRecu[LG_MESSAGE];
	int lus;

	memset(messageRecu, 0, LG_MESSAGE);

	lus = recv(socketDialogue, messageRecu, LG_MESSAGE, 0);

	if (lus <= 0) {
		printf("Client déconnecté.\n");
		close(socketDialogue);
		return;
	}

	printf("Message reçu : %s (%d octets)\n", messageRecu, lus);
}

void envoyerMessage(int socketDialogue, const char *message)
{
	int nb = send(socketDialogue, message, strlen(message), 0);
	if (nb < 0) {
		perror("send");
		close(socketDialogue);
		exit(EXIT_FAILURE);
	}
}

void boucleServeur(int socketEcoute)
{
    int socketDialogue;
    socklen_t longueurAdresse;
    struct sockaddr_in pointDeRencontreDistant;
    char messageRecu[LG_MESSAGE];
    int lus;

    longueurAdresse = sizeof(pointDeRencontreDistant);

    while (1) {
        printf("Attente d’une demande de connexion...\n");

        socketDialogue = accept(socketEcoute,
                                (struct sockaddr *)&pointDeRencontreDistant,
                                &longueurAdresse);

        if (socketDialogue < 0) {
            perror("accept");
            close(socketEcoute);
            exit(EXIT_FAILURE);
        }

        memset(messageRecu, 0, LG_MESSAGE);

        lus = recv(socketDialogue, messageRecu, LG_MESSAGE, 0);

        if (lus <= 0) {
            printf("Client déconnecté.\n");
            close(socketDialogue);
            continue;
        }

        printf("Message reçu : %s (%d octets)\n", messageRecu, lus);

		/* recuperation de l'addresse ip du client */
		char *adresseIP = inet_ntoa(pointDeRencontreDistant.sin_addr);
		printf("Adresse IP du client : %s\n", adresseIP);

		const char *messageAEnvoyer = "Message bien reçu ! start x";
		envoyerMessage(socketDialogue, messageAEnvoyer);
		
		close(socketDialogue);
    }
}

char* creationMot(){
	FILE *f = fopen(LISTE_MOTS, "r");
	if (f == NULL) {
		perror("Erreur lors de l'ouverture du fichier des mots");
		exit(EXIT_FAILURE);
	}

	static char mot[LG_MESSAGE];
	int nombreMots = 0;
	char ligne[LG_MESSAGE];

	while (fgets(ligne, sizeof(ligne), f) != NULL) {
		nombreMots++;
	}

	int motAleatoire = rand() % nombreMots;

	rewind(f);

	for (int i = 0; i <= motAleatoire; i++) {
		fgets(mot, sizeof(mot), f);
	}

	mot[strcspn(mot, "\n")] = 0;

	fclose(f);
	return mot;
}

int main() {
    int socketEcoute;
    socklen_t longueurAdresse;
    struct sockaddr_in pointDeRencontreLocal;

    // Création et initialisation de la socket
    creationSocket(&socketEcoute, &longueurAdresse, &pointDeRencontreLocal);

    // Boucle principale du serveur
    boucleServeur(socketEcoute);

	char* mot = creationMot();
    // Ne sera jamais atteint en pratique
    close(socketEcoute);
    return 0;
}
