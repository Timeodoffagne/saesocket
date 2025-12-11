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
	 int owns_socket;      /* 1 = le handler doit fermer la socket en quittant, 0 = non */
	 int matched;         /* 1 = ce client a été apparié et ne doit plus lire la socket */
} ClientInfo;

/* -------------------------------------------------------------------------- */
/*                          State matchmaking / synchronisation                */
/* -------------------------------------------------------------------------- */
static ClientInfo *waiting_info = NULL; /* pointeur vers ClientInfo du 1er joueur en attente */
static int game_in_progress = 0;
static pthread_mutex_t match_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t match_cond = PTHREAD_COND_INITIALIZER;

/* -------------------------------------------------------------------------- */
/*  utilitaire : envoi sans exit (pour threads)                               */
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
/*  Jeu multijoueur (thread) : état partagé des lettres découvertes mais      */
/*  essais séparés. Le thread prend possession des sockets et les ferme.     */
/* -------------------------------------------------------------------------- */
typedef struct {
	 int sock1;
	 int sock2;
} GameArgs;

static void *jeuDuPenduMultijoueur_thread(void *arg)
{
	 GameArgs *g = (GameArgs *)arg;
	 int sock1 = g->sock1;
	 int sock2 = g->sock2;
	 free(g);

	 char *motADeviner = creationMot();
	 int longueurMot = strlen(motADeviner);

	 /* état partagé des lettres découvertes */
	 char lettresDevinees[LG_MESSAGE] = {0};
	 int lettresTrouvees = 0;

	 int essais1 = 10, essais2 = 10;

	 char motCache[LG_MESSAGE];
	 char buffer[LG_MESSAGE];

	 printf("Nouvelle partie multijoueur : mot = %s (%d lettres)\n", motADeviner, longueurMot);

	 /* envoyer "start x" aux deux joueurs (second joue en premier selon spec) */
	 if (envoyerMessage_noexit(sock2, "start x") < 0 || envoyerMessage_noexit(sock1, "start x") < 0)
	 {
		 close(sock1); close(sock2);
		 goto end;
	 }

	 int active = 2; /* 2 joue en premier */
	 while (1)
	 {
		 int curSock = (active == 1) ? sock1 : sock2;
		 int *curEssais = (active == 1) ? &essais1 : &essais2;

		 /* construire mot masqué à partir de lettresDevinees (partagé) */
		 memset(motCache, 0, sizeof(motCache));
		 for (int i = 0; i < longueurMot; ++i)
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

		 if (envoyerMessage_noexit(curSock, motCache) < 0) break;

		 char essaisStr[8];
		 sprintf(essaisStr, "%d", *curEssais);
		 sleep(1);
		 if (envoyerMessage_noexit(curSock, essaisStr) < 0) break;

		 /* attendre lettre du joueur actif */
		 memset(buffer, 0, sizeof(buffer));
		 int lus = recv(curSock, buffer, sizeof(buffer) - 1, 0);
		 if (lus <= 0)
		 {
			 printf("Un joueur s'est déconnecté pendant la partie.\n");
			 break;
		 }
		 buffer[strcspn(buffer, "\n")] = 0;
		 char lettre = buffer[0];
		 printf("Joueur %d a envoyé : %c\n", active, lettre);

		 /* lettre déjà découverte ? (global) */
		 if (strchr(lettresDevinees, lettre))
		 {
			 if (envoyerMessage_noexit(curSock, "Lettre déjà devinée") < 0) break;
			 continue; /* même joueur rejoue */
		 }

		 /* ajouter à l'état global */
		 strncat(lettresDevinees, &lettre, 1);

		 /* compter occurrences dans le mot */
		 int occur = 0;
		 for (int i = 0; i < longueurMot; ++i)
			 if (motADeviner[i] == lettre)
				 ++occur;

		 if (occur > 0)
		 {
			 lettresTrouvees += occur;
			 if (envoyerMessage_noexit(curSock, "Bonne lettre !") < 0) break;
		 }
		 else
		 {
			 (*curEssais)--;
			 if (envoyerMessage_noexit(curSock, "Mauvaise lettre") < 0) break;
		 }

		 /* vérifier victoire : si la découverte globale couvre le mot, le joueur actif gagne */
		 if (lettresTrouvees >= longueurMot)
		 {
			 if (envoyerMessage_noexit(curSock, "END") >= 0) { sleep(1); envoyerMessage_noexit(curSock, "VICTOIRE"); }
			 int other = (active == 1) ? sock2 : sock1;
			 if (envoyerMessage_noexit(other, "END") >= 0) { sleep(1); envoyerMessage_noexit(other, "DEFAITE"); }
			 printf("Joueur %d a gagné.\n", active);
			 break;
		 }

		 /* si les deux ont épuisé leurs essais -> fin */
		 if (essais1 <= 0 && essais2 <= 0)
		 {
			 envoyerMessage_noexit(sock1, "END"); sleep(1); envoyerMessage_noexit(sock1, "DEFAITE");
			 envoyerMessage_noexit(sock2, "END"); sleep(1); envoyerMessage_noexit(sock2, "DEFAITE");
			 printf("Les deux joueurs ont perdu (essais épuisés).\n");
			 break;
		 }

		 /* alterner joueur */
		 active = (active == 1) ? 2 : 1;
	 }

	 /* fermer sockets (le thread de jeu prend la possession) */
	 close(sock1);
	 close(sock2);

 end:
	 /* marquer fin de partie */
	 pthread_mutex_lock(&match_mutex);
	 game_in_progress = 0;
	 pthread_mutex_unlock(&match_mutex);
	 return NULL;
}

