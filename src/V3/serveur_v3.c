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
#include <signal.h> // Pour la gestion des signaux (fork)
#include <wait.h>   // Pour waitpid (si SIG_IGN n'est pas utilisé)

#define PORT 5000
#define LG_MESSAGE 256
#define LISTE_MOTS "../../assets/mots.txt"
#define MAX_ESSAIS 10
#define MAX_CLIENTS_EN_ATTENTE 2 // Pour la file d'attente de la boucle principale

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

// Tableau des clients en attente de partie
ClientData *fileAttente[MAX_CLIENTS_EN_ATTENTE] = {NULL, NULL};


// =====================================================
// OUTILS COMMUNICATION (Identique à V2)
// =====================================================

void envoyerPacket(int sock, int source_id, const char *msg)
{
    Packet p;
    memset(&p, 0, sizeof(p));

    p.destinataire = source_id; // Le champ est utilisé pour identifier l'expéditeur lors du relais
    strncpy(p.message, msg, LG_MESSAGE - 1);
    p.message[LG_MESSAGE - 1] = '\0';

    char buffer[sizeof(Packet)];
    memset(buffer, 0, sizeof(buffer));

    int dest_net = htonl(p.destinataire);
    memcpy(buffer, &dest_net, sizeof(int));
    memcpy(buffer + sizeof(int), p.message, LG_MESSAGE);

    send(sock, buffer, sizeof(Packet), 0);
    printf("Serveur [%d] -> Client (%d): '%s'\n", getpid(), sock, msg);
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

    printf("Serveur [%d] <- Client (%d): Dest=%d | Message='%s'\n", getpid(), sock, p->destinataire, p->message);
    
    return rec;
}


// =====================================================
// OUTILS JEU (Identique à V2)
// =====================================================

char *choisirMotAleatoire()
{
    // Implementation identique à celle de serveur_v2.c (omise ici pour la concision)
    // ... assurez-vous que cette fonction est correctement implémentée et que
    // le fichier LISTE_MOTS est accessible.
    // Pour cet exemple, je retourne un mot par défaut.
    static char mot[LG_MESSAGE] = "PENDU";
    return mot; 
}

void initialiserMasque(const char *mot, char *masque)
{
    size_t len = strlen(mot);
    for (size_t i = 0; i < len; i++)
    {
        masque[i * 2] = '_';
        masque[i * 2 + 1] = ' ';
    }
    masque[len * 2] = '\0';
}


// =====================================================
// LOGIQUE JEU DE LA PARTIE (Nouveau conteneur pour la logique V2)
// =====================================================

