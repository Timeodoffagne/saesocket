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
#include <signal.h> 

#define PORT 5000
#define LG_MESSAGE 256
// Augmentation de la capacité pour la file d'attente du serveur principal
#define MAX_CLIENTS_EN_ATTENTE 10 

// =====================================================
// STRUCTURES
// =====================================================
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
    int pret; // 0 = pas prêt, 1 = prêt à jouer, 2 = demande de rejeu
} ClientData;

// Tableau des clients en attente de partie (pour le processus parent)
ClientData *fileAttente[MAX_CLIENTS_EN_ATTENTE] = {NULL}; // Initialisation correcte

// =====================================================
// OUTILS COMMUNICATION (Identique)
// =====================================================

void envoyerPacket(int sock, int source_id, const char *msg)
{
    Packet p;
    memset(&p, 0, sizeof(p));

    p.destinataire = source_id;
    strncpy(p.message, msg, LG_MESSAGE - 1);
    p.message[LG_MESSAGE - 1] = '\0';

    char buffer[sizeof(Packet)];
    memset(buffer, 0, sizeof(buffer));

    int dest_net = htonl(p.destinataire);
    memcpy(buffer, &dest_net, sizeof(int));
    memcpy(buffer + sizeof(int), p.message, LG_MESSAGE);

    send(sock, buffer, sizeof(Packet), 0);
    printf("Serveur [%d] -> Client (%d) C%d: '%s'\n", getpid(), sock, source_id, msg);
}

int recevoirPacket(int sock, Packet *p)
{
    char buffer[sizeof(int) + LG_MESSAGE];
    memset(buffer, 0, sizeof(buffer));

    int rec = recv(sock, buffer, sizeof(buffer), 0);

    if (rec <= 0)
        return rec;

    int dest_net;
    memcpy(&dest_net, buffer, sizeof(int));
    p->destinataire = ntohl(dest_net);

    memcpy(p->message, buffer + sizeof(int), LG_MESSAGE);
    p->message[LG_MESSAGE - 1] = '\0';

    printf("Serveur [%d] <- Client (%d) Dest=%d: Message='%s'\n", getpid(), sock, p->destinataire, p->message);
    
    return rec;
}


// =====================================================
// LOGIQUE JEU DE LA PARTIE V3 (Relais Multi-Manche - Pas de changement)
// =====================================================

