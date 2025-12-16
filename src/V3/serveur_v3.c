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
#define MAX_CLIENTS_EN_ATTENTE 2

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
ClientData *fileAttente[MAX_CLIENTS_EN_ATTENTE] = {NULL, NULL};


// =====================================================
// OUTILS COMMUNICATION (Identique à V2)
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
// LOGIQUE JEU DE LA PARTIE V3 (Relais Multi-Manche)
// =====================================================
// Cette fonction gère une partie complète, y compris les rejeux/swaps
void traiterJeu(ClientData *c1, ClientData *c2)
{
    Packet p;
    int ret;
    
    // Pointeur pour les rôles actuels, qui seront swappés en cas de rejeu
    ClientData *maitre;
    ClientData *devineur;
    
    // Assigner les rôles initiaux (déjà fait par le parent)
    maitre = (c1->id == 1) ? c1 : c2;
    devineur = (c1->id == 2) ? c1 : c2;
    
    printf("Serveur [%d]: Début du jeu entre C%d (Maître) et C%d (Devineur).\n", 
           getpid(), maitre->id, devineur->id);
           
    int continuer_partie = 1;
    
    // Boucle de jeu/rejeu dans le processus enfant
    while (continuer_partie)
    {
        // ================================================================
        // PHASE 1 : INITIALISATION (RELAIS DU READY_WORD)
        // Correction de l'erreur de communication immédiate.
        // ================================================================
        printf("Serveur [%d]: Attente de READY_WORD de C%d (Maître).\n", getpid(), maitre->id);
        
        // C1 (Maître) choisit son mot et l'envoie (READY_WORD)
        ret = recevoirPacket(maitre->socket, &p);
        
        if (ret <= 0 || strstr(p.message, "READY_WORD:") != p.message)
        {
            printf("Serveur [%d]: C%d déconnecté/Erreur pendant READY_WORD. Fin de partie.\n", getpid(), maitre->id);
            if (ret > 0) envoyerPacket(devineur->socket, maitre->id, "PARTNER_DISCONNECTED");
            goto end_game; 
        }
        
        // Relais de READY_WORD à C2 (Devineur)
        printf("Serveur [%d]: Relais de READY_WORD à C%d (Devineur).\n", getpid(), devineur->id);
        envoyerPacket(devineur->socket, maitre->id, p.message);
        
        // ================================================================
        // PHASE 2 : BOUCLE DE JEU (RELAIS DES LETTRES/UPDATES)
        // ================================================================
        int manche_finie = 0;
        while (!manche_finie)
        {
            // 1. Attente de la lettre du devineur (C2)
            ret = recevoirPacket(devineur->socket, &p);
            if (ret <= 0) 
            {
                printf("Serveur [%d]: Devineur (C%d) déconnecté. Fin de partie.\n", getpid(), devineur->id);
                envoyerPacket(maitre->socket, devineur->id, "PARTNER_DISCONNECTED");
                goto end_game;
            }

            // Relais de la lettre vers C1.
            printf("Serveur [%d]: Relais de la lettre de C%d à C%d.\n", getpid(), devineur->id, maitre->id);
            envoyerPacket(maitre->socket, devineur->id, p.message);
            
            // 2. Attente de l'UPDATE de C1 (Maître du Jeu)
            ret = recevoirPacket(maitre->socket, &p);
            if (ret <= 0)
            {
                printf("Serveur [%d]: Maître du Jeu (C%d) déconnecté. Fin de partie.\n", getpid(), maitre->id);
                envoyerPacket(devineur->socket, maitre->id, "PARTNER_DISCONNECTED");
                goto end_game;
            }

            // Relais de l'UPDATE vers C2
            printf("Serveur [%d]: Relais de l'état de C%d à C%d: '%s'\n", getpid(), maitre->id, devineur->id, p.message);
            envoyerPacket(devineur->socket, maitre->id, p.message);
            
            // 3. Vérifier si l'UPDATE était la fin de partie (END_GAME)
            if (strstr(p.message, "END_GAME:") == p.message)
            {
                manche_finie = 1;
            }
            else
            {
                // Si la partie est finie, C1 envoie END_GAME immédiatement après UPDATE.
                // On doit attendre ce second paquet pour le relayer.
                ret = recevoirPacket(maitre->socket, &p);
                if (ret > 0 && strstr(p.message, "END_GAME:") == p.message)
                {
                    printf("Serveur [%d]: Relais de END_GAME (second message) à C%d: '%s'\n", getpid(), devineur->id, p.message);
                    envoyerPacket(devineur->socket, maitre->id, p.message);
                    manche_finie = 1;
                }
                else if (ret <= 0) {
                     printf("Serveur [%d]: Maître du Jeu (C%d) déconnecté après l'UPDATE. Fin de partie.\n", getpid(), maitre->id);
                     envoyerPacket(devineur->socket, maitre->id, "PARTNER_DISCONNECTED");
                     goto end_game;
                }
            }
        } // Fin de la boucle de manche
        
        // ================================================================
        // PHASE 3 : POST-GAME (ATTENTE REPLAY/EXIT)
        // ================================================================
        
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
        ret = recevoirPacket(devineur->socket, &p);
        if (ret > 0) 
        {
            if (strcmp(p.message, "REPLAY") == 0) c2_wants_replay = 1;
            if (strcmp(p.message, "exit") == 0) goto end_game;
        } else {
             goto end_game;
        }
        
        if (c1_wants_replay && c2_wants_replay)
        {
            // SWAP ROLES
            ClientData *temp_ptr = maitre;
            maitre = devineur; // L'ancien Devineur devient le Maître
            devineur = temp_ptr; // L'ancien Maître devient le Devineur
            
            // Envoyer les messages de swap (REPLAY_START:X)
            char msg_maitre[LG_MESSAGE];
            char msg_devineur[LG_MESSAGE];
            
            // Le nouveau maître reçoit son nouvel ID (1)
            snprintf(msg_maitre, LG_MESSAGE, "REPLAY_START:%d", 1); 
            // Le nouveau devineur reçoit son nouvel ID (2)
            snprintf(msg_devineur, LG_MESSAGE, "REPLAY_START:%d", 2);
            
            envoyerPacket(maitre->socket, maitre->id, msg_maitre); 
            envoyerPacket(devineur->socket, devineur->id, msg_devineur); 

            printf("Serveur [%d]: REPLAY accepté. Rôles inversés : Nouveau Maître C%d, Nouveau Devineur C%d.\n", 
                   getpid(), maitre->id, devineur->id);
            // La boucle `while (continuer_partie)` recommence (nouvelle manche).
        }
        else
        {
            continuer_partie = 0; // Sortie de la boucle de jeu/rejeu.
        }
        
    } // Fin de la boucle de jeu/rejeu

end_game:
    // Nettoyage final du processus enfant
    printf("Serveur [%d]: Fermeture des sockets pour C%d et C%d.\n", getpid(), c1->id, c2->id);
    close(c1->socket);
    close(c2->socket);
    free(c1);
    free(c2);
    printf("Serveur [%d]: Processus enfant terminé.\n", getpid());
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
    
    // Gérer les processus zombies
    signal(SIGCHLD, SIG_IGN); 

    printf("Serveur [%d] prêt à écouter les connexions sur le port %d.\n", getpid(), PORT);
    
    while (1)
    {
        FD_ZERO(&set);
        FD_SET(STDIN_FILENO, &set);
        max_fd = STDIN_FILENO;

        // Ajouter les sockets clients existants à l'ensemble
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
            if (errno == EINTR)
                continue;
            perror("select");
            break;
        }

        // Gérer l'entrée standard (commande exit du serveur)
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
            if (!newClient)
            {
                perror("malloc");
                continue;
            }

            socklen_t addrlen = sizeof(newClient->addr);
            newClient->socket = accept(socketEcoute, (struct sockaddr *)&newClient->addr, &addrlen);

            if (newClient->socket < 0)
            {
                perror("accept");
                free(newClient);
                continue;
            }

            // Trouver une place dans la file d'attente
            int placed = 0;
            for (int i = 0; i < MAX_CLIENTS_EN_ATTENTE; i++)
            {
                if (fileAttente[i] == NULL)
                {
                    newClient->id = i + 1; // ID 1 ou 2 pour la file d'attente
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
                // File d'attente pleine, refuser la connexion
                printf("Serveur [%d]: Connexion refusée. File d'attente pleine.\n", getpid());
                envoyerPacket(newClient->socket, 0, "SERVEUR_PLEIN");
                close(newClient->socket);
                free(newClient);
            }
        }

        // Gérer les messages des clients en attente
        for (int i = 0; i < MAX_CLIENTS_EN_ATTENTE; i++)
        {
            ClientData *client = fileAttente[i];
            if (client != NULL && FD_ISSET(client->socket, &set))
            {
                Packet p;
                ret = recevoirPacket(client->socket, &p);

                if (ret <= 0 || strcmp(p.message, "exit") == 0)
                {
                    // Déconnexion ou fermeture du client
                    printf("Serveur [%d]: Client C%d déconnecté.\n", getpid(), client->id);
                    close(client->socket);
                    free(client);
                    fileAttente[i] = NULL;
                    continue;
                }
                
                // Commande 'start' (prêt à jouer)
                if (strcmp(p.message, "start") == 0)
                {
                    client->pret = 1;
                    printf("Serveur [%d]: Client C%d est prêt (start).\n", getpid(), client->id);
                }
                
                // Commande 'REPLAY' (demande de rejeu)
                // Dans V3, REPLAY ne devrait être reçu que par le processus enfant.
                // Si le parent le reçoit, c'est que le client a quitté/re-rentré la boucle.
                // Nous ignorons le REPLAY dans le parent pour cette implémentation.
            }
        }
        
        // =================================================
        // VÉRIFICATION D'UNE NOUVELLE PARTIE
        // =================================================

        ClientData *c1_partie = fileAttente[0];
        ClientData *c2_partie = fileAttente[1];
        
        // Vérification : 2 clients en attente et les deux sont prêts (1)
        if (c1_partie != NULL && c2_partie != NULL && c1_partie->pret == 1 && c2_partie->pret == 1)
        {
            // **********************************************
            // DÉMARRAGE DE LA PARTIE DANS UN PROCESSUS ENFANT
            // **********************************************
            pid_t pid = fork();

            if (pid < 0)
            {
                perror("fork");
                printf("Serveur [%d]: Échec du fork. Clients C%d et C%d restent en attente.\n", getpid(), c1_partie->id, c2_partie->id);
                c1_partie->pret = 0;
                c2_partie->pret = 0;
            }
            else if (pid == 0) // Processus enfant
            {
                // Cloner les structures pour le processus enfant
                ClientData *client_copy1 = (ClientData *)malloc(sizeof(ClientData));
                ClientData *client_copy2 = (ClientData *)malloc(sizeof(ClientData));
                
                // On copie les données avant que le parent ne les free/reset
                memcpy(client_copy1, c1_partie, sizeof(ClientData));
                memcpy(client_copy2, c2_partie, sizeof(ClientData));

                // Envoyer le message de confirmation (start) aux clients
                envoyerPacket(client_copy1->socket, client_copy1->id, "start");
                envoyerPacket(client_copy2->socket, client_copy2->id, "start");
                
                // L'enfant exécute la logique de jeu
                traiterJeu(client_copy1, client_copy2); 
                
                // Note: traiterJeu appelle exit(EXIT_SUCCESS)
            }
            else // Processus parent
            {
                printf("Serveur [%d]: Partie créée (PID: %d). Nettoyage de la file d'attente.\n", getpid(), pid);

                // Le parent doit fermer les sockets pour lui-même et libérer la mémoire
                // Ces sockets sont maintenus ouverts par le processus enfant.
                close(c1_partie->socket);
                close(c2_partie->socket);
                free(c1_partie);
                free(c2_partie);
                fileAttente[0] = NULL;
                fileAttente[1] = NULL;
            }
        }
        else if (c1_partie != NULL && c1_partie->pret == 1)
        {
            // Un seul client est prêt, envoyer un message d'attente
            envoyerPacket(c1_partie->socket, 0, "EN_ATTENTE_ADVERSAIRE");
            c1_partie->pret = 0; 
        }
        else if (c2_partie != NULL && c2_partie->pret == 1)
        {
             // Un seul client est prêt, envoyer un message d'attente
            envoyerPacket(c2_partie->socket, 0, "EN_ATTENTE_ADVERSAIRE");
            c2_partie->pret = 0; 
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