void traiterJeu(ClientData *c1, ClientData *c2)
{
    char motSecret[LG_MESSAGE];
    char motMasque[LG_MESSAGE * 2];
    char lettresJouees[27];
    int essaisRestants = MAX_ESSAIS;
    int ret;
    Packet p;
    char feedback[LG_MESSAGE] = "Début de la partie.";
    
    // Rôles
    ClientData *maitreDuJeu = (c1->id == 1) ? c1 : c2;
    ClientData *devineur = (c1->id == 2) ? c1 : c2;

    // Phase 1 : Initialisation (C1 choisit/Sélectionne le mot)
    
    // Le serveur choisit le mot (pour simplifier la logique de V3)
    // NOTE: Si le mot doit être choisi par C1 (Maître du Jeu) comme en V2, 
    // il faudrait ici recevoir un message spécial de C1 contenant le mot. 
    // Gardons la logique V2 où C1 envoie READY_WORD.
    
    // Dans la V2, le motSecret est géré localement par C1, le serveur ne fait que relayer.
    // Puisque le serveur de la V3 doit être neutre, il faut supposer que C1 a déjà
    // envoyé son READY_WORD au serveur qui l'a relayé à C2. 
    
    // Pour que le processus enfant puisse jouer, il doit récupérer l'info initiale:
    
    // 1. Lire le READY_WORD (qui est déjà dans le buffer du client lors du "start")
    // Le serveur V3 ne fait que relayer. On commence donc la boucle de jeu.
    
    // Cependant, pour la REPLAY_START, le serveur parent donne les IDs.
    // On suppose que l'état initial (mot masqué et essais) a été géré
    // dans la phase d'initialisation du client V2.
    
    
    printf("Serveur [%d]: Début du jeu entre C%d (Maître) et C%d (Devineur).\n", 
           getpid(), maitreDuJeu->id, devineur->id);


    // Initialisation locale (pour la gestion des lettres déjà jouées)
    memset(lettresJouees, 0, sizeof(lettresJouees));
    
    // Boucle de Rejeu V2 (gestion dans le client V2)
    // Ici, nous gérons la boucle de jeu unique.
    
    // La difficulté ici est que le serveur V2/V3 n'est pas censé connaître le mot secret.
    // Le serveur V2 se contente de relayer la lettre de C2 à C1, et l'UPDATE de C1 à C2.
    
    while (1)
    {
        // 1. Attente de la lettre du devineur (C2)
        ret = recevoirPacket(devineur->socket, &p);
        if (ret <= 0) 
        {
            printf("Serveur [%d]: Devineur (C%d) déconnecté. Fin de partie.\n", getpid(), devineur->id);
            envoyerPacket(maitreDuJeu->socket, devineur->id, "PARTNER_DISCONNECTED");
            break;
        }

        // Vérification de la commande (REPLAY ou exit)
        if (strcmp(p.message, "REPLAY") == 0 || strcmp(p.message, "exit") == 0) {
            printf("Serveur [%d]: Commande %s de C%d interceptée.\n", getpid(), p.message, devineur->id);
            if (strcmp(p.message, "REPLAY") == 0) devineur->pret = 2;
            envoyerPacket(maitreDuJeu->socket, devineur->id, p.message);
            break;
        }

        // La commande est la lettre jouée. Relais vers C1.
        printf("Serveur [%d]: Relais de la lettre de C%d à C%d.\n", getpid(), devineur->id, maitreDuJeu->id);
        envoyerPacket(maitreDuJeu->socket, devineur->id, p.message);
        
        // 2. Attente de l'UPDATE de C1 (Maître du Jeu)
        ret = recevoirPacket(maitreDuJeu->socket, &p);
        if (ret <= 0)
        {
            printf("Serveur [%d]: Maître du Jeu (C%d) déconnecté. Fin de partie.\n", getpid(), maitreDuJeu->id);
            envoyerPacket(devineur->socket, maitreDuJeu->id, "PARTNER_DISCONNECTED");
            break;
        }

        // Relais de l'UPDATE (Mot masqué, essais, feedback, lettres jouées) vers C2
        printf("Serveur [%d]: Relais de l'état de C%d à C%d.\n", getpid(), maitreDuJeu->id, devineur->id);
        envoyerPacket(devineur->socket, maitreDuJeu->id, p.message);
        
        // C1 vérifie si la partie est terminée et envoie END_GAME après l'UPDATE
        if (strstr(p.message, "END_GAME:") != NULL || strstr(p.message, "UPDATE:") == p.message)
        {
            // Vérifier si C1 envoie END_GAME (la fin de partie a été déclenchée)
            if (strstr(p.message, "END_GAME:") != NULL)
            {
                // On est dans la boucle de fin de jeu, C1 envoie END_GAME, C2 doit le recevoir.
                // END_GAME est déjà dans le buffer car c'est le dernier message reçu.
            }
            else
            {
                // C1 envoie d'abord UPDATE. Attendons le message de fin de partie de C1
                // (qui peut être immédiat si la partie s'est terminée avec cette lettre).
                ret = recevoirPacket(maitreDuJeu->socket, &p);
                if (ret <= 0) {
                    printf("Serveur [%d]: Maître du Jeu (C%d) déconnecté après l'UPDATE. Fin de partie.\n", getpid(), maitreDuJeu->id);
                    envoyerPacket(devineur->socket, maitreDuJeu->id, "PARTNER_DISCONNECTED");
                    break;
                }
            }

            // Si c'est un message de fin de partie ou de rejeu de C1
            if (strstr(p.message, "END_GAME:") != NULL)
            {
                // Relais de END_GAME à C2
                envoyerPacket(devineur->socket, maitreDuJeu->id, p.message);
                
                // Attente des demandes de rejeu des deux clients (REPLAY)
                
                // Attente REPLAY de C1
                ret = recevoirPacket(maitreDuJeu->socket, &p);
                if (ret <= 0 || strcmp(p.message, "exit") == 0) {
                    printf("Serveur [%d]: C%d déconnecté/Exit après la partie. Fin de partie.\n", getpid(), maitreDuJeu->id);
                    envoyerPacket(devineur->socket, maitreDuJeu->id, "PARTNER_DISCONNECTED");
                    break;
                }
                if (strcmp(p.message, "REPLAY") == 0) maitreDuJeu->pret = 2;
                
                // Attente REPLAY de C2
                ret = recevoirPacket(devineur->socket, &p);
                if (ret <= 0 || strcmp(p.message, "exit") == 0) {
                    printf("Serveur [%d]: C%d déconnecté/Exit après la partie. Fin de partie.\n", getpid(), devineur->id);
                    envoyerPacket(maitreDuJeu->socket, devineur->id, "PARTNER_DISCONNECTED");
                    break;
                }
                if (strcmp(p.message, "REPLAY") == 0) devineur->pret = 2;
                
                // Si les deux veulent rejouer, le serveur parent doit les re-trier
                if (maitreDuJeu->pret == 2 && devineur->pret == 2)
                {
                    printf("Serveur [%d]: Les deux clients demandent REPLAY. Retour à la file d'attente...\n", getpid());
                    // Dans l'architecture fork, on ne peut pas vraiment retourner à la file d'attente du parent.
                    // On doit signaler au client de se reconnecter ou d'attendre l'inversement des rôles.
                    
                    // Dans la V2, c'est le serveur qui gère le REPLAY_START.
                    // Pour simuler cela et terminer proprement ce processus enfant, 
                    // on envoie REPLAY à C1 et C2. Le parent les mettra en attente.
                    
                    // Dans la V2, le REPLAY est géré par le parent, donc l'enfant doit juste se terminer.
                    // Le client V2 va renvoyer REPLAY au parent s'il le souhaite. 
                    // On sort simplement de la boucle de jeu pour que l'enfant se termine.
                }
                
                break; // Fin de la boucle de jeu
            }
        }
    }

    // Fin du processus enfant
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
    int client_index = 0;
    
    // Gérer les processus zombies (optionnel mais recommandé)
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
                    newClient->id = i + 1; // ID 1 ou 2 (sera réassigné par le parent)
                    newClient->pret = 0;
                    fileAttente[i] = newClient;
                    client_index = i;
                    placed = 1;
                    
                    // Envoyer l'ID au client (destinataire est l'ID, message est la bienvenue)
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
                if (strcmp(p.message, "REPLAY") == 0)
                {
                    client->pret = 2;
                    printf("Serveur [%d]: Client C%d demande un rejeu.\n", getpid(), client->id);
                }
            }
        }
        
        // =================================================
        // VÉRIFICATION D'UNE NOUVELLE PARTIE / REJEU
        // =================================================

        ClientData *c1_partie = fileAttente[0];
        ClientData *c2_partie = fileAttente[1];
        
        // Vérification : 2 clients en attente et les deux sont prêts (1) ou demandent rejeu (2)
        if (c1_partie != NULL && c2_partie != NULL)
        {
            int c1_ready = c1_partie->pret;
            int c2_ready = c2_partie->pret;
            
            int start_now = 0;
            int c1_new_id, c2_new_id;

            if (c1_ready == 1 && c2_ready == 1)
            {
                // Nouveau match. Roles assignés par position (C1=Maitre, C2=Devineur)
                c1_new_id = 1; c2_new_id = 2;
                start_now = 1;
            }
            else if (c1_ready == 2 && c2_ready == 2)
            {
                // Rejeu (Rôles inversés : C1(old) devient C2, C2(old) devient C1)
                c1_new_id = 2; c2_new_id = 1; 
                start_now = 1;
            }
            
            if (start_now)
            {
                // **********************************************
                // DÉMARRAGE DE LA PARTIE DANS UN PROCESSUS ENFANT
                // **********************************************
                pid_t pid = fork();

                if (pid < 0)
                {
                    perror("fork");
                    // Tenter de redémarrer (ou déconnecter) les clients
                    printf("Serveur [%d]: Échec du fork. Clients C%d et C%d restent en attente.\n", getpid(), c1_partie->id, c2_partie->id);
                    c1_partie->pret = 0;
                    c2_partie->pret = 0;
                }
                else if (pid == 0) // Processus enfant
                {
                    // L'enfant reçoit les copies des clients
                    // Il doit mettre à jour les ID selon les nouveaux rôles
                    ClientData *c1_child = c1_partie;
                    ClientData *c2_child = c2_partie;
                    
                    // Cloner les structures pour le processus enfant (important car le parent va les free/reset)
                    ClientData *client_maitre = (ClientData *)malloc(sizeof(ClientData));
                    ClientData *client_devineur = (ClientData *)malloc(sizeof(ClientData));
                    
                    if (c1_new_id == 1) {
                         memcpy(client_maitre, c1_child, sizeof(ClientData));
                         memcpy(client_devineur, c2_child, sizeof(ClientData));
                    } else {
                         memcpy(client_maitre, c2_child, sizeof(ClientData));
                         memcpy(client_devineur, c1_child, sizeof(ClientData));
                    }

                    client_maitre->id = 1;
                    client_devineur->id = 2;
                    
                    // Envoyer le message de confirmation/swap aux clients
                    if (c1_ready == 1) // Nouveau match
                    {
                        envoyerPacket(client_maitre->socket, client_maitre->id, "start");
                        envoyerPacket(client_devineur->socket, client_devineur->id, "start");
                    }
                    else // Rejeu (swap)
                    {
                        char msg_maitre[LG_MESSAGE];
                        char msg_devineur[LG_MESSAGE];
                        
                        // Envoyer REPLAY_START pour forcer la mise à jour des rôles dans le client V2
                        snprintf(msg_maitre, LG_MESSAGE, "REPLAY_START:%d", 1);
                        snprintf(msg_devineur, LG_MESSAGE, "REPLAY_START:%d", 2);
                        
                        envoyerPacket(client_maitre->socket, client_maitre->id, msg_maitre);
                        envoyerPacket(client_devineur->socket, client_devineur->id, msg_devineur);
                    }
                    
                    // L'enfant exécute la logique de jeu
                    traiterJeu(client_maitre, client_devineur); 
                    
                    // Note: traiterJeu appelle exit(EXIT_SUCCESS)
                    // (Les free() des structures sont dans traiterJeu)
                }
                else // Processus parent
                {
                    printf("Serveur [%d]: Partie créée (PID: %d). Nettoyage de la file d'attente.\n", getpid(), pid);

                    // Le parent doit fermer les sockets et libérer la mémoire des clients
                    // pour qu'ils ne soient gérés que par le processus enfant.
                    close(c1_partie->socket);
                    close(c2_partie->socket);
                    free(c1_partie);
                    free(c2_partie);
                    fileAttente[0] = NULL;
                    fileAttente[1] = NULL;
                }
            }
        }
        else if (c1_partie != NULL && c1_partie->pret == 1)
        {
            // Un seul client est prêt, envoyer un message d'attente
            envoyerPacket(c1_partie->socket, 0, "EN_ATTENTE_ADVERSAIRE");
            c1_partie->pret = 0; // Remettre à 0 pour ne pas saturer la console
        }
        else if (c2_partie != NULL && c2_partie->pret == 1)
        {
             // Un seul client est prêt, envoyer un message d'attente
            envoyerPacket(c2_partie->socket, 0, "EN_ATTENTE_ADVERSAIRE");
            c2_partie->pret = 0; // Remettre à 0 pour ne pas saturer la console
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

    // Autoriser la réutilisation des adresses (important pour le développement)
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

    if (listen(*socketEcoute, 2) < 0) // 2 : backlog max de connexions en attente
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