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
#include <pthread.h>

#define PORT 5000
#define LG_MESSAGE 256
#define LISTE_MOTS "../../assets/mots.txt"

/* --------------------------------------------------------------------------
/*                            Création de la socket
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
/*                               Mot aléatoire                                 */
/* -------------------------------------------------------------------------- */

char *creationMot()
{
    FILE *f = fopen(LISTE_MOTS, "r");
    if (!f)
    {
        perror("Erreur ouverture fichier mots");
        exit(EXIT_FAILURE);
    }

    static char mot[LG_MESSAGE];
    char ligne[LG_MESSAGE];
    int nombreMots = 0;

    while (fgets(ligne, LG_MESSAGE, f))
        nombreMots++;

    if (nombreMots == 0)
    {
        fprintf(stderr, "Erreur : fichier mots vide.\n");
        fclose(f);
        exit(EXIT_FAILURE);
    }

    int index = rand() % nombreMots;
    rewind(f);

    for (int i = 0; i <= index; i++)
        fgets(mot, LG_MESSAGE, f);

    mot[strcspn(mot, "\n")] = '\0';
    fclose(f);

    return mot;
}

void deconnexion(int socketDialogue)
{
    close(socketDialogue);
    printf("Connexion fermée.\n");
}

void envoyerMessage(int socketDialogue, const char *message)
{
    int nb = send(socketDialogue, message, strlen(message), 0);
    if (nb < 0)
    {
        perror("send");
        exit(EXIT_FAILURE);
    }
}

int jeuDuPendu(int socketDialogue)
{
    char *motADeviner = creationMot();
    int longueurMot = strlen(motADeviner);

    char lettresDevinees[LG_MESSAGE] = {0};
    int essaisRestants = 10;
    int lettresTrouvees = 0;

    char motCache[LG_MESSAGE];

    printf("Nouveau jeu du pendu ! Mot = %s (%d lettres)\n", motADeviner, longueurMot);

    /* 1) Le serveur confirme le début */
    /* Attention, n'envoyer que lorsque 2 clients sont connecté*/
    envoyerMessage(socketDialogue, "start x");

    while (essaisRestants > 0 && lettresTrouvees < longueurMot)
    {
        /* Construction du mot caché */
        memset(motCache, 0, LG_MESSAGE);

        for (int i = 0; i < longueurMot; i++)
        {
            if (strchr(lettresDevinees, motADeviner[i]))
            {
                strncat(motCache, &motADeviner[i], 1);
                strncat(motCache, " ", 1);
            }
            else
            {
                strcat(motCache, "_ ");
            }
        }

        /* Envoie le mot masqué */
        envoyerMessage(socketDialogue, motCache);

        /* Envoie le nombre d’essais */
        char essaisStr[8];
        sprintf(essaisStr, "%d", essaisRestants);
        sleep(1);
        envoyerMessage(socketDialogue, essaisStr);

        /* Réception d’une lettre */
        char buffer[16];
        int lus = recv(socketDialogue, buffer, sizeof(buffer), 0);

        if (lus <= 0)
        {
            printf("Client déconnecté.\n");
            // Indiquer à l'appelant que le client s'est déconnecté
            return 0;
        }

        buffer[strcspn(buffer, "\n")] = 0; // clean \n
        char lettre = buffer[0];

        printf("Lettre reçue : %c\n", lettre);

        /* Lettre déjà jouée */
        if (strchr(lettresDevinees, lettre))
        {
            envoyerMessage(socketDialogue, "Lettre déjà devinée");
            continue;
        }

        /* Ajout */
        strncat(lettresDevinees, &lettre, 1);

        /* Bonne ou mauvaise lettre ? */
        if (strchr(motADeviner, lettre) && (essaisRestants > 0 && lettresTrouvees < longueurMot))
        {
            for (int i = 0; i < longueurMot; i++)
                if (motADeviner[i] == lettre)
                    lettresTrouvees++;

            envoyerMessage(socketDialogue, "Bonne lettre !");
        }
        else if (essaisRestants > 0 && lettresTrouvees < longueurMot)
        {
            essaisRestants--;
            envoyerMessage(socketDialogue, "Mauvaise lettre");
        }
    }

    envoyerMessage(socketDialogue, "END");
    sleep(1);
    /* Fin du jeu */
    if (lettresTrouvees == longueurMot)
    {
        envoyerMessage(socketDialogue, "VICTOIRE");
        printf("Le client a gagné !\n");
    }
    else
    {
        envoyerMessage(socketDialogue, "DEFAITE");
        printf("Le client a perdu. Mot : %s\n", motADeviner);
    }

    return 1; /* partie terminée normalement, garder la connexion ouverte */
}

