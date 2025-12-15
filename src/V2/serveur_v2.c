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
#define LISTE_MOTS "../../assets/mots.txt"

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
    int essaisRestants;
    int pret;
} ClientData;

ClientData *client1 = NULL;
ClientData *client2 = NULL;

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

    printf("Serveur démarré : écoute sur le port %d\n", PORT);
}

/* -------------------------------------------------------------------------- */
// char *creationMot()
// {
//     // ... (code commenté)
// }

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
        return 0;

    int dest_net;
    memcpy(&dest_net, buffer, sizeof(int));
    p->destinataire = ntohl(dest_net);

    memcpy(p->message, buffer + sizeof(int), LG_MESSAGE);
    p->message[LG_MESSAGE - 1] = '\0';

    struct sockaddr_in addrClient;
    socklen_t len = sizeof(addrClient);

    if (getpeername(socketDialogue, (struct sockaddr *)&addrClient, &len) != -1)
    {
        char *ipClient = inet_ntoa(addrClient.sin_addr);
        printf("[RECU] De %s | Dest=%d | Message='%s'\n",
               ipClient, p->destinataire, p->message);
    }

    return lus;
}

/* -------------------------------------------------------------------------- */
/* JEU DU PENDU                                        */
/* -------------------------------------------------------------------------- */
// int jeuDuPendu(ClientData *c1, ClientData *c2)
// {
//     // ... (code commenté)
// }

