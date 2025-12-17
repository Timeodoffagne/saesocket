// ====================================================
// COTÉ SERVEUR DE LA VERSION 4
// ====================================================

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <string.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <sys/select.h>
#include <time.h>

#define PORT 5000
#define LG_MESSAGE 256
#define P2P_BASE_PORT 6000

typedef struct
{
    int destinataire;
    char message[LG_MESSAGE];
} Packet;

typedef struct
{
    int id;
    int socket;
    struct sockaddr_in addr;
    int pret;
    int p2p_port;
    int p2p_ready; // Nouveau : indique si le socket P2P est prêt
} ClientData;

/* -------------------------------------------------------------------------- */
void creationSocket(int *socketEcoute,
                    socklen_t *longueurAdresse,
                    struct sockaddr_in *pointDeRencontreLocal)
{
    *socketEcoute = socket(AF_INET, SOCK_STREAM, 0);
    if (*socketEcoute < 0)
    {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    *longueurAdresse = sizeof(struct sockaddr_in);
    memset(pointDeRencontreLocal, 0, *longueurAdresse);

    pointDeRencontreLocal->sin_family = AF_INET;
    pointDeRencontreLocal->sin_addr.s_addr = htonl(INADDR_ANY);
    pointDeRencontreLocal->sin_port = htons(PORT);

    int opt = 1;
    setsockopt(*socketEcoute, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    if (bind(*socketEcoute,
             (struct sockaddr *)pointDeRencontreLocal,
             *longueurAdresse) < 0)
    {
        perror("bind");
        exit(EXIT_FAILURE);
    }

    if (listen(*socketEcoute, 5) < 0)
    {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    printf("|===========================================================|\n");
    printf("|     SERVEUR V4 - MATCHMAKING P2P DÉMARRÉ                  |\n");
    printf("|     Port d'écoute : %d                                    |\n", PORT);
    printf("|===========================================================|\n");
}

/* -------------------------------------------------------------------------- */
int envoyerPacket(int sock, int destinataire, const char *msg)
{
    if (sock < 0)
        return -1;

    Packet p;
    p.destinataire = destinataire;
    strncpy(p.message, msg, LG_MESSAGE - 1);
    p.message[LG_MESSAGE - 1] = '\0';

    char buffer[sizeof(Packet)];
    int dest_net = htonl(p.destinataire);
    memcpy(buffer, &dest_net, sizeof(int));
    memcpy(buffer + sizeof(int), p.message, LG_MESSAGE);

    int sent = send(sock, buffer, sizeof(Packet), 0);
    if (sent > 0)
    {
        printf("[ENVOI] Socket %d | Dest=%d | Message='%s'\n", sock, destinataire, msg);
    }
    return sent;
}

/* -------------------------------------------------------------------------- */
int recevoirPacket(int socketDialogue, Packet *p)
{
    char buffer[sizeof(int) + LG_MESSAGE];
    memset(buffer, 0, sizeof(buffer));

    int lus = recv(socketDialogue, buffer, sizeof(buffer), 0);
    if (lus <= 0)
        return lus;

    int dest_net;
    memcpy(&dest_net, buffer, sizeof(int));
    p->destinataire = ntohl(dest_net);

    memcpy(p->message, buffer + sizeof(int), LG_MESSAGE);
    p->message[LG_MESSAGE - 1] = '\0';

    printf("[RECU] Socket %d | Dest=%d | Message='%s'\n",
           socketDialogue, p->destinataire, p->message);

    return lus;
}

/* -------------------------------------------------------------------------- */
void boucleServeur(int socketEcoute)
{
    socklen_t longueurAdresse = sizeof(struct sockaddr_in);
    struct sockaddr_in clientAddr;

    ClientData *client1 = NULL;
    ClientData *client2 = NULL;

    while (1)
    {
        fd_set readfds;
        FD_ZERO(&readfds);

        int maxfd = socketEcoute;
        FD_SET(socketEcoute, &readfds);

        if (client1 && client1->socket >= 0)
        {
            FD_SET(client1->socket, &readfds);
            if (client1->socket > maxfd)
                maxfd = client1->socket;
        }
        if (client2 && client2->socket >= 0)
        {
            FD_SET(client2->socket, &readfds);
            if (client2->socket > maxfd)
                maxfd = client2->socket;
        }

        FD_SET(STDIN_FILENO, &readfds);
        if (STDIN_FILENO > maxfd)
            maxfd = STDIN_FILENO;

        struct timeval timeout;
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;

        int ready = select(maxfd + 1, &readfds, NULL, NULL, &timeout);
        if (ready < 0)
        {
            if (errno == EINTR)
                continue;
            perror("select");
            break;
        }

        // Nouvelle connexion
        if (FD_ISSET(socketEcoute, &readfds))
        {
            int s = accept(socketEcoute, (struct sockaddr *)&clientAddr, &longueurAdresse);
            if (s < 0)
            {
                perror("accept");
            }
            else
            {
                if (!client1)
                {
                    ClientData *c = malloc(sizeof(ClientData));
                    if (!c)
                    {
                        perror("malloc");
                        close(s);
                    }
                    else
                    {
                        c->id = 1;
                        c->socket = s;
                        c->addr = clientAddr;
                        c->pret = 0;
                        c->p2p_port = 0;
                        c->p2p_ready = 0;
                        client1 = c;
                        printf("\n[CONNEXION] v Client 1 connecté : %s\n", inet_ntoa(c->addr.sin_addr));
                        envoyerPacket(s, 1, "Bienvenue Client 1 ! Tapez 'start' pour jouer.");
                    }
                }
                else if (!client2)
                {
                    ClientData *c = malloc(sizeof(ClientData));
                    if (!c)
                    {
                        perror("malloc");
                        close(s);
                    }
                    else
                    {
                        c->id = 2;
                        c->socket = s;
                        c->addr = clientAddr;
                        c->pret = 0;
                        c->p2p_port = 0;
                        c->p2p_ready = 0;
                        client2 = c;
                        printf("\n[CONNEXION] v Client 2 connecté : %s\n", inet_ntoa(c->addr.sin_addr));
                        envoyerPacket(s, 2, "Bienvenue Client 2 ! Tapez 'start' pour jouer.");
                    }
                }
                else
                {
                    printf("\n[REFUS] ✗ Serveur plein : %s\n", inet_ntoa(clientAddr.sin_addr));
                    char *msg = "Serveur plein\n";
                    send(s, msg, strlen(msg), 0);
                    close(s);
                }
            }
        }

        // Activité sur client1
        if (client1 && client1->socket >= 0 && FD_ISSET(client1->socket, &readfds))
        {
            Packet p;
            int lus = recevoirPacket(client1->socket, &p);
            if (lus <= 0)
            {
                printf("\n[DÉCONNEXION] Client 1 déconnecté\n");
                close(client1->socket);
                free(client1);
                client1 = NULL;
            }
            else if (strcmp(p.message, "start") == 0)
            {
                client1->pret = 1;
                printf("[MATCHMAKING] Client 1 est prêt (%d/2)\n", (client1->pret) + (client2 && client2->pret ? 1 : 0));

                // Si les deux sont prêts, lancer le matchmaking P2P
                if (client2 && client2->pret == 1)
                {
                    printf("\n|============================================================|\n");
                    printf("|           LANCEMENT D'UNE PARTIE P2P                       |\n");
                    printf("|============================================================|\n");

                    // Attribuer un port P2P à C1
                    client1->p2p_port = P2P_BASE_PORT + (rand() % 1000);
                    char *ip_c1 = inet_ntoa(client1->addr.sin_addr);

                    printf("[MATCHMAKING] C1 (%s) sera le serveur P2P sur le port %d\n", ip_c1, client1->p2p_port);

                    // Envoyer à C1 : il doit créer son serveur P2P
                    char msg_c1[LG_MESSAGE];
                    snprintf(msg_c1, LG_MESSAGE, "P2P_SERVER:%d", client1->p2p_port);
                    envoyerPacket(client1->socket, 1, msg_c1);
                }
            }
            else if (strcmp(p.message, "P2P_LISTENING") == 0)
            {
                // C1 confirme qu'il écoute sur son port P2P
                client1->p2p_ready = 1;
                printf("[MATCHMAKING] v C1 est en écoute P2P\n");

                // Maintenant on peut dire à C2 de se connecter
                if (client2 && client2->pret == 1)
                {
                    char *ip_c1 = inet_ntoa(client1->addr.sin_addr);
                    char msg_c2[LG_MESSAGE];
                    snprintf(msg_c2, LG_MESSAGE, "P2P_CONNECT:%s:%d", ip_c1, client1->p2p_port);
                    envoyerPacket(client2->socket, 2, msg_c2);

                    printf("[MATCHMAKING] v Infos P2P envoyées à C2\n");
                    printf("[MATCHMAKING] v Serveur prêt pour de nouvelles connexions\n\n");

                    // Fermer les connexions
                    close(client1->socket);
                    close(client2->socket);
                    free(client1);
                    free(client2);
                    client1 = NULL;
                    client2 = NULL;
                }
            }
            else if (strcmp(p.message, "exit") == 0)
            {
                printf("\n[DÉCONNEXION] Client 1 a quitté\n");
                close(client1->socket);
                free(client1);
                client1 = NULL;
            }
        }

        // Activité sur client2
        if (client2 && client2->socket >= 0 && FD_ISSET(client2->socket, &readfds))
        {
            Packet p;
            int lus = recevoirPacket(client2->socket, &p);
            if (lus <= 0)
            {
                printf("\n[DÉCONNEXION] Client 2 déconnecté\n");
                close(client2->socket);
                free(client2);
                client2 = NULL;
            }
            else if (strcmp(p.message, "start") == 0)
            {
                client2->pret = 1;
                printf("[MATCHMAKING] Client 2 est prêt (%d/2)\n", (client1 && client1->pret ? 1 : 0) + (client2->pret));

                // Si les deux sont prêts, lancer le matchmaking P2P
                if (client1 && client1->pret == 1)
                {
                    printf("\n|============================================================|\n");
                    printf("|           LANCEMENT D'UNE PARTIE P2P                       |\n");
                    printf("|============================================================|\n");

                    // Attribuer un port P2P à C1
                    client1->p2p_port = P2P_BASE_PORT + (rand() % 1000);
                    char *ip_c1 = inet_ntoa(client1->addr.sin_addr);

                    printf("[MATCHMAKING] C1 (%s) sera le serveur P2P sur le port %d\n", ip_c1, client1->p2p_port);

                    // Envoyer à C1 : il doit créer son serveur P2P
                    char msg_c1[LG_MESSAGE];
                    snprintf(msg_c1, LG_MESSAGE, "P2P_SERVER:%d", client1->p2p_port);
                    envoyerPacket(client1->socket, 1, msg_c1);
                }
            }
            else if (strcmp(p.message, "exit") == 0)
            {
                printf("\n[DÉCONNEXION] Client 2 a quitté\n");
                close(client2->socket);
                free(client2);
                client2 = NULL;
            }
        }

        // Stdin
        if (FD_ISSET(STDIN_FILENO, &readfds))
        {
            char line[LG_MESSAGE];
            if (fgets(line, sizeof(line), stdin) == NULL)
            {
                printf("stdin fermé (EOF)\n");
            }
            else
            {
                line[strcspn(line, "\n")] = 0;
                if (strcmp(line, "exit") == 0)
                {
                    printf("\n[ARRÊT] Arrêt du serveur demandé\n");
                    if (client1)
                    {
                        close(client1->socket);
                        free(client1);
                    }
                    if (client2)
                    {
                        close(client2->socket);
                        free(client2);
                    }
                    break;
                }
                else if (strcmp(line, "status") == 0)
                {
                    printf("\n--- STATUS SERVEUR ---\n");
                    printf("Client 1 : %s\n", client1 ? "Connecté" : "Libre");
                    printf("Client 2 : %s\n", client2 ? "Connecté" : "Libre");
                    printf("----------------------\n");
                }
            }
        }
    }

    if (client1)
    {
        close(client1->socket);
        free(client1);
    }
    if (client2)
    {
        close(client2->socket);
        free(client2);
    }
}

/* -------------------------------------------------------------------------- */
int main()
{
    srand(time(NULL));

    int socketEcoute;
    socklen_t longueurAdresse;
    struct sockaddr_in pointDeRencontreLocal;

    creationSocket(&socketEcoute, &longueurAdresse, &pointDeRencontreLocal);
    boucleServeur(socketEcoute);

    close(socketEcoute);
    printf("\n[ARRÊT] Serveur terminé proprement\n");
    return 0;
}