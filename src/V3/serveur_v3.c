// Définir _POSIX_C_SOURCE avant tous les includes
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <string.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <sys/select.h>
#include <time.h>
#include <signal.h>

#define PORT 5000
#define LG_MESSAGE 256
#define MAX_CLIENTS_ATTENTE 20

typedef struct
{
    int destinataire;
    char message[LG_MESSAGE];
} Packet;

typedef struct
{
    int socket;
    struct sockaddr_in addr;
    time_t connexion_time;
} ClientEnAttente;

typedef struct
{
    pid_t pid;
    int id_partie;
    time_t debut;
} Partie;

// Variables globales
ClientEnAttente clients_attente[MAX_CLIENTS_ATTENTE];
int nb_clients_attente = 0;
Partie parties[100];
int nb_parties = 0;
int prochain_id_partie = 1;
volatile sig_atomic_t enfant_termine = 0;

void sigchld_handler(int sig)
{
    (void)sig;
    enfant_termine = 1;
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

    if (listen(*socketEcoute, 10) < 0)
    {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    printf("🚀 Serveur V3 démarré : écoute sur le port %d\n", PORT);
    printf("📊 Architecture: Serveur + Programme externe 'partie_v3'\n");
    printf("═══════════════════════════════════════════════════════\n\n");
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

    return send(sock, buffer, sizeof(Packet), 0);
}

/* -------------------------------------------------------------------------- */
void nettoyerPartiesTerminees()
{
    int status;
    pid_t pid;

    while ((pid = waitpid(-1, &status, WNOHANG)) > 0)
    {
        for (int i = 0; i < nb_parties; i++)
        {
            if (parties[i].pid == pid)
            {
                time_t duree = time(NULL) - parties[i].debut;
                printf("✓ Partie %d terminée (PID %d, durée: %ld sec)\n",
                       parties[i].id_partie, pid, duree);

                for (int j = i; j < nb_parties - 1; j++)
                {
                    parties[j] = parties[j + 1];
                }
                nb_parties--;
                break;
            }
        }
    }

    enfant_termine = 0;
}

/* -------------------------------------------------------------------------- */
void lancerPartieExterne(int sock_c1, int sock_c2, 
                         struct sockaddr_in addr_c1, 
                         struct sockaddr_in addr_c2)
{
    int id = prochain_id_partie++;

    pid_t pid = fork();

    if (pid < 0)
    {
        perror("fork");
        close(sock_c1);
        close(sock_c2);
        return;
    }
    else if (pid == 0)
    {
        // PROCESSUS ENFANT : exécuter le programme externe

        // Convertir les sockets en chaînes pour passer en argument
        char arg_sock1[16], arg_sock2[16], arg_id[16];
        snprintf(arg_sock1, sizeof(arg_sock1), "%d", sock_c1);
        snprintf(arg_sock2, sizeof(arg_sock2), "%d", sock_c2);
        snprintf(arg_id, sizeof(arg_id), "%d", id);

        char arg_ip1[INET_ADDRSTRLEN], arg_ip2[INET_ADDRSTRLEN];
        strcpy(arg_ip1, inet_ntoa(addr_c1.sin_addr));
        strcpy(arg_ip2, inet_ntoa(addr_c2.sin_addr));

        // Chemins possibles pour le programme externe
        const char *chemins[] = {
            "./partie_v3",           // Dans le répertoire courant
            "../../build/partie_v3", // Dans build/
            "../build/partie_v3",
            "./build/partie_v3"
        };

        for (int i = 0; i < 4; i++)
        {
            execl(chemins[i], "partie_v3", 
                  arg_id, arg_sock1, arg_sock2, arg_ip1, arg_ip2, 
                  (char *)NULL);
        }

        // Si on arrive ici, execl a échoué
        perror("execl partie_v3");
        fprintf(stderr, "❌ Impossible de lancer partie_v3. Assurez-vous qu'il est compilé.\n");
        exit(EXIT_FAILURE);
    }
    else
    {
        // PROCESSUS PARENT : enregistrer la partie

        // IMPORTANT: Le parent doit fermer les sockets maintenant
        // car ils sont passés au processus enfant
        close(sock_c1);
        close(sock_c2);

        parties[nb_parties].pid = pid;
        parties[nb_parties].id_partie = id;
        parties[nb_parties].debut = time(NULL);
        nb_parties++;

        printf("🎮 Partie %d lancée (PID %d) : %s vs %s\n",
               id, pid, inet_ntoa(addr_c1.sin_addr), inet_ntoa(addr_c2.sin_addr));
        printf("📊 Parties actives: %d | Clients en attente: %d\n",
               nb_parties, nb_clients_attente);
    }
}

/* -------------------------------------------------------------------------- */
void ajouterClientAttente(int socket, struct sockaddr_in addr)
{
    if (nb_clients_attente >= MAX_CLIENTS_ATTENTE)
    {
        printf("⚠️  File d'attente pleine\n");
        char *msg = "Serveur saturé\n";
        send(socket, msg, strlen(msg), 0);
        close(socket);
        return;
    }

    clients_attente[nb_clients_attente].socket = socket;
    clients_attente[nb_clients_attente].addr = addr;
    clients_attente[nb_clients_attente].connexion_time = time(NULL);
    nb_clients_attente++;

    printf("➕ Client ajouté (%s) - En attente: %d\n",
           inet_ntoa(addr.sin_addr), nb_clients_attente);
}

/* -------------------------------------------------------------------------- */
void creerPartiesSiPossible()
{
    while (nb_clients_attente >= 2)
    {
        int sock_c1 = clients_attente[0].socket;
        int sock_c2 = clients_attente[1].socket;
        struct sockaddr_in addr_c1 = clients_attente[0].addr;
        struct sockaddr_in addr_c2 = clients_attente[1].addr;

        // Retirer ces clients de la file
        for (int i = 0; i < nb_clients_attente - 2; i++)
        {
            clients_attente[i] = clients_attente[i + 2];
        }
        nb_clients_attente -= 2;

        // Lancer le programme externe
        lancerPartieExterne(sock_c1, sock_c2, addr_c1, addr_c2);
    }
}

/* -------------------------------------------------------------------------- */
void afficherStats()
{
    printf("\n═══════════════════════════════════════════════════════\n");
    printf("📊 STATISTIQUES SERVEUR V3\n");
    printf("═══════════════════════════════════════════════════════\n");
    printf("Parties actives: %d\n", nb_parties);
    printf("Clients en attente: %d\n", nb_clients_attente);
    
    if (nb_parties > 0)
    {
        printf("\nParties en cours:\n");
        for (int i = 0; i < nb_parties; i++)
        {
            time_t duree = time(NULL) - parties[i].debut;
            printf("  - Partie %d (PID %d) : %ld sec\n",
                   parties[i].id_partie, parties[i].pid, duree);
        }
    }
    
    printf("═══════════════════════════════════════════════════════\n\n");
}

/* -------------------------------------------------------------------------- */
void boucleServeur(int socketEcoute)
{
    struct sigaction sa;
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    if (sigaction(SIGCHLD, &sa, NULL) == -1)
    {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }

    socklen_t longueurAdresse = sizeof(struct sockaddr_in);
    struct sockaddr_in clientAddr;

    printf("✅ Serveur prêt à accepter des connexions\n\n");

    while (1)
    {
        if (enfant_termine)
        {
            nettoyerPartiesTerminees();
        }

        creerPartiesSiPossible();

        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(socketEcoute, &readfds);
        FD_SET(STDIN_FILENO, &readfds);

        int maxfd = socketEcoute > STDIN_FILENO ? socketEcoute : STDIN_FILENO;

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

        if (FD_ISSET(socketEcoute, &readfds))
        {
            int new_socket = accept(socketEcoute, (struct sockaddr *)&clientAddr, &longueurAdresse);
            if (new_socket < 0)
            {
                perror("accept");
            }
            else
            {
                ajouterClientAttente(new_socket, clientAddr);
            }
        }

        if (FD_ISSET(STDIN_FILENO, &readfds))
        {
            char line[256];
            if (fgets(line, sizeof(line), stdin) == NULL)
            {
                printf("stdin fermé\n");
            }
            else
            {
                line[strcspn(line, "\n")] = 0;

                if (strcmp(line, "exit") == 0 || strcmp(line, "quit") == 0)
                {
                    printf("🛑 Arrêt du serveur...\n");

                    for (int i = 0; i < nb_clients_attente; i++)
                    {
                        close(clients_attente[i].socket);
                    }

                    for (int i = 0; i < nb_parties; i++)
                    {
                        kill(parties[i].pid, SIGTERM);
                    }

                    while (nb_parties > 0)
                    {
                        nettoyerPartiesTerminees();
                        sleep(100000);
                    }

                    printf("✅ Serveur arrêté\n");
                    break;
                }
                else if (strcmp(line, "stats") == 0)
                {
                    afficherStats();
                }
                else if (strcmp(line, "help") == 0)
                {
                    printf("\nCommandes:\n");
                    printf("  stats - Statistiques\n");
                    printf("  exit  - Arrêter\n");
                    printf("  help  - Aide\n\n");
                }
                else
                {
                    printf("Commande inconnue: '%s'\n", line);
                }
            }
        }
    }
}

/* -------------------------------------------------------------------------- */
int main()
{
    int socketEcoute;
    socklen_t longueurAdresse;
    struct sockaddr_in pointDeRencontreLocal;

    creationSocket(&socketEcoute, &longueurAdresse, &pointDeRencontreLocal);
    boucleServeur(socketEcoute);

    close(socketEcoute);
    return 0;
}