/* ----------------------------------------------------------------------------------- */
/* Dans cette version traiterPacket() ne fait que renvoyer un paquet à l'autre client  */
/* ----------------------------------------------------------------------------------- */
int traiterPacket(ClientData *c1, ClientData *c2, int identifiantClient)
{
    Packet p;
    ClientData *joueurSource = (identifiantClient == 1) ? c1 : c2;
    ClientData *joueurDest;

    // Le joueur destinataire est l'autre
    if (identifiantClient == 1)
        joueurDest = c2;
    else
        joueurDest = c1;

    // 1. Recevoir le paquet du joueur émetteur
    int lus = recevoirPacket(joueurSource->socket, &p);
    if (lus <= 0)
        return 0; // Déconnexion

    printf("[SERVEUR] Client %d | Message='%s'\n", identifiantClient, p.message);

    // 2. Traitement des commandes de contrôle (non-relais)
    if (strcmp(p.message, "exit") == 0)
    {
        printf("Client %d demande une fermeture.\n", identifiantClient);
        // Informer l'autre joueur que le partenaire s'en va.
        if (joueurDest)
            envoyerPacket(joueurDest->socket, joueurDest->id, "PARTNER_DISCONNECTED");
        return 0;
    }

    // 3. Logique de Rejeu / Inversion des rôles
    if (strcmp(p.message, "REPLAY") == 0)
    {
        if (!c1 || !c2) return lus; // Pas d'adversaire

        joueurSource->pret = 1; // Marquer le joueur comme prêt à rejouer
        printf("[SERVEUR] Client %d est prêt à rejouer (pret=%d).\n", identifiantClient, joueurSource->pret);

        // Si les deux joueurs sont prêts à rejouer, on inverse les rôles et relance.
        if (c1->pret == 1 && c2->pret == 1)
        {
            printf("[SERVEUR] Les deux clients sont prêts. Inversion des rôles et relance.\n");
            
            // Envoyer à C1 le message de relance avec le NOUVEL ID (2)
            if (envoyerPacket(c1->socket, 2, "REPLAY_START:2") <= 0) return 0;
            // Envoyer à C2 le message de relance avec le NOUVEL ID (1)
            if (envoyerPacket(c2->socket, 1, "REPLAY_START:1") <= 0) return 0;
            
            // Réinitialiser l'état "pret" pour la nouvelle partie
            c1->pret = 0;
            c2->pret = 0;
        } 
        else
        {
             // Relayer le message REPLAY à l'adversaire (qui n'est pas encore prêt)
             if (envoyerPacket(joueurDest->socket, joueurSource->id, p.message) <= 0)
             {
                 printf("Erreur de relais vers client %d\n", joueurDest->id);
                 return 0;
             }
        }
        
        return lus;
    }
    
    // Logique pour la commande 'start' initiale (gestion du 'pret' et de la confirmation de début de jeu)
    if (strcmp(p.message, "start") == 0)
    {
        joueurSource->pret = 1;
        
        if (c1 && c2 && c1->pret == 1 && c2->pret == 1)
        {
            printf("[SERVEUR] Les deux clients sont prêts à jouer. Envoi de 'start'.\n");
            // L'ID dans le paquet reste l'ID du joueur, le message est 'start' pour lancer le jeu.
            if (envoyerPacket(c1->socket, c1->id, "start") <= 0) return 0;
            if (envoyerPacket(c2->socket, c2->id, "start") <= 0) return 0;
            return lus;
        }
    }

    if (joueurDest && joueurDest->socket >= 0)
    {
        if (envoyerPacket(joueurDest->socket, joueurSource->id, p.message) <= 0)
        {
            printf("Erreur de relais vers client %d\n", joueurDest->id);
            return 0;
        }
        return lus;
    }
    else
    {
        envoyerPacket(joueurSource->socket, joueurSource->id, "EN_ATTENTE_ADVERSAIRE");
    }

    return lus;
}
/* -------------------------------------------------------------------------- */
void boucleServeur(int socketEcoute)
{
    socklen_t longueurAdresse = sizeof(struct sockaddr_in);
    struct sockaddr_in clientAddr;

    client1 = NULL;
    client2 = NULL;

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

        int ready = select(maxfd + 1, &readfds, NULL, NULL, NULL);
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
                        c->essaisRestants = 10;
                        c->pret = 0;
                        client1 = c;
                        printf("Client 1 connecté : %s\n", inet_ntoa(c->addr.sin_addr));
                        envoyerPacket(s, 1, "Bienvenue client 1 ! Tapez 'start' pour jouer.");
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
                        c->essaisRestants = 10;
                        c->pret = 0;
                        client2 = c;
                        printf("Client 2 connecté : %s\n", inet_ntoa(c->addr.sin_addr));
                        envoyerPacket(s, 2, "Bienvenue client 2 ! Tapez 'start' pour jouer.");
                    }
                }
                else
                {
                    printf("Serveur plein : refus du client %s\n",
                           inet_ntoa(clientAddr.sin_addr));
                    char *msg = "Serveur plein\n";
                    send(s, msg, strlen(msg), 0);
                    close(s);
                }
            }
        }

        // Activité sur client1
        if (client1 && client1->socket >= 0 && FD_ISSET(client1->socket, &readfds))
        {
            int lus = traiterPacket(client1, client2, 1);
            if (lus <= 0)
            {
                printf("Client 1 (%s) déconnecté.\n", inet_ntoa(client1->addr.sin_addr));
                close(client1->socket);
                free(client1);
                client1 = NULL;
                printf("serveur prêt à accepter de nouvelles connexions.\n");
            }
        }

        // Activité sur client2
        if (client2 && client2->socket >= 0 && FD_ISSET(client2->socket, &readfds))
        {
            int lus = traiterPacket(client1, client2, 2);
            if (lus <= 0)
            {
                printf("Client 2 (%s) déconnecté.\n", inet_ntoa(client2->addr.sin_addr));
                close(client2->socket);
                free(client2);
                client2 = NULL;
                printf("Serveur prêt à accepter de nouvelles connexions.\n");
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
                    printf("Arrêt serveur demandé par stdin.\n");
                    if (client1)
                    {
                        close(client1->socket);
                        free(client1);
                        client1 = NULL;
                    }
                    if (client2)
                    {
                        close(client2->socket);
                        free(client2);
                        client2 = NULL;
                    }
                    break;
                }
                // Broadcast aux clients
                if (client1)
                    envoyerPacket(client1->socket, client1->id, line);
                if (client2)
                    envoyerPacket(client2->socket, client2->id, line);
            }
        }
    }

    if (client1)
    {
        close(client1->socket);
        free(client1);
        client1 = NULL;
    }
    if (client2)
    {
        close(client2->socket);
        free(client2);
        client2 = NULL;
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
    return 0;
}