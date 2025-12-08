#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <string.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define LG_BUFFER 256

int main(int argc, char *argv[]) {

    if (argc < 3) {
        printf("USAGE : %s ip port\n", argv[0]);
        exit(1);
    }

    char ip[16];
    int port;

    strncpy(ip, argv[1], 16);
    sscanf(argv[2], "%d", &port);

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket");
        exit(2);
    }

    struct sockaddr_in serv;
    socklen_t lg = sizeof(serv);

    memset(&serv, 0, lg);
    serv.sin_family = AF_INET;
    serv.sin_port = htons(port);
    inet_aton(ip, &serv.sin_addr);

    if (connect(sock, (struct sockaddr *)&serv, lg) < 0) {
        perror("connect");
        close(sock);
        exit(3);
    }

    printf("Connecté au serveur %s:%d\n", ip, port);

    char buffer[LG_BUFFER];

    // --- Attendre le message "start x" ---
    int lus = recv(sock, buffer, LG_BUFFER, 0);
    if (lus <= 0) {
        printf("Serveur fermé.\n");
        exit(4);
    }

    buffer[lus] = 0;
    printf("Serveur : %s\n", buffer);

    int taille = 0;
    sscanf(buffer, "start %d", &taille);

    // Affichage initial (x tirets)
    char mask[LG_BUFFER];
    for (int i = 0; i < taille; i++) mask[i] = '_';
    mask[taille] = 0;

    printf("Mot : %s\n", mask);

    // --- Boucle de jeu ---
    while (1) {
        char lettre;
        printf("Choisir une lettre : ");
        scanf(" %c", &lettre);

        char msg[64];
        snprintf(msg, 64, "LETTER %c", lettre);
        send(sock, msg, strlen(msg) + 1, 0);

        // Attendre la réponse
        lus = recv(sock, buffer, LG_BUFFER, 0);
        if (lus <= 0) {
            printf("Serveur fermé.\n");
            break;
        }
        buffer[lus] = 0;

        printf("Serveur : %s\n", buffer);

        char status[32], newmask[128];
        int tries;

        // Analyse du résultat
        if (sscanf(buffer, "RESULT %s %s %d", status, newmask, &tries) >= 2) {

            if (strcmp(status, "ALREADY") == 0) {
                printf("Lettre déjà utilisée ! Mot : %s\n", newmask);
            }
            else if (strcmp(status, "CONTINUE") == 0) {
                printf("Mot : %s | Tentatives restantes : %d\n", newmask, tries);
            }
            else if (strcmp(status, "WON") == 0) {
                printf("🎉 Vous avez gagné ! Mot final : %s\n", newmask);
                break;
            }
            else if (strcmp(status, "LOST") == 0) {
                printf("💀 Vous avez perdu ! Mot final : %s\n", newmask);
                break;
            }
        }
    }

    close(sock);
    return 0;
}
