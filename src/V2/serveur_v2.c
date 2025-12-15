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
    int role;  // 1 = MAITRE (choisit mot), 2 = DEVINETTE (devine)
} ClientData;

ClientData *client1 = NULL;
ClientData *client2 = NULL;

/* ========================================================================== */
/*                          FONCTIONS RÉSEAU                                  */
/* ========================================================================== */

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

    printf("[SERVEUR] Démarré : écoute sur le port %d\n", PORT);
}

/* -------------------------------------------------------------------------- */
int envoyerPacket(int sock, int destinataire, const char *msg)
{
    if (sock < 0) return -1;
    
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
        printf("[SERVEUR] ENVOI → Socket %d | Dest=%d | Message='%s'\n", 
               sock, destinataire, msg);
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
        printf("[SERVEUR] RECU ← %s | Dest=%d | Message='%s'\n",
               ipClient, p->destinataire, p->message);
    }

    return lus;
}

/* ========================================================================== */
/*                          GESTION DES MESSAGES                              */
/* ========================================================================== */

int traiterMessage(ClientData *expediteur, ClientData *destinataire)
{
    if (!expediteur || !destinataire) return 0;
    
    Packet p;
    int lus = recevoirPacket(expediteur->socket, &p);
    if (lus <= 0)
    {
        printf("[SERVEUR] Client %d déconnecté\n", expediteur->id);
        return 0;
    }

    // Commandes spéciales
    if (strcmp(p.message, "exit") == 0)
    {
        printf("[SERVEUR] Client %d demande la fermeture\n", expediteur->id);
        envoyerPacket(expediteur->socket, expediteur->id, "Au revoir");
        return 0;
    }

    // RELAI PUR : transférer le message au destinataire
    printf("[SERVEUR] RELAI: C%d → C%d | '%s'\n", 
           expediteur->id, destinataire->id, p.message);
    
    if (envoyerPacket(destinataire->socket, expediteur->id, p.message) <= 0)
    {
        printf("[SERVEUR] Erreur envoi vers client %d\n", destinataire->id);
        return 0;
    }

    return lus;
}

/* ========================================================================== */
/*                          BOUCLE PRINCIPALE                                 */
/* ========================================================================== */

void boucleServeur(int socketEcoute)
{
    socklen_t longueurAdresse = sizeof(struct sockaddr_in);
    struct sockaddr_in clientAddr;

    client1 = NULL;
    client2 = NULL;

    printf("[SERVEUR] En attente de connexions...\n");

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
                        c->role = 1;  // MAITRE (choisit le mot)
                        client1 = c;
                        
                        printf("[SERVEUR] Client 1 connecté : %s (ROLE=MAITRE)\n", 
                               inet_ntoa(c->addr.sin_addr));
                        
                        envoyerPacket(s, 1, "ROLE=MAITRE|En attente du joueur 2...");
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
                        c->role = 2;  // DEVINETTE (devine le mot)
                        client2 = c;
                        
                        printf("[SERVEUR] Client 2 connecté : %s (ROLE=DEVINETTE)\n", 
                               inet_ntoa(c->addr.sin_addr));
                        
                        envoyerPacket(s, 2, "ROLE=DEVINETTE|Bienvenue !");
                        
                        // Informer C1 que C2 est là
                        if (client1)
                        {
                            envoyerPacket(client1->socket, 1, 
                                         "JOUEUR2_CONNECTE|Tapez 'start' pour commencer");
                        }
                    }
                }
                else
                {
                    printf("[SERVEUR] Serveur plein : refus du client %s\n", 
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
            int lus = traiterMessage(client1, client2);
            if (lus <= 0)
            {
                printf("[SERVEUR] Client 1 (%s) déconnecté.\n", 
                       inet_ntoa(client1->addr.sin_addr));
                
                // Informer C2
                if (client2 && client2->socket >= 0)
                {
                    envoyerPacket(client2->socket, 2, "ADVERSAIRE_DECONNECTE");
                }
                
                close(client1->socket);
                free(client1);
                client1 = NULL;
                printf("[SERVEUR] Prêt à accepter de nouvelles connexions.\n");
            }
        }

        // Activité sur client2
        if (client2 && client2->socket >= 0 && FD_ISSET(client2->socket, &readfds))
        {
            int lus = traiterMessage(client2, client1);
            if (lus <= 0)
            {
                printf("[SERVEUR] Client 2 (%s) déconnecté.\n", 
                       inet_ntoa(client2->addr.sin_addr));
                
                // Informer C1
                if (client1 && client1->socket >= 0)
                {
                    envoyerPacket(client1->socket, 1, "ADVERSAIRE_DECONNECTE");
                }
                
                close(client2->socket);
                free(client2);
                client2 = NULL;
                printf("[SERVEUR] Prêt à accepter de nouvelles connexions.\n");
            }
        }

        // Stdin (commandes admin)
        if (FD_ISSET(STDIN_FILENO, &readfds))
        {
            char line[LG_MESSAGE];
            if (fgets(line, sizeof(line), stdin) == NULL)
            {
                printf("[SERVEUR] stdin fermé (EOF)\n");
            }
            else
            {
                line[strcspn(line, "\n")] = 0;
                if (strcmp(line, "exit") == 0)
                {
                    printf("[SERVEUR] Arrêt demandé par stdin.\n");
                    
                    if (client1)
                    {
                        envoyerPacket(client1->socket, 1, "SERVEUR_ARRET");
                        close(client1->socket);
                        free(client1);
                        client1 = NULL;
                    }
                    if (client2)
                    {
                        envoyerPacket(client2->socket, 2, "SERVEUR_ARRET");
                        close(client2->socket);
                        free(client2);
                        client2 = NULL;
                    }
                    break;
                }
                else if (strcmp(line, "status") == 0)
                {
                    printf("[SERVEUR] État:\n");
                    printf("  - Client 1: %s\n", client1 ? "Connecté" : "Déconnecté");
                    printf("  - Client 2: %s\n", client2 ? "Connecté" : "Déconnecté");
                }
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

/* ========================================================================== */
/*                                  MAIN                                      */
/* ========================================================================== */

int main()
{
    int socketEcoute;
    socklen_t longueurAdresse;
    struct sockaddr_in pointDeRencontreLocal;

    creationSocket(&socketEcoute, &longueurAdresse, &pointDeRencontreLocal);
    boucleServeur(socketEcoute);

    close(socketEcoute);
    printf("[SERVEUR] Arrêté proprement.\n");
    return 0;
}