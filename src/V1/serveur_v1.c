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
#define LISTE_MOTS "../../assets/mots.txt"

#define LG_MESSAGE 256

typedef struct
{
    int destinataire;         // 1 ou 2
    char message[LG_MESSAGE]; // message texte
} Packet;

typedef struct
{
    int id;                  // 1 ou 2
    int socket;              // socket de dialogue du client
    struct sockaddr_in addr; // adresse IP du client
} ClientData;

ClientData *client1 = NULL;
ClientData *client2 = NULL;

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
/*                               Mot aléatoire                                */
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

/*-------------------------------------------------------------------------- */
/*                          Gestion des messages client                      */
/* ------------------------------------------------------------------------- */
void deconnexion(int socketDialogue)
{
    close(socketDialogue);
    printf("Connexion fermée.\n");
}

/*-------------------------------------------------------------------------- */
/*                          Envoi des messages client                        */
/* ------------------------------------------------------------------------- */
int envoyerPacket(int sock, int destinataire, const char *msg)
{
    Packet p;
    p.destinataire = destinataire;
    strncpy(p.message, msg, LG_MESSAGE);

    char buffer[sizeof(Packet)];
    memcpy(buffer, &p.destinataire, sizeof(int));
    memcpy(buffer + sizeof(int), p.message, LG_MESSAGE);

    return send(sock, buffer, sizeof(Packet), 0);
}

/*-------------------------------------------------------------------------- */
/*                      Réception des messages client                        */
/* ------------------------------------------------------------------------- */
int recevoirPacket(int sock, Packet *p)
{
    char buffer[sizeof(int) + LG_MESSAGE];
    int rec = recv(sock, buffer, sizeof(buffer), 0);

    if (rec <= 0)
        return rec;

    int dest_net;
    memcpy(&dest_net, buffer, sizeof(int));
    p->destinataire = ntohl(dest_net);

    memcpy(p->message, buffer + sizeof(int), LG_MESSAGE);

    return rec;
}

/* -------------------------------------------------------------------------- */
/*                              Jeu du pendu                                  */
/*--------------------------------------------------------------------------- */
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
/*                          Gestion des messages client                        */
/* -------------------------------------------------------------------------- */
int recevoirPacket(int socketDialogue, Packet *p)
{
    char buffer[sizeof(int) + LG_MESSAGE];
    memset(buffer, 0, sizeof(buffer));

    int lus = recv(socketDialogue, buffer, sizeof(buffer), 0);
    if (lus <= 0)
    {
        return 0; // déconnexion
    }

    // Décomposer le paquet
    int dest_net;
    memcpy(&dest_net, buffer, sizeof(int));
    p->destinataire = ntohl(dest_net);

    memcpy(p->message, buffer + sizeof(int), LG_MESSAGE);

    // Affichage debug
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

int traiterPacket(int socketDialogue)
{
    Packet p;
    int lus = recevoirPacket(socketDialogue, &p);

    if (lus <= 0)
        return 0; // client déconnecté

    printf("[SERVEUR] Dest=%d | Message=%s\n", p.destinataire, p.message);

    // Commandes spéciales du client
    if (strcmp(p.message, "start x") == 0)
    {
        printf("-> Commande start x reçue.\n");

        // Démarrage du jeu solo existant
        int res = jeuDuPendu(socketDialogue);
        if (res == 0)
            return 0;

        return lus;
    }
    else if (strcmp(p.message, "exit") == 0)
    {
        printf("Client demande une fermeture.\n");
        envoyerPacket(socketDialogue, p.destinataire, "Au revoir");
        close(socketDialogue);
        return 0;
    }
    else
    {
        envoyerPacket(socketDialogue, p.destinataire, "Commande inconnue");
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

        ClientData *cd = malloc(sizeof(ClientData));
        cd->socket = socketDialogue;
        cd->addr = client;

        if (client1 == NULL)
        {
            cd->id = 1;
            client1 = cd;
            printf("Client 1 connecté : %s\n", inet_ntoa(client.sin_addr));
        }
        else if (client2 == NULL)
        {
            cd->id = 2;
            client2 = cd;
            printf("Client 2 connecté : %s\n", inet_ntoa(client.sin_addr));
        }
        else
        {
            printf("Serveur plein : refus du client %s\n", inet_ntoa(client.sin_addr));
            close(socketDialogue);
            free(cd);
            continue;
        }

        char messageAEnvoyer[LG_MESSAGE];

        /* Boucle principale : utilisons select() pour surveiller la socket client et stdin.
           Ainsi, si le client ferme son terminal (FIN), la socket devient lisible et
           recv() retournera 0 -> on détecte la déconnexion et on revient en attente. */
        while (1)
        {
            fd_set readfds;
            FD_ZERO(&readfds);
            FD_SET(socketDialogue, &readfds);
            FD_SET(STDIN_FILENO, &readfds);

            int maxfd = socketDialogue > STDIN_FILENO ? socketDialogue : STDIN_FILENO;

            int ready = select(maxfd + 1, &readfds, NULL, NULL, NULL);
            if (ready < 0)
            {
                perror("select");
                close(socketDialogue);
                break;
            }

            /* Si la socket est lisible : reception d'un message ou déconnexion */
            if (FD_ISSET(socketDialogue, &readfds))
            {
                int lus = recevoirMessage(socketDialogue);

                if (lus == 0)
                {
                    printf("Client %s déconnecté.\n", inet_ntoa(client.sin_addr));
                    close(socketDialogue);
                    break; // Retour à "En attente d'un client..."
                }

                if (lus < 0)
                {
                    perror("recv");
                    close(socketDialogue);
                    break;
                }

                /* continuer la boucle pour éventuellement lire stdin ou nouvelle donnée */
            }

            /* Si stdin est lisible : l'opérateur veut envoyer un message */
            if (FD_ISSET(STDIN_FILENO, &readfds))
            {
                if (fgets(messageAEnvoyer, LG_MESSAGE, stdin) == NULL)
                {
                    /* EOF sur stdin : on ferme la connexion actuelle et on attend un nouveau client */
                    printf("stdin fermé. Fermeture de la connexion avec %s\n", inet_ntoa(client.sin_addr));
                    close(socketDialogue);
                    break;
                }

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