/* -------------------------------------------------------------------------- */
/*                          Structure pour les infos client                    */
/* -------------------------------------------------------------------------- */
typedef struct {
    int socketDialogue;
    struct sockaddr_in client;
} ClientInfo;

/* -------------------------------------------------------------------------- */
/*                          Gestion des messages client                        */
/* -------------------------------------------------------------------------- */
void *gererClient(void *arg)
{
    ClientInfo *info = (ClientInfo *)arg;
    int socketDialogue = info->socketDialogue;

    while (1)
    {
        int res = recevoirMessage(socketDialogue);
        if (res <= 0)
        {
            deconnexion(socketDialogue);
            break;
        }
    }

    free(info);
    pthread_exit(NULL);
}

int recevoirMessage(int socketDialogue)
{
    char messageRecu[LG_MESSAGE];
    memset(messageRecu, 0, LG_MESSAGE);

    int lus = recv(socketDialogue, messageRecu, LG_MESSAGE, 0);

    if (lus <= 0)
    {
        return 0; // le client s'est déconnecté
    }

    // Récupération de l'adresse IP du client
    struct sockaddr_in addrClient;
    socklen_t len = sizeof(addrClient);

    if (getpeername(socketDialogue, (struct sockaddr *)&addrClient, &len) == -1)
    {
        perror("getpeername");
        return lus;
    }

    char *ipClient = inet_ntoa(addrClient.sin_addr);

    printf("%s a envoyé : %s (%d octets)\n", ipClient, messageRecu, lus);

    /* Gestion des commandes :
       - "start x" : démarre le jeu (comportement existant)
       - "exit"    : le client demande la fermeture -> on ferme côté serveur
       - autre     : on informe le client que ce n'est pas une commande */
    if (strcmp(messageRecu, "start x") == 0)
    {
        printf("Commande spéciale reçue : démarrage du jeu.\n");
        /* Lance le jeu ; si le client se déconnecte pendant la partie, propager 0 */
        int res = jeuDuPendu(socketDialogue);
        if (res == 0)
        {
            /* le client s'est déconnecté pendant la partie */
            return 0;
        }
        /* partie terminée normalement : ne pas fermer la socket, permettre relance */
        return lus;
    }
    else if (strcmp(messageRecu, "exit") == 0)
    {
        printf("Client %s a demandé la fermeture.\n", ipClient);
        envoyerMessage(socketDialogue, "Au revoir");
        close(socketDialogue);
        return 0;
    }
    else
    {
        envoyerMessage(socketDialogue, "Commande inconnue. Envoyez 'start x' ou 'exit'.");
    }

    return lus;
}

/* -------------------------------------------------------------------------- */
/*  Ajout : état global de matchmaking / jeu                                    */
/* -------------------------------------------------------------------------- */
static int waiting_socket = -1;                      /* socket du 1er joueur en attente */
static struct sockaddr_in waiting_addr;              /* adresse du 1er joueur */
static int game_in_progress = 0;

/* -------------------------------------------------------------------------- */
/*  Fonction utilitaire : envoi sûr (avec gestion d'erreur sans exit brut)     */
/* -------------------------------------------------------------------------- */
static int envoyerMessage_noexit(int socketDialogue, const char *message)
{
    int nb = send(socketDialogue, message, strlen(message), 0);
    if (nb < 0)
    {
        perror("send");
        return -1;
    }
    return nb;
}

