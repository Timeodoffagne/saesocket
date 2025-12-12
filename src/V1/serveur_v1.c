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

/* -------------------------------------------------------------------------- */
/*                              Jeu du pendu                                  */
/*--------------------------------------------------------------------------- */
int jeuDuPendu(int socketDialogue1, int socketDialogue2)
{
    char *motADeviner = creationMot();
    int longueurMot = strlen(motADeviner);

    char lettresDevinees[LG_MESSAGE] = {0};
    int essaisRestants = 10;
    int lettresTrouvees = 0;
    int joueurCourant = 2;

    char motCache[LG_MESSAGE];

    printf("Nouveau jeu du pendu ! Mot = %s (%d lettres)\n", motADeviner, longueurMot);

    /* 1) Le serveur confirme le début */
    envoyerPacket(socketDialogue1, 1, "start");
    envoyerPacket(socketDialogue2, 2, "start");

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
        envoyerPacket(socketDialogue1, 1, motCache);
        envoyerPacket(socketDialogue2, 2, motCache);

        /* Envoie le nombre d’essais */
        char essaisStr[8];
        sprintf(essaisStr, "%d", essaisRestants);
        sleep(1);
        envoyerPacket(socketDialogue1, 1, essaisStr);
        envoyerPacket(socketDialogue2, 2, essaisStr);

        /* Réception d’une lettre */
        char buffer[16];

        int lus;
        if (joueurCourant == 1)
            lus = recv(socketDialogue1, buffer, sizeof(buffer), 0);
        else
            lus = recv(socketDialogue2, buffer, sizeof(buffer), 0);

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
            if (joueurCourant == 1)
            {
                envoyerPacket(socketDialogue1, 1, "Lettre déjà devinée");
                envoyerPacket(socketDialogue1, 2, "Lettre déjà devinée");
            }
            else
            {
                envoyerPacket(socketDialogue2, 2, "Lettre déjà devinée");
                envoyerPacket(socketDialogue2, 1, "Lettre déjà devinée");
            }
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

            if (joueurCourant == 1)
            {
                envoyerPacket(socketDialogue1, 1, "Bonne lettre !");
                envoyerPacket(socketDialogue2, 2, "Bonne lettre !");
            }
            else
            {
                envoyerPacket(socketDialogue2, 2, "Bonne lettre !");
                envoyerPacket(socketDialogue1, 1, "Bonne lettre !");
            }
        }
        else if (essaisRestants > 0 && lettresTrouvees < longueurMot)
        {
            essaisRestants--;
            if (joueurCourant == 1)
            {
                envoyerPacket(socketDialogue1, 1, "Mauvaise lettre");
                envoyerPacket(socketDialogue2, 2, "Mauvaise lettre");
            }
            else
            {
                envoyerPacket(socketDialogue2, 2, "Mauvaise lettre");
                envoyerPacket(socketDialogue1, 1, "Mauvaise lettre");
            }
        }
    }
    if (joueurCourant == 1)
    {
        envoyerPacket(socketDialogue1, 1, "END");
        envoyerPacket(socketDialogue2, 2, "END");
    }
    else
    {
        envoyerPacket(socketDialogue2, 2, "END");
        envoyerPacket(socketDialogue1, 1, "END");
    }
    sleep(1);
    /* Fin du jeu */
    if (lettresTrouvees == longueurMot)
    {
        if (joueurCourant == 1)
        {
            envoyerPacket(socketDialogue1, 1, "VICTOIRE");
            envoyerPacket(socketDialogue2, 2, "VICTOIRE");
        }
        else
        {
            envoyerPacket(socketDialogue2, 2, "VICTOIRE");
            envoyerPacket(socketDialogue1, 1, "VICTOIRE");
        }
        printf("Le client %d a gagné !\n", joueurCourant);
    }
    else
    {
        if (joueurCourant == 1)
        {
            envoyerPacket(socketDialogue1, 1, "DEFAITE");
            envoyerPacket(socketDialogue2, 2, "DEFAITE");
        }
        else
        {
            envoyerPacket(socketDialogue2, 2, "DEFAITE");
            envoyerPacket(socketDialogue1, 1, "DEFAITE");
        }
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

int traiterPacket(int socketDialogue1, int socketDialogue2, int identifiantClient)
{
    Packet p;
    int lus = 0;
    if (identifiantClient == 1)
    {
        lus = recevoirPacket(socketDialogue1, &p);
    }
    else if (identifiantClient == 2)
    {
        lus = recevoirPacket(socketDialogue2, &p);
    }
    if (lus <= 0)
        return 0; // client déconnecté

    printf("[SERVEUR] Dest=%d | Message=%s\n", p.destinataire, p.message);

    // Commandes spéciales du client
    if (strcmp(p.message, "start") == 0)
    {
        printf("-> Commande start reçue.\n");

        // Démarrage du jeu solo existant
        int res = jeuDuPendu(socketDialogue1, socketDialogue2);
        if (res == 0)
            return 0;

        return lus;
    }
    else if (strcmp(p.message, "exit") == 0)
    {
        printf("Client demande une fermeture.\n");
        if (identifiantClient == 1)
        {
            envoyerPacket(socketDialogue1, p.destinataire, "Au revoir");
            close(socketDialogue1);
        }
        else if (identifiantClient == 2)
        {
            envoyerPacket(socketDialogue2, p.destinataire, "Au revoir");
            close(socketDialogue2);
        }
        return 0;
    }
    else
    {
        if (identifiantClient == 1)
            envoyerPacket(socketDialogue1, p.destinataire, "Commande inconnue");
        else if (identifiantClient == 2)
            envoyerPacket(socketDialogue2, p.destinataire, "Commande inconnue");
    }
    return lus;
}

/* -------------------------------------------------------------------------- */
/*                              Boucle principale                             */
/* -------------------------------------------------------------------------- */
void boucleServeur(int socketEcoute)
{
    int socketDialogue1 = -1;
    int socketDialogue2 = -1;
    socklen_t longueurAdresse = sizeof(struct sockaddr_in);
    struct sockaddr_in clientAddr;

    /* pointers persistants pour infos client */
    /* client1 / client2 sont des globals dans ton code */
    client1 = NULL;
    client2 = NULL;

    while (1)
    {
        fd_set readfds;
        FD_ZERO(&readfds);

        int maxfd = socketEcoute;
        FD_SET(socketEcoute, &readfds);

        if (client1 && client1->socket >= 0) {
            FD_SET(client1->socket, &readfds);
            if (client1->socket > maxfd) maxfd = client1->socket;
        }
        if (client2 && client2->socket >= 0) {
            FD_SET(client2->socket, &readfds);
            if (client2->socket > maxfd) maxfd = client2->socket;
        }

        FD_SET(STDIN_FILENO, &readfds);
        if (STDIN_FILENO > maxfd) maxfd = STDIN_FILENO;

        /* select attend une activité : nouvelle connexion, données clients, ou stdin */
        int ready = select(maxfd + 1, &readfds, NULL, NULL, NULL);
        if (ready < 0) {
            if (errno == EINTR) continue;
            perror("select");
            break;
        }

        /* --- nouvelle connexion entrante ? --- */
        if (FD_ISSET(socketEcoute, &readfds)) {
            int s = accept(socketEcoute, (struct sockaddr *)&clientAddr, &longueurAdresse);
            if (s < 0) {
                perror("accept");
            } else {
                /* attribuer au premier slot libre */
                if (!client1) {
                    ClientData *c = malloc(sizeof(ClientData));
                    if (!c) { perror("malloc"); close(s); }
                    else {
                        c->id = 1; c->socket = s; c->addr = clientAddr;
                        client1 = c;
                        printf("Client 1 connecté : %s\n", inet_ntoa(c->addr.sin_addr));
                    }
                } else if (!client2) {
                    ClientData *c = malloc(sizeof(ClientData));
                    if (!c) { perror("malloc"); close(s); }
                    else {
                        c->id = 2; c->socket = s; c->addr = clientAddr;
                        client2 = c;
                        printf("Client 2 connecté : %s\n", inet_ntoa(c->addr.sin_addr));
                    }
                } else {
                    /* trop de clients : on refuse proprement */
                    printf("Serveur plein : refus du client %s\n", inet_ntoa(clientAddr.sin_addr));
                    char *msg = "Serveur plein\n";
                    send(s, msg, strlen(msg), 0);
                    close(s);
                }
            }
        }

        /* --- activité sur client1 ? --- */
        if (client1 && client1->socket >= 0 && FD_ISSET(client1->socket, &readfds)) {
            int lus = traiterPacket(client1->socket,
                                    client2 ? client2->socket : -1,
                                    1);
            if (lus <= 0) {
                printf("Client 1 (%s) déconnecté.\n", inet_ntoa(client1->addr.sin_addr));
                close(client1->socket);
                free(client1);
                client1 = NULL;
            }
        }

        /* --- activité sur client2 ? --- */
        if (client2 && client2->socket >= 0 && FD_ISSET(client2->socket, &readfds)) {
            int lus = traiterPacket(client1 ? client1->socket : -1,
                                    client2->socket,
                                    2);
            if (lus <= 0) {
                printf("Client 2 (%s) déconnecté.\n", inet_ntoa(client2->addr.sin_addr));
                close(client2->socket);
                free(client2);
                client2 = NULL;
            }
        }

        /* --- activité sur stdin (opérateur serveur) ? --- */
        if (FD_ISSET(STDIN_FILENO, &readfds)) {
            char line[LG_MESSAGE];
            if (fgets(line, sizeof(line), stdin) == NULL) {
                /* EOF sur stdin : on continue mais on ne plante pas */
                printf("stdin fermé (EOF)\n");
            } else {
                line[strcspn(line, "\n")] = 0;
                if (strcmp(line, "exit") == 0) {
                    printf("Arrêt serveur demandé par stdin.\n");
                    /* fermer les clients et sortir */
                    if (client1) { close(client1->socket); free(client1); client1 = NULL; }
                    if (client2) { close(client2->socket); free(client2); client2 = NULL; }
                    break;
                }
                /* Broadcast simple : envoi manuel au(x) client(s) connectés */
                if (client1) envoyerPacket(client1->socket, client1->id, line);
                if (client2) envoyerPacket(client2->socket, client2->id, line);
            }
        }

        /* boucle reprend automatiquement */
    } /* fin while(1) */

    /* nettoyage final */
    if (client1) { close(client1->socket); free(client1); client1 = NULL; }
    if (client2) { close(client2->socket); free(client2); client2 = NULL; }
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
