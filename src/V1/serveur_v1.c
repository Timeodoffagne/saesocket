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

/* nouvelle fonction utilitaire : envoie en verif d'erreur */
static void envoyerMessageSafe(int sock, const char *msg)
{
    if (send(sock, msg, strlen(msg), 0) < 0)
    {
        perror("send");
    }
}

/* construit le mot masqué à partir du mot et lettresDevinees */
static void construireMotCache(const char *mot, const char *lettresDevinees, char *motCache)
{
    motCache[0] = '\0';
    for (size_t i = 0; i < strlen(mot); ++i)
    {
        if (strchr(lettresDevinees, mot[i]))
        {
            strncat(motCache, &mot[i], 1);
            strcat(motCache, " ");
        }
        else
        {
            strcat(motCache, "_ ");
        }
    }
}

/* Partie pour deux joueurs : s1 = premier connecté, s2 = deuxième (joue en premier) */
static void jouerDeuxJoueurs(int s1, int s2)
{
    char *motADeviner = creationMot();
    int longueurMot = strlen(motADeviner);
    char lettresDevinees[LG_MESSAGE] = {0};
    int essaisRestants = 10;
    int lettresTrouvees = 0;
    char motCache[LG_MESSAGE];

    printf("Nouvelle partie V1 : mot = %s (%d lettres)\n", motADeviner, longueurMot);

    /* Informer les deux clients de leur rôle */
    char buf[LG_MESSAGE];
    snprintf(buf, sizeof(buf), "WAIT %d", longueurMot);
    envoyerMessageSafe(s1, buf);
    snprintf(buf, sizeof(buf), "START %d", longueurMot);
    envoyerMessageSafe(s2, buf);

    int active = s2; /* le 2e joueur commence */
    int other = s1;

    while (1)
    {
        /* Envoyer TURN à l'actif */
        envoyerMessageSafe(active, "TURN");

        /* Envoyer mot masqué + essais à l'actif */
        construireMotCache(motADeviner, lettresDevinees, motCache);
        envoyerMessageSafe(active, motCache);

        snprintf(buf, sizeof(buf), "%d", essaisRestants);
        envoyerMessageSafe(active, buf);

        /* Attendre lettre de l'actif (avec select pour détecter déconnexions) */
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(active, &rfds);
        struct timeval tv;
        tv.tv_sec = 120;
        tv.tv_usec = 0; /* timeout raisonnable */
        int sel = select(active + 1, &rfds, NULL, NULL, &tv);
        if (sel <= 0)
        {
            /* timeout ou erreur -> considérer comme déconnexion */
            fprintf(stderr, "Pas de réponse du joueur actif ou erreur.\n");
            envoyerMessageSafe(other, "OPP_DISCONNECT");
            break;
        }

        if (FD_ISSET(active, &rfds))
        {
            char lettreBuf[LG_MESSAGE];
            int n = recv(active, lettreBuf, sizeof(lettreBuf) - 1, 0);
            if (n <= 0)
            {
                /* déconnexion de l'actif */
                perror("recv");
                envoyerMessageSafe(other, "OPP_DISCONNECT");
                break;
            }
            lettreBuf[n] = '\0';
            lettreBuf[strcspn(lettreBuf, "\n")] = '\0';
            char lettre = lettreBuf[0];

            printf("Joueur %s a joué la lettre : %c\n", inet_ntoa(((struct sockaddr_in){0}).sin_addr), lettre);

            /* Lettre déjà jouée ? */
            if (strchr(lettresDevinees, lettre))
            {
                envoyerMessageSafe(active, "Lettre déjà devinée");
                /* on ne change pas le tour (on pourrait laisser l'autre jouer) -> on passe au tour suivant */
            }
            else
            {
                strncat(lettresDevinees, &lettre, 1);
                if (strchr(motADeviner, lettre))
                {
                    int occurrences = 0;
                    for (int i = 0; i < longueurMot; ++i)
                        if (motADeviner[i] == lettre)
                            occurrences++;
                    lettresTrouvees += occurrences;
                    envoyerMessageSafe(active, "Bonne lettre !");
                }
                else
                {
                    essaisRestants--;
                    envoyerMessageSafe(active, "Mauvaise lettre");
                }
            }

            /* Mettre à jour motCache et envoyer UPDATE à tous */
            construireMotCache(motADeviner, lettresDevinees, motCache);
            envoyerMessageSafe(s1, "UPDATE");
            envoyerMessageSafe(s1, motCache);
            snprintf(buf, sizeof(buf), "%d", essaisRestants);
            envoyerMessageSafe(s1, buf);

            envoyerMessageSafe(s2, "UPDATE");
            envoyerMessageSafe(s2, motCache);
            envoyerMessageSafe(s2, buf);

            /* Vérifier fin de partie */
            if (lettresTrouvees >= longueurMot)
            {
                /* active gagne */
                envoyerMessageSafe(active, "WIN");
                envoyerMessageSafe(other, "LOSE");
                printf("Le joueur sur socket %d a gagné.\n", active);
                break;
            }

            if (essaisRestants <= 0)
            {
                /* plus d'essais : tout le monde perd (on envoie DEFAITE aux deux) */
                envoyerMessageSafe(s1, "LOSE");
                envoyerMessageSafe(s2, "LOSE");
                printf("Plus d'essais restants. Fin de la partie.\n");
                break;
            }

            /* Alterner le tour */
            int tmp = active;
            active = other;
            other = tmp;
        }
    }

    /* Fermer les deux sockets de la partie */
    close(s1);
    close(s2);
    printf("Partie terminée, sockets fermées.\n");
}

/* -------------------------------------------------------------------------- */
/*                              Boucle principale                             */
/* -------------------------------------------------------------------------- */
void boucleServeur(int socketEcoute)
{
    while (1)
    {
        printf("En attente d’un client (1/2)...\n");
        struct sockaddr_in client1;
        socklen_t len = sizeof(client1);
        int s1 = accept(socketEcoute, (struct sockaddr *)&client1, &len);
        if (s1 < 0)
        {
            perror("accept");
            continue;
        }
        printf("Premier client connecté : %s\n", inet_ntoa(client1.sin_addr));

        printf("En attente d’un client (2/2)...\n");
        struct sockaddr_in client2;
        len = sizeof(client2);
        int s2 = accept(socketEcoute, (struct sockaddr *)&client2, &len);
        if (s2 < 0)
        {
            perror("accept (second)");
            close(s1);
            continue;
        }
        printf("Deuxième client connecté : %s\n", inet_ntoa(client2.sin_addr));

        /* Lancer la partie synchronisée pour s1 et s2 (bloquant) */
        jouerDeuxJoueurs(s1, s2);

        /* Après la partie, retour en attente d'une nouvelle paire */
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