void traiterJeu(ClientData *c1, ClientData *c2)
{
    Packet p;
    int ret;
    
    ClientData *maitre;
    ClientData *devineur;
    
    maitre = (c1->id == 1) ? c1 : c2;
    devineur = (c1->id == 2) ? c1 : c2;
    
    printf("Serveur [%d]: Début du jeu entre C%d (Maître) et C%d (Devineur).\n", 
           getpid(), maitre->id, devineur->id);
           
    int continuer_partie = 1;
    
    while (continuer_partie)
    {
        // PHASE 1 : INITIALISATION (RELAIS DU READY_WORD)
        printf("Serveur [%d]: Attente de READY_WORD de C%d (Maître).\n", getpid(), maitre->id);
        
        ret = recevoirPacket(maitre->socket, &p);
        
        if (ret <= 0 || strstr(p.message, "READY_WORD:") != p.message)
        {
            printf("Serveur [%d]: C%d déconnecté/Erreur pendant READY_WORD. Fin de partie.\n", getpid(), maitre->id);
            if (ret > 0) envoyerPacket(devineur->socket, maitre->id, "PARTNER_DISCONNECTED");
            goto end_game; 
        }
        
        printf("Serveur [%d]: Relais de READY_WORD à C%d (Devineur).\n", getpid(), devineur->id);
        envoyerPacket(devineur->socket, maitre->id, p.message);
        
        // PHASE 2 : BOUCLE DE JEU
        int manche_finie = 0;
        while (!manche_finie)
        {
            // 1. Attente de la lettre du devineur (C2)
            ret = recevoirPacket(devineur->socket, &p);
            if (ret <= 0 || strcmp(p.message, "exit") == 0) 
            {
                printf("Serveur [%d]: Devineur (C%d) déconnecté. Fin de partie.\n", getpid(), devineur->id);
                envoyerPacket(maitre->socket, devineur->id, "PARTNER_DISCONNECTED");
                goto end_game;
            }
            if (strcmp(p.message, "REPLAY") == 0) { 
                devineur->pret = 2;
                manche_finie = 1;
                break;
            }

            // Relais de la lettre vers C1.
            envoyerPacket(maitre->socket, devineur->id, p.message);
            
            // 2. Attente de l'UPDATE de C1 (Maître du Jeu)
            ret = recevoirPacket(maitre->socket, &p);
            if (ret <= 0 || strcmp(p.message, "exit") == 0)
            {
                printf("Serveur [%d]: Maître du Jeu (C%d) déconnecté. Fin de partie.\n", getpid(), maitre->id);
                envoyerPacket(devineur->socket, maitre->id, "PARTNER_DISCONNECTED");
                goto end_game;
            }

            // Relais de l'UPDATE vers C2
            envoyerPacket(devineur->socket, maitre->id, p.message);
            
            // 3. Vérifier si l'UPDATE n'était pas la fin de partie (END_GAME)
            if (strstr(p.message, "END_GAME:") == p.message)
            {
                manche_finie = 1;
            }
            else
            {
                // Attente de END_GAME (si la partie s'est finie avec ce coup)
                ret = recevoirPacket(maitre->socket, &p);
                
                if (ret <= 0 || strcmp(p.message, "exit") == 0) {
                     printf("Serveur [%d]: Maître du Jeu (C%d) déconnecté après l'UPDATE. Fin de partie.\n", getpid(), maitre->id);
                     envoyerPacket(devineur->socket, maitre->id, "PARTNER_DISCONNECTED");
                     goto end_game;
                }
                
                if (strstr(p.message, "END_GAME:") == p.message)
                {
                    envoyerPacket(devineur->socket, maitre->id, p.message);
                    manche_finie = 1;
                }
            }
        } // Fin de la boucle de manche
        
        // PHASE 3 : POST-GAME (ATTENTE REPLAY/EXIT)
        
        printf("Serveur [%d]: Fin de manche. Attente des commandes REPLAY/exit...\n", getpid());
        
        int c1_wants_replay = 0;
        int c2_wants_replay = 0;
        
        // Attente REPLAY/exit de C1 (Maître)
        ret = recevoirPacket(maitre->socket, &p);
        if (ret > 0) 
        {
            if (strcmp(p.message, "REPLAY") == 0) c1_wants_replay = 1;
            if (strcmp(p.message, "exit") == 0) goto end_game;
        } else {
             goto end_game;
        }
        
        // Attente REPLAY/exit de C2 (Devineur)
        if (devineur->pret != 2) // Si REPLAY n'a pas été envoyé en début de tour
        {
            ret = recevoirPacket(devineur->socket, &p);
            if (ret > 0) 
            {
                if (strcmp(p.message, "REPLAY") == 0) c2_wants_replay = 1;
                if (strcmp(p.message, "exit") == 0) goto end_game;
            } else {
                 goto end_game;
            }
        } else {
            c2_wants_replay = 1;
            devineur->pret = 0; 
        }
        
        if (c1_wants_replay && c2_wants_replay)
        {
            // SWAP ROLES
            ClientData *temp_ptr = maitre;
            maitre = devineur; 
            devineur = temp_ptr; 
            
            // Envoyer les messages de swap (REPLAY_START:X)
            char msg[LG_MESSAGE];
            
            snprintf(msg, LG_MESSAGE, "REPLAY_START:%d", 1); 
            envoyerPacket(maitre->socket, maitre->id, msg); 
            
            snprintf(msg, LG_MESSAGE, "REPLAY_START:%d", 2);
            envoyerPacket(devineur->socket, devineur->id, msg); 

            printf("Serveur [%d]: REPLAY accepté. Rôles inversés : Nouveau Maître C%d, Nouveau Devineur C%d.\n", 
                   getpid(), maitre->id, devineur->id);
        }
        else
        {
            continuer_partie = 0; 
        }
        
    } 

end_game:
    printf("Serveur [%d]: Processus enfant terminé. Libération des ressources.\n", getpid());
    close(c1->socket);
    close(c2->socket);
    free(c1);
    free(c2);
    exit(EXIT_SUCCESS);
}


// =====================================================
// FONCTION : boucle du serveur (gestion des connexions)
// =====================================================

