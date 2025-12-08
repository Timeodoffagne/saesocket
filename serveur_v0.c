#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <string.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define PORT 5000
#define LG_BUFFER 256

int main() {
    int sockListen = socket(AF_INET, SOCK_STREAM, 0);
    if (sockListen < 0) {
        perror("socket");
        exit(1);
    }

    // Autorise le port immédiat après restart
    int opt = 1;
    setsockopt(sockListen, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in local;
    socklen_t lg = sizeof(local);

    memset(&local, 0, lg);
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    local.sin_port = htons(PORT);

    if (bind(sockListen, (struct sockaddr *)&local, lg) < 0) {
        perror("bind");
        exit(2);
    }

    if (listen(sockListen, 5) < 0) {
        perror("listen");
        exit(3);
    }

    printf("Serveur pendu lancé sur port %d\n", PORT);

    // Mot à deviner
    const char *mot = "HELLO";
    int taille = strlen(mot);

    while (1) {
        printf("En attente d'un client...\n");

        struct sockaddr_in distant;
        socklen_t lg2 = sizeof(distant);

        int sockDialog = accept(sockListen, (struct sockaddr *)&distant, &lg2);
        if (sockDialog < 0) {
            perror("accept");
            continue;
        }

        printf("Client connecté.\n");

        // Variables de jeu
        char mask[LG_BUFFER];
        int tries = 6;
        int used[256] = {0};

        for (int i = 0; i < taille; i++) mask[i] = '_';
        mask[taille] = 0;

        // Envoi "start x"
        char msgStart[64];
        snprintf(msgStart, 64, "start %d", taille);
        send(sockDialog, msgStart, strlen(msgStart) + 1, 0);

        char buffer[LG_BUFFER];

        // Boucle de jeu
        while (1) {
            int lus = recv(sockDialog, buffer, LG_BUFFER, 0);
            if (lus <= 0) break;

            buffer[lus] = 0;

            char cmd[16];
            char lettre;
            sscanf(buffer, "%s %c", cmd, &lettre);

            lettre = toupper(lettre);

            if (used[(unsigned char)lettre]) {
                char resp[128];
                snprintf(resp, 128, "RESULT ALREADY %s %d", mask, tries);
                send(sockDialog, resp, strlen(resp) + 1, 0);
                continue;
            }

            used[(unsigned char)lettre] = 1;

            int found = 0;
            for (int i = 0; i < taille; i++) {
                if (mot[i] == lettre) {
                    mask[i] = lettre;
                    found = 1;
                }
            }

            if (!found) tries--;

            if (strcmp(mask, mot) == 0) {
                char resp[128];
                snprintf(resp, 128, "RESULT WON %s", mask);
                send(sockDialog, resp, strlen(resp) + 1, 0);
                break;
            }

            if (tries <= 0) {
                char resp[128];
                snprintf(resp, 128, "RESULT LOST %s", mot);
                send(sockDialog, resp, strlen(resp) + 1, 0);
                break;
            }

            char resp[128];
            snprintf(resp, 128, "RESULT CONTINUE %s %d", mask, tries);
            send(sockDialog, resp, strlen(resp) + 1, 0);
        }

        close(sockDialog);
        printf("Partie terminée, retour en écoute.\n");
    }

    close(sockListen);
    return 0;
}
