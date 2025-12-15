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
    int replay_requested; // 1 si le client a envoyé 'REPLAY', 0 sinon
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

/* ----------------------------------------------------------------------------------- */
// Codes de retour spéciaux pour traiterPacket :
// 0: Déconnexion
// 1: Relais standard réussi
// 2: REPLAY reçu
// 3: ROLE_SWAP déclenché
/* ----------------------------------------------------------------------------------- */
int traiterPacket(ClientData *c1, ClientData *c2, int identifiantClient)
{
    Packet p;
    ClientData *joueurSource = (identifiantClient == 1) ? c1 : c2;
    ClientData *joueurDest;

    if (identifiantClient == 1)
        joueurDest = c2;
    else
        joueurDest = c1;

    // 1. Recevoir le paquet du joueur émetteur
    int lus = recevoirPacket(joueurSource->socket, &p);
    if (lus <= 0)
        return 0; // Déconnexion

    printf("[SERVEUR RELAIS] Client %d | Message='%s'\n", identifiantClient, p.message);

    // 2. Traitement des commandes de contrôle (non-relais)
    if (strcmp(p.message, "exit") == 0)
    {
        printf("Client %d demande une fermeture.\n", identifiantClient);
        if (joueurDest)
            envoyerPacket(joueurDest->socket, joueurDest->id, "PARTNER_DISCONNECTED");
        return 0;
    }

    // 3. Logique de Rejeu / Inversion des rôles
    if (strcmp(p.message, "REPLAY") == 0)
    {
        joueurSource->replay_requested = 1;
        
        if (joueurDest && joueurDest->replay_requested == 1)
        {
            // Les deux sont prêts, le rôle sera inversé dans boucleServeur
            return 3; // Déclenche le ROLE_SWAP
        }
        else
        {
            // Relayer la demande de rejeu à l'autre joueur
            if (joueurDest && joueurDest->socket >= 0)
                envoyerPacket(joueurDest->socket, joueurSource->id, "REPLAY_REQUESTED"); 
        }
        return 2; // REPLAY reçu, mais pas encore prêt pour swap
    }

    // 4. Relais : si l'autre joueur existe, envoyer le message tel quel.
    if (joueurDest && joueurDest->socket >= 0)
    {
        // L'ID du destinataire doit être l'ID de l'autre client, 
        // mais le paquet doit contenir l'ID de la source pour le client
        if (envoyerPacket(joueurDest->socket, joueurSource->id, p.message) <= 0)
        {
            printf("Erreur de relais vers client %d\n", joueurDest->id);
            return 0;
        }
        return 1;
    }
    else
    {
        envoyerPacket(joueurSource->socket, joueurSource->id, "EN_ATTENTE_ADVERSAIRE");
    }

    return 1;
}

/* -------------------------------------------------------------------------- */
void boucleServeur(int socketEcoute)
{
    socklen_t longueurAdresse = sizeof(struct sockaddr_in);
    struct sockaddr_in clientAddr;

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

        struct timeval tv = {1, 0}; // Timeout de 1 seconde
        int ready = select(maxfd + 1, &readfds, NULL, NULL, &tv);
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
                ClientData *c = malloc(sizeof(ClientData));
                if (!c)
                {
                    perror("malloc");
                    close(s);
                    continue;
                }
                c->socket = s;
                c->addr = clientAddr;
                c->replay_requested = 0;
                
                if (!client1)
                {
                    c->id = 1;
                    client1 = c;
                    printf("Client 1 connecté : %s\n", inet_ntoa(c->addr.sin_addr));
                    envoyerPacket(s, 1, "Bienvenue client 1 ! Vous êtes le Maître du Jeu pour la 1ère partie.");
                }
                else if (!client2)
                {
                    c->id = 2;
                    client2 = c;
                    printf("Client 2 connecté : %s\n", inet_ntoa(c->addr.sin_addr));
                    envoyerPacket(s, 2, "Bienvenue client 2 ! Vous êtes le Devineur pour la 1ère partie.");
                }
                else
                {
                    printf("Serveur plein : refus du client %s\n", inet_ntoa(clientAddr.sin_addr));
                    char *msg = "Serveur plein\n";
                    send(s, msg, strlen(msg), 0);
                    free(c);
                    close(s);
                }
            }
        }

        // Activité sur client1
        if (client1 && client1->socket >= 0 && FD_ISSET(client1->socket, &readfds))
        {
            int status = traiterPacket(client1, client2, 1);
            if (status <= 0)
            {
                printf("Client 1 (%s) déconnecté.\n", inet_ntoa(client1->addr.sin_addr));
                close(client1->socket);
                free(client1);
                client1 = NULL;
                if (client2) envoyerPacket(client2->socket, client2->id, "PARTNER_DISCONNECTED");
            }
            else if (status == 3) // ROLE_SWAP déclenché
            {
                // Inversion des rôles (swap des pointeurs et des IDs)
                ClientData *temp = client1;
                client1 = client2;
                client2 = temp;
                
                client1->id = 1; 
                client2->id = 2; 
                client1->replay_requested = 0; // Réinitialisation de l'état
                client2->replay_requested = 0;
                
                printf("[SERVEUR] RÔLES INVERSÉS : Nouveau C1 (Maître) = %d, Nouveau C2 (Devineur) = %d\n", client1->socket, client2->socket);

                // Notifier les clients de leurs nouveaux rôles
                envoyerPacket(client1->socket, client1->id, "ROLE_SWAP:1"); 
                envoyerPacket(client2->socket, client2->id, "ROLE_SWAP:2"); 
            }
        }

        // Activité sur client2
        if (client2 && client2->socket >= 0 && FD_ISSET(client2->socket, &readfds))
        {
            int status = traiterPacket(client1, client2, 2);
            if (status <= 0)
            {
                printf("Client 2 (%s) déconnecté.\n", inet_ntoa(client2->addr.sin_addr));
                close(client2->socket);
                free(client2);
                client2 = NULL;
                if (client1) envoyerPacket(client1->socket, client1->id, "PARTNER_DISCONNECTED");
            }
            else if (status == 3) // ROLE_SWAP déclenché
            {
                // Inversion des rôles (swap des pointeurs et des IDs)
                ClientData *temp = client1;
                client1 = client2;
                client2 = temp;
                
                client1->id = 1; 
                client2->id = 2; 
                client1->replay_requested = 0; // Réinitialisation de l'état
                client2->replay_requested = 0;
                
                printf("[SERVEUR] RÔLES INVERSÉS : Nouveau C1 (Maître) = %d, Nouveau C2 (Devineur) = %d\n", client1->socket, client2->socket);

                // Notifier les clients de leurs nouveaux rôles
                envoyerPacket(client1->socket, client1->id, "ROLE_SWAP:1"); 
                envoyerPacket(client2->socket, client2->id, "ROLE_SWAP:2"); 
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
                    break;
                }
                // Broadcast aux clients (avec ID 0 pour indiquer le serveur)
                if (client1)
                    envoyerPacket(client1->socket, 0, line);
                if (client2)
                    envoyerPacket(client2->socket, 0, line);
            }
        }
    }

    if (client1) { close(client1->socket); free(client1); }
    if (client2) { close(client2->socket); free(client2); }
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