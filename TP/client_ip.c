#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <string.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define LG_MESSAGE 256

int main(int argc, char *argv[]){
    int sock;
    struct sockaddr_in addrServ;
    socklen_t lg = sizeof(struct sockaddr_in);
    char buffer[LG_MESSAGE];
    char reponse[LG_MESSAGE];
    int port;

    if (argc < 3){
        printf("USAGE : %s ip port\n", argv[0]);
        exit(-1);
    }

    strncpy(buffer, "", LG_MESSAGE);
    printf("Voulez-vous l'heure ou la date ? ");
    scanf("%255s", buffer);

    sscanf(argv[2], "%d", &port);

    // 1. Création socket
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0){
        perror("socket");
        exit(-1);
    }

    // 2. Remplir addr
    memset(&addrServ, 0, lg);
    addrServ.sin_family = AF_INET;
    addrServ.sin_port = htons(port);
    inet_aton(argv[1], &addrServ.sin_addr);

    // 3. Demande de connexion
    if (connect(sock, (struct sockaddr *)&addrServ, lg) < 0){
        perror("connect");
        exit(-2);
    }

    // 4. Envoyer la demande
    send(sock, buffer, strlen(buffer)+1, 0);

    // 5. Recevoir la réponse
    int lus = recv(sock, reponse, LG_MESSAGE, 0);
    if (lus > 0){
        printf("Réponse du serveur : %s\n", reponse);
    } else {
        printf("Erreur de réception.\n");
    }

    // 6. Fermeture
    close(sock);
    return 0;
}
