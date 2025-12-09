#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <string.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define PORT 5000
#define LG_MESSAGE 256
#define LISTE_MOTS "../assets/mots.txt"

/* -------------------------------------------------------------------------- */
/*                            Création de la socket                            */
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

void envoyerMessage(int socketDialogue, const char *message)
{
    int nb = send(socketDialogue, message, strlen(message), 0);
    if (nb < 0)
    {
        perror("send");
        exit(EXIT_FAILURE);
    }
}

void jeuDuPendu(int socketDialogue)
{
    char *motADeviner = creationMot();
    int longueurMot = strlen(motADeviner);
    char lettresDevinees[LG_MESSAGE] = {0};
    int essaisRestants = 10;
    int lettresTrouvees = 0;

    char motCache[LG_MESSAGE];

    printf("Nouveau jeu du pendu ! Mot de %d lettres.\n", longueurMot);

    envoyerMessage(socketDialogue, "start x");

    while (essaisRestants > 0 && lettresTrouvees < longueurMot)
    {
        /* --- Construction du mot caché --- */
        memset(motCache, 0, LG_MESSAGE);
        for (int i = 0; i < longueurMot; i++)
        {
            if (strchr(lettresDevinees, motADeviner[i]))
            {
                strncat(motCache, &motADeviner[i], 1);
            }
            else
            {
                strcat(motCache, "_");
            }
        }

        envoyerMessage(socketDialogue, motCache);

        /* --- Envoi du nombre d'essais restants --- */
        char essaisRestantsChar[4];
        sprintf(essaisRestantsChar, "%d", essaisRestants);
        sleep(0.5);
        
        envoyerMessage(socketDialogue, essaisRestantsChar);

        printf("En attente d'une lettre du client...\n");

        /* --- Réception de la lettre envoyée par le client --- */
        char lettre;
        int lus = recv(socketDialogue, &lettre, 1, 0);

        if (lus <= 0)
        {
            printf("Client déconnecté pendant le jeu.\n");
            return;
        }

        printf("Lettre reçue : %c\n", lettre);

        /* Si lettre déjà utilisée */
        if (strchr(lettresDevinees, lettre))
        {
            envoyerMessage(socketDialogue, "Lettre déjà devinée");
            continue;
        }

        /* On ajoute la lettre à la liste des lettres jouées */
        strncat(lettresDevinees, &lettre, 1);

        /* Vérifie si lettre correcte */
        if (strchr(motADeviner, lettre))
        {
            envoyerMessage(socketDialogue, "Bonne lettre !");
            for (int i = 0; i < longueurMot; i++)
            {
                if (motADeviner[i] == lettre)
                    lettresTrouvees++;
            }
        }
        else
        {
            essaisRestants--;
            envoyerMessage(socketDialogue, "Mauvaise lettre");
        }
    }

    /* --- Fin de partie --- */
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
}

/* -------------------------------------------------------------------------- */
/*                          Gestion des messages client                        */
/* -------------------------------------------------------------------------- */
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

    if (strcmp(messageRecu, "start x") == 0)
    {
        printf("Commande spéciale reçue : démarrage du jeu.\n");
        jeuDuPendu(socketDialogue);
    }

    return lus;
}

/* -------------------------------------------------------------------------- */
/*                              Boucle principale                             */
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

        char messageAEnvoyer[LG_MESSAGE];

        while (1)
        {

            int lus = recevoirMessage(socketDialogue);

            if (lus == 0)
            {
                printf("Client %s déconnecté.\n", inet_ntoa(client.sin_addr));
                close(socketDialogue);
                break; // <--- Retour à "En attente d'un client..."
            }

            printf("Entrez le message à envoyer (ou 'exit' pour quitter) : ");
            fgets(messageAEnvoyer, LG_MESSAGE, stdin);
            messageAEnvoyer[strcspn(messageAEnvoyer, "\n")] = '\0';

            if (strcmp(messageAEnvoyer, "exit") == 0)
            {
                printf("Fermeture de la connexion avec %s\n", inet_ntoa(client.sin_addr));
                close(socketDialogue);
                break;
            }

            envoyerMessage(socketDialogue, messageAEnvoyer);
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
