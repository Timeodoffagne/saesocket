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

void lire_heure(char *heure){
    FILE *fpipe;
    fpipe = popen("date '+%X'","r");
    if (!fpipe){
        perror("popen");
        exit(-1);
    }
    fgets(heure, LG_MESSAGE, fpipe);
    pclose(fpipe);
}

void lire_date(char *date){
    FILE *fpipe;
    fpipe = popen("date '+%A %d %B %Y'","r");
    if (!fpipe){
        perror("popen");
        exit(-1);
    }
    fgets(date, LG_MESSAGE, fpipe);
    pclose(fpipe);
}

int main(){
    int socketEcoute, socketDialogue;
    struct sockaddr_in addrLocal, addrDistant;
    socklen_t lg = sizeof(struct sockaddr_in);
    char buffer[LG_MESSAGE];
    char reponse[LG_MESSAGE];

    // 1. Création de la socket d'écoute
    socketEcoute = socket(AF_INET, SOCK_STREAM, 0);
    if (socketEcoute < 0){
        perror("socket");
        exit(-1);
    }
    printf("Socket d'écoute créée (%d)\n", socketEcoute);

    // 2. Attachement local
    memset(&addrLocal, 0, lg);
    addrLocal.sin_family = AF_INET;
    addrLocal.sin_addr.s_addr = htonl(INADDR_ANY);
    addrLocal.sin_port = htons(PORT);

    if (bind(socketEcoute, (struct sockaddr *)&addrLocal, lg) < 0){
        perror("bind");
        exit(-2);
    }

    // 3. Déclaration des connexions autorisées
    if (listen(socketEcoute, 5) < 0){
        perror("listen");
        exit(-3);
    }

    printf("Serveur en écoute sur le port %d…\n", PORT);

    while (1){
        printf("En attente d'une connexion…\n\n");

        // 4. Attente connexion
        socketDialogue = accept(socketEcoute, (struct sockaddr *)&addrDistant, &lg);
        if (socketDialogue < 0){
            perror("accept");
            continue;
        }

        // 5. Réception de la demande
        int lus = recv(socketDialogue, buffer, LG_MESSAGE, 0);
        if (lus <= 0){
            perror("recv");
            close(socketDialogue);
            continue;
        }

        printf("Demande reçue : %s\n", buffer);

        // 6. Traitement
        if (strncmp(buffer, "heure", 5) == 0){
            lire_heure(reponse);
        } else if (strncmp(buffer, "date", 4) == 0){
            lire_date(reponse);
        } else {
            snprintf(reponse, LG_MESSAGE, "Commande inconnue.");
        }

        // 7. Envoi de la réponse
        send(socketDialogue, reponse, strlen(reponse)+1, 0);

        // 8. Fermeture socket dialogue
        close(socketDialogue);
    }

    // 9. Fermeture socket écoute
    close(socketEcoute);
    return 0;
}