/* -------------------------------------------------------------------------- */
/*  Jeu entre 2 joueurs : mêmes règles que V0, mais états séparés par joueur   */
/*  sock1 = premier connecté (attend), sock2 = deuxième connecté (joue 1er)    */
/* -------------------------------------------------------------------------- */
static void jeuDuPenduMultijoueur(int sock1, int sock2)
{
    char *motADeviner = creationMot();
    int longueurMot = strlen(motADeviner);

    /* états séparés pour chaque joueur */
    char lettresDevinees1[LG_MESSAGE] = {0};
    char lettresDevinees2[LG_MESSAGE] = {0};
    int essais1 = 10;
    int essais2 = 10;
    int trouve1 = 0;
    int trouve2 = 0;

    char motCache[LG_MESSAGE];
    char buffer[LG_MESSAGE];

    printf("Nouvelle partie multijoueur : mot = %s (%d lettres)\n", motADeviner, longueurMot);

    /* Réveiller les deux clients (ils attendent la confirmation) en envoyant "start x" */
    if (envoyerMessage_noexit(sock2, "start x") < 0 || envoyerMessage_noexit(sock1, "start x") < 0)
    {
        /* si l'un des envois échoue, on ferme les sockets et termine */
        close(sock1);
        close(sock2);
        return;
    }

    /* active = 2 (second connecté joue en premier), puis alterner */
    int active = 2;

    while (1)
    {
        int curSock = (active == 1) ? sock1 : sock2;
        char *curLettres = (active == 1) ? lettresDevinees1 : lettresDevinees2;
        int *curEssais = (active == 1) ? &essais1 : &essais2;
        int *curTrouve = (active == 1) ? &trouve1 : &trouve2;

        /* Construire mot masqué pour le joueur actif */
        memset(motCache, 0, sizeof(motCache));
        for (int i = 0; i < longueurMot; i++)
        {
            if (strchr(curLettres, motADeviner[i]))
            {
                strncat(motCache, &motADeviner[i], 1);
                strncat(motCache, " ", 1);
            }
            else
            {
                strcat(motCache, "_ ");
            }
        }

        /* Envoyer mot masqué au joueur actif */
        if (envoyerMessage_noexit(curSock, motCache) < 0)
            goto cleanup_disconnect;

        /* Envoyer essais restants au joueur actif */
        char essaisStr[8];
        sprintf(essaisStr, "%d", *curEssais);
        sleep(1);
        if (envoyerMessage_noexit(curSock, essaisStr) < 0)
            goto cleanup_disconnect;

        /* Attendre la lettre du joueur actif */
        memset(buffer, 0, sizeof(buffer));
        int lus = recv(curSock, buffer, sizeof(buffer) - 1, 0);
        if (lus <= 0)
        {
            printf("Un joueur s'est déconnecté pendant la partie.\n");
            goto cleanup_disconnect;
        }
        buffer[strcspn(buffer, "\n")] = 0;
        char lettre = buffer[0];
        printf("Joueur %d a envoyé la lettre : %c\n", active, lettre);

        /* Lettre déjà jouée ? */
        if (strchr(curLettres, lettre))
        {
            if (envoyerMessage_noexit(curSock, "Lettre déjà devinée") < 0)
                goto cleanup_disconnect;
            /* même joueur rejoue : on ne change pas active */
            continue;
        }

        /* Ajout à ses lettres devinées */
        strncat(curLettres, &lettre, 1);

        /* Vérifier si bonne lettre pour le mot (état individuel) */
        if (strchr(motADeviner, lettre))
        {
            /* compter occurrences */
            int added = 0;
            for (int i = 0; i < longueurMot; i++)
                if (motADeviner[i] == lettre)
                    added++;
            *curTrouve += added;

            if (envoyerMessage_noexit(curSock, "Bonne lettre !") < 0)
                goto cleanup_disconnect;
        }
        else
        {
            (*curEssais)--;
            if (envoyerMessage_noexit(curSock, "Mauvaise lettre") < 0)
                goto cleanup_disconnect;
        }

        /* Vérifier victoire */
        if (*curTrouve >= longueurMot)
        {
            /* joueur actif gagne */
            if (envoyerMessage_noexit(curSock, "END") < 0)
                goto cleanup_disconnect;
            sleep(1);
            if (envoyerMessage_noexit(curSock, "VICTOIRE") < 0)
                goto cleanup_disconnect;

            /* informer l'autre joueur qu'il a perdu */
            int otherSock = (active == 1) ? sock2 : sock1;
            if (envoyerMessage_noexit(otherSock, "END") >= 0)
            {
                sleep(1);
                envoyerMessage_noexit(otherSock, "DEFAITE");
            }

            printf("Joueur %d a gagné la partie.\n", active);
            break;
        }

        /* Si les deux joueurs n'ont plus d'essais -> fin */
        if (essais1 <= 0 && essais2 <= 0)
        {
            /* déclarer les deux perdants */
            envoyerMessage_noexit(sock1, "END");
            sleep(1);
            envoyerMessage_noexit(sock1, "DEFAITE");
            envoyerMessage_noexit(sock2, "END");
            sleep(1);
            envoyerMessage_noexit(sock2, "DEFAITE");
            printf("Les deux joueurs ont épuisé leurs essais. Fin de la partie.\n");
            break;
        }

        /* changer de joueur pour le tour suivant */
        active = (active == 1) ? 2 : 1;
    }

cleanup_disconnect:
    /* fermeture des sockets en fin de partie */
    close(sock1);
    close(sock2);
    return;
}