/* -------------------------------------------------------------------------- */
/*  Handler par client : lit en boucle et répond. Sur "start x" effectue     */
/*  le matchmaking (attente ou appariement). Si apparié, le handler quitte   */
/*  sans fermer la socket (le thread de jeu s'occupe des sockets).          */
/* -------------------------------------------------------------------------- */
void *client_handler(void *arg)
{
	 ClientInfo *info = (ClientInfo *)arg;
	 int sock = info->socketDialogue;
	 info->owns_socket = 1;
	 info->matched = 0;

	 char buffer[LG_MESSAGE];

	 while (1)
	 {
		 memset(buffer, 0, sizeof(buffer));
		 int lus = recv(sock, buffer, sizeof(buffer) - 1, 0);
		 if (lus <= 0) break;
		 buffer[strcspn(buffer, "\n")] = 0;

		 if (strcmp(buffer, "start x") == 0)
		 {
			 pthread_mutex_lock(&match_mutex);
			 if (game_in_progress)
			 {
				 envoyerMessage_noexit(sock, "Serveur occupé. Réessayez plus tard.");
				 pthread_mutex_unlock(&match_mutex);
				 continue;
			 }

			 if (waiting_info == NULL)
			 {
				 /* premier joueur : mettre en attente (le client restera bloqué jusqu'au match) */
				 waiting_info = info;
				 info->owns_socket = 1; /* le jeu prendra la socket plus tard, pour l'instant handler garde la respo */
				 printf("Premier joueur %s en attente d'un adversaire...\n", inet_ntoa(info->client.sin_addr));

				 /* attendre signal de matchmaking */
				 while (info->matched == 0 && game_in_progress == 0)
					 pthread_cond_wait(&match_cond, &match_mutex);

				 /* si on est réveillé parce qu'on a été apparié, le jeu thread prendra la socket */
				 pthread_mutex_unlock(&match_mutex);
				 break; /* quitter handler sans fermer la socket (jeu prendra la suite) */
			 }
			 else
			 {
				 /* second joueur : lancer la partie avec waiting_info */
				 ClientInfo *first = waiting_info;
				 waiting_info = NULL;
				 game_in_progress = 1;

				 /* créer thread de jeu qui prend possession des sockets */
				 GameArgs *g = malloc(sizeof(GameArgs));
				 if (!g)
				 {
					 perror("malloc");
					 /* libérer situation */
					 game_in_progress = 0;
					 pthread_mutex_unlock(&match_mutex);
					 continue;
				 }
				 g->sock1 = first->socketDialogue;
				 g->sock2 = sock;

				 /* marquer que les handlers ne doivent pas fermer les sockets */
				 first->owns_socket = 0;
				 info->owns_socket = 0;
				 first->matched = 1;

				 /* démarrer le thread de jeu */
				 pthread_t gt;
				 pthread_create(&gt, NULL, jeuDuPenduMultijoueur_thread, (void *)g);
				 pthread_detach(gt);

				 /* réveiller le premier handler pour qu'il sorte proprement */
				 pthread_cond_signal(&match_cond);
				 pthread_mutex_unlock(&match_mutex);

				 /* quitter également le handler sans fermer la socket (jeu thread prend la suite) */
				 break;
			 }
		 }
		 else if (strcmp(buffer, "exit") == 0)
		 {
			 envoyerMessage_noexit(sock, "Au revoir");
			 break;
		 }
		 else
		 {
			 /* réponse par défaut : garder la connexion ouverte, permettre échanges illimités */
			 envoyerMessage_noexit(sock, "Commande inconnue. Envoyez 'start x' pour jouer ou 'exit'.");
		 }
	 }

	 /* fin du handler : fermer la socket si il en est responsable */
	 if (info->owns_socket)
		 close(info->socketDialogue);

	 free(info);
	 return NULL;
}

/* -------------------------------------------------------------------------- */
/*  Remplacement de boucleServeur : accepte et spawn un thread client par   */
/*  connexion. Le handshake initial (lecture) est transféré au handler.      */
/* -------------------------------------------------------------------------- */
void boucleServeur(int socketEcoute)
{
	 while (1)
	 {
		 printf("En attente d’un client...\n");
		 struct sockaddr_in client;
		 socklen_t longueurAdresse = sizeof(client);
		 int socketDialogue = accept(socketEcoute, (struct sockaddr *)&client, &longueurAdresse);
		 if (socketDialogue < 0)
		 {
			 perror("accept");
			 continue;
		 }

		 ClientInfo *info = malloc(sizeof(ClientInfo));
		 if (!info)
		 {
			 perror("malloc");
			 close(socketDialogue);
			 continue;
		 }
		 info->socketDialogue = socketDialogue;
		 info->client = client;
		 info->owns_socket = 1;
		 info->matched = 0;

		 pthread_t tid;
		 if (pthread_create(&tid, NULL, client_handler, (void *)info) != 0)
		 {
			 perror("pthread_create");
			 free(info);
			 close(socketDialogue);
			 continue;
		 }
		 pthread_detach(tid);
	 }
}