void boucleServeur(int socketEcoute)
{
    fd_set set;
    int max_fd;
    int ret;
    
    signal(SIGCHLD, SIG_IGN); 

    printf("Serveur [%d] prêt à écouter les connexions sur le port %d.\n", getpid(), PORT);
    
    while (1)
    {
        FD_ZERO(&set);
        FD_SET(STDIN_FILENO, &set);
        max_fd = STDIN_FILENO;

        // Ajouter les sockets clients existants à l'ensemble (parcourt toute la file)
        for (int i = 0; i < MAX_CLIENTS_EN_ATTENTE; i++)
        {
            if (fileAttente[i] != NULL)
            {
                FD_SET(fileAttente[i]->socket, &set);
                if (fileAttente[i]->socket > max_fd)
                    max_fd = fileAttente[i]->socket;
            }
        }

        // Ajouter la socket d'écoute
        FD_SET(socketEcoute, &set);
        if (socketEcoute > max_fd)
            max_fd = socketEcoute;

        ret = select(max_fd + 1, &set, NULL, NULL, NULL);

        if (ret < 0)
        {
            if (errno == EINTR) continue;
            perror("select");
            break;
        }

        // Gérer l'entrée standard (exit) - Pas de changement
        if (FD_ISSET(STDIN_FILENO, &set))
        {
            char line[LG_MESSAGE];
            if (fgets(line, sizeof(line), stdin) != NULL)
            {
                line[strcspn(line, "\n")] = 0;
                if (strcmp(line, "exit") == 0)
                {
                    printf("Arrêt serveur demandé par stdin. Déconnexion des clients en attente.\n");
                    for (int i = 0; i < MAX_CLIENTS_EN_ATTENTE; i++)
                    {
                        if (fileAttente[i] != NULL)
                        {
                            envoyerPacket(fileAttente[i]->socket, 0, "SERVER_SHUTDOWN");
                            close(fileAttente[i]->socket);
                            free(fileAttente[i]);
                            fileAttente[i] = NULL;
                        }
                    }
                    return;
                }
            }
        }

        // Gérer les nouvelles connexions
        if (FD_ISSET(socketEcoute, &set))
        {
            ClientData *newClient = (ClientData *)malloc(sizeof(ClientData));
            if (!newClient) {
                perror("malloc");
                continue;
            }

            socklen_t addrlen = sizeof(newClient->addr);
            newClient->socket = accept(socketEcoute, (struct sockaddr *)&newClient->addr, &addrlen);

            if (newClient->socket < 0) {
                perror("accept");
                free(newClient);
                continue;
            }

            // Trouver une place dans la file d'attente (n'importe quel slot NULL)
            int placed = 0;
            for (int i = 0; i < MAX_CLIENTS_EN_ATTENTE; i++)
            {
                if (fileAttente[i] == NULL)
                {
                    newClient->id = i + 1; // ID basé sur la position dans la file
                    newClient->pret = 0;
                    fileAttente[i] = newClient;
                    placed = 1;
                    
                    char welcome_msg[LG_MESSAGE];
                    snprintf(welcome_msg, LG_MESSAGE, "Bienvenue C%d. En attente de commande...", newClient->id);
                    envoyerPacket(newClient->socket, newClient->id, welcome_msg);

                    printf("Serveur [%d]: Nouveau client C%d connecté (socket %d).\n", getpid(), newClient->id, newClient->socket);
                    break;
                }
            }

            if (!placed)
            {
                // File d'attente pleine, refuser la connexion (si MAX_CLIENTS_EN_ATTENTE est atteint)
                printf("Serveur [%d]: Connexion refusée. File d'attente pleine (%d max).\n", getpid(), MAX_CLIENTS_EN_ATTENTE);
                envoyerPacket(newClient->socket, 0, "SERVEUR_PLEIN");
                close(newClient->socket);
                free(newClient);
            }
        }

        // Gérer les messages des clients en attente - Pas de changement
        for (int i = 0; i < MAX_CLIENTS_EN_ATTENTE; i++)
        {
            ClientData *client = fileAttente[i];
            if (client != NULL && FD_ISSET(client->socket, &set))
            {
                Packet p;
                ret = recevoirPacket(client->socket, &p);

                if (ret <= 0 || strcmp(p.message, "exit") == 0)
                {
                    printf("Serveur [%d]: Client C%d déconnecté.\n", getpid(), client->id);
                    close(client->socket);
                    free(client);
                    fileAttente[i] = NULL;
                    continue;
                }
                
                if (strcmp(p.message, "start") == 0)
                {
                    client->pret = 1;
                    printf("Serveur [%d]: Client C%d est prêt (start).\n", getpid(), client->id);
                }
            }
        }
        
        // =================================================
        // VÉRIFICATION D'UNE NOUVELLE PARTIE (LOGIQUE CORRIGÉE)
        // =================================================

        ClientData *c1_partie = NULL;
        ClientData *c2_partie = NULL;
        int index1 = -1;
        int index2 = -1;

        // Trouver la première paire de clients prêts (pret = 1)
        for (int i = 0; i < MAX_CLIENTS_EN_ATTENTE; i++) {
            if (fileAttente[i] != NULL && fileAttente[i]->pret == 1) {
                if (c1_partie == NULL) {
                    c1_partie = fileAttente[i];
                    index1 = i;
                } else {
                    c2_partie = fileAttente[i];
                    index2 = i;
                    break; // Paire trouvée
                }
            }
        }
        
        // Démarrage : 2 clients trouvés et prêts
        if (c1_partie != NULL && c2_partie != NULL)
        {
            // **********************************************
            // DÉMARRAGE DE LA PARTIE DANS UN PROCESSUS ENFANT
            // **********************************************
            pid_t pid = fork();

            if (pid < 0)
            {
                perror("fork");
                printf("Serveur [%d]: Échec du fork. Clients C%d et C%d restent en attente.\n", getpid(), c1_partie->id, c2_partie->id);
                // Réinitialiser le statut 'pret' pour éviter un fork infini
                c1_partie->pret = 0;
                c2_partie->pret = 0;
            }
            else if (pid == 0) // Processus enfant
            {
                // Cloner les structures pour le processus enfant
                ClientData *client_copy1 = (ClientData *)malloc(sizeof(ClientData));
                ClientData *client_copy2 = (ClientData *)malloc(sizeof(ClientData));
                
                // C1 est toujours le Maître du jeu (ID 1)
                // C2 est toujours le Devineur (ID 2)
                client_copy1->id = 1;
                client_copy2->id = 2;
                client_copy1->socket = c1_partie->socket;
                client_copy2->socket = c2_partie->socket;
                
                // Envoyer le message de confirmation (start) aux clients
                // On utilise les ID 1 et 2 pour la communication de la partie.
                envoyerPacket(client_copy1->socket, client_copy1->id, "start");
                envoyerPacket(client_copy2->socket, client_copy2->id, "start");
                
                // L'enfant exécute la logique de jeu
                traiterJeu(client_copy1, client_copy2); 
            }
            else // Processus parent
            {
                printf("Serveur [%d]: Partie créée (PID: %d) entre C%d et C%d. Nettoyage de la file d'attente aux indices %d et %d.\n", 
                       getpid(), pid, c1_partie->id, c2_partie->id, index1, index2);

                // Fermer les sockets du parent et libérer la mémoire aux indices trouvés
                close(c1_partie->socket);
                close(c2_partie->socket);
                free(c1_partie);
                free(c2_partie);
                fileAttente[index1] = NULL;
                fileAttente[index2] = NULL;
            }
        }
        else 
        {
            // ENVOI DE L'ÉTAT D'ATTENTE (s'il y a au moins un client prêt seul)
            
            // Si c1_partie est trouvé mais pas c2_partie (client seul prêt)
            if (c1_partie != NULL && c2_partie == NULL)
            {
                envoyerPacket(c1_partie->socket, 0, "EN_ATTENTE_ADVERSAIRE");
            }
        }
    }
}

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

    int optval = 1;
    if (setsockopt(*socketEcoute, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0) {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }

    *longueurAdresse = sizeof(struct sockaddr_in);
    memset(pointDeRencontreLocal, 0, *longueurAdresse);

    pointDeRencontreLocal->sin_family = AF_INET;
    pointDeRencontreLocal->sin_addr.s_addr = htonl(INADDR_ANY);
    pointDeRencontreLocal->sin_port = htons(PORT);

    if (bind(*socketEcoute, (struct sockaddr *)pointDeRencontreLocal, *longueurAdresse) < 0)
    {
        perror("bind");
        close(*socketEcoute);
        exit(EXIT_FAILURE);
    }

    if (listen(*socketEcoute, 2) < 0) 
    {
        perror("listen");
        close(*socketEcoute);
        exit(EXIT_FAILURE);
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