/* -------------------------------------------------------------------------- */
/*  Remplacement : boucleServeur et logique de matchmaking                    */
/* -------------------------------------------------------------------------- */
void boucleServeur(int socketEcoute)
{
    int socketDialogue;
    socklen_t longueurAdresse = sizeof(struct sockaddr_in);
    struct sockaddr_in client;

    while (1)
    {
        printf("En attente d’un client...\n");

        socketDialogue = accept(socketEcoute,
                                (struct sockaddr *)&client,
                                &longueurAdresse);

        if (socketDialogue < 0)
        {
            perror("accept");
            continue;
        }

        printf("Connexion de %s\n", inet_ntoa(client.sin_addr));

        /* Lire la première commande du client (bloquant) */
        char messageRecu[LG_MESSAGE];
        memset(messageRecu, 0, sizeof(messageRecu));
        int lus = recv(socketDialogue, messageRecu, sizeof(messageRecu) - 1, 0);

        if (lus <= 0)
        {
            printf("Client %s déconnecté immédiatement.\n", inet_ntoa(client.sin_addr));
            close(socketDialogue);
            continue;
        }
        messageRecu[strcspn(messageRecu, "\n")] = 0;
        printf("%s a envoyé : %s (%d octets)\n", inet_ntoa(client.sin_addr), messageRecu, lus);

        if (strcmp(messageRecu, "start x") == 0)
        {
            /* Demande de matchmaking */
            if (game_in_progress)
            {
                /* refuser nouvelle demande pendant qu'une partie est en cours */
                envoyerMessage_noexit(socketDialogue, "Serveur occupé. Réessayez plus tard.");
                close(socketDialogue);
                continue;
            }

            if (waiting_socket == -1)
            {
                /* Premier joueur : le mettre en attente (ne rien renvoyer maintenant) */
                waiting_socket = socketDialogue;
                waiting_addr = client;
                printf("Premier joueur (%s) en attente d'un adversaire...\n", inet_ntoa(client.sin_addr));
                /* Le client est bloqué dans son recv côté client, en attente de 'start x' du serveur */
                continue;
            }
            else
            {
                /* Deuxième joueur : on lance la partie entre waiting_socket et socketDialogue */
                int sock1 = waiting_socket;         /* premier connecté (attend) */
                int sock2 = socketDialogue;         /* second connecté (joue 1er) */
                struct sockaddr_in addr1 = waiting_addr;
                printf("Match trouvé : %s (1er) vs %s (2e)\n", inet_ntoa(addr1.sin_addr), inet_ntoa(client.sin_addr));

                /* Marquer partie en cours */
                game_in_progress = 1;
                waiting_socket = -1;

                /* Lancer la partie (bloquant, le serveur ne gère qu'une partie à la fois) */
                jeuDuPenduMultijoueur(sock1, sock2);

                /* Partie terminée, libérer l'état */
                game_in_progress = 0;
                waiting_socket = -1;
                printf("Partie terminée. Retour à l'attente de nouveaux joueurs.\n");
                continue;
            }
        }
        else if (strcmp(messageRecu, "exit") == 0)
        {
            envoyerMessage_noexit(socketDialogue, "Au revoir");
            close(socketDialogue);
            continue;
        }
        else
        {
            envoyerMessage_noexit(socketDialogue, "Commande inconnue. Envoyez 'start x' ou 'exit'.");
            close(socketDialogue);
            continue;
        }
    }
}

/* -------------------------------------------------------------------------- */
/*                                   MAIN                                      */
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
