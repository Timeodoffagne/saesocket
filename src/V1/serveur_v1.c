#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <string.h>
#include <time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <sys/select.h>

#define PORT 5000
#define LG_MESSAGE 256
#define LISTE_MOTS "../../assets/mots.txt"

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
    int essaisRestants;  // NOUVEAU: essais individuels
} ClientData;

ClientData *client1 = NULL;
ClientData *client2 = NULL;

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

/* -------------------------------------------------------------------------- */
int envoyerPacket(int sock, int destinataire, const char *msg)
{
    Packet p;
    p.destinataire = destinataire;
    strncpy(p.message, msg, LG_MESSAGE - 1);
    p.message[LG_MESSAGE - 1] = '\0';

    char buffer[sizeof(Packet)];
    int dest_net = htonl(p.destinataire);  // CORRECTION: conversion réseau
    memcpy(buffer, &dest_net, sizeof(int));
    memcpy(buffer + sizeof(int), p.message, LG_MESSAGE);

    return send(sock, buffer, sizeof(Packet), 0);
}

/* -------------------------------------------------------------------------- */
int recevoirPacket(int socketDialogue, Packet *p)
{
    char buffer[sizeof(int) + LG_MESSAGE];
    memset(buffer, 0, sizeof(buffer));

    int lus = recv(socketDialogue, buffer, sizeof(buffer), 0);
    if (lus <= 0)
        return 0;

    int dest_net;
    memcpy(&dest_net, buffer, sizeof(int));
    p->destinataire = ntohl(dest_net);

    memcpy(p->message, buffer + sizeof(int), LG_MESSAGE);
    p->message[LG_MESSAGE - 1] = '\0';

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

/* -------------------------------------------------------------------------- */
/*                        JEU DU PENDU (CORRIGÉ)                              */
/* -------------------------------------------------------------------------- */
int jeuDuPendu(ClientData *c1, ClientData *c2)
{
    if (!c1 || !c2) return 0;

    char *motADeviner = creationMot();
    int longueurMot = strlen(motADeviner);

    char lettresDevinees[LG_MESSAGE] = {0};
    c1->essaisRestants = 10;
    c2->essaisRestants = 10;
    int lettresTrouvees = 0;
    int joueurCourant = 1;  // commence avec joueur 1

    char motCache[LG_MESSAGE];

    printf("\n=== Nouvelle partie ===\n");
    printf("Mot à deviner : %s (%d lettres)\n", motADeviner, longueurMot);

    // Confirmation de démarrage
    envoyerPacket(c1->socket, c1->id, "start");
    envoyerPacket(c2->socket, c2->id, "start");

    while (lettresTrouvees < longueurMot && 
           c1->essaisRestants > 0 && 
           c2->essaisRestants > 0)
    {
        // Construction du mot masqué
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

        // Envoie le mot masqué avec l'ID du joueur courant
        envoyerPacket(c1->socket, joueurCourant, motCache);
        envoyerPacket(c2->socket, joueurCourant, motCache);

        // Envoie les essais restants individuels
        char essais1[8], essais2[8];
        sprintf(essais1, "%d", c1->essaisRestants);
        sprintf(essais2, "%d", c2->essaisRestants);
        
        envoyerPacket(c1->socket, c1->id, essais1);
        envoyerPacket(c2->socket, c2->id, essais2);

        // Réception de la lettre du joueur courant
        Packet p;
        int lus;
        ClientData *joueur = (joueurCourant == 1) ? c1 : c2;
        
        lus = recevoirPacket(joueur->socket, &p);
        if (lus <= 0)
        {
            printf("Client %d déconnecté.\n", joueurCourant);
            return 0;
        }

        char lettre = p.message[0];
        printf("Joueur %d joue : '%c'\n", joueurCourant, lettre);

        // Lettre déjà devinée
        if (strchr(lettresDevinees, lettre))
        {
            envoyerPacket(c1->socket, joueurCourant, "Lettre déjà devinée");
            envoyerPacket(c2->socket, joueurCourant, "Lettre déjà devinée");
            continue;
        }

        // Ajout de la lettre
        strncat(lettresDevinees, &lettre, 1);

        // Vérification : bonne ou mauvaise lettre
        if (strchr(motADeviner, lettre))
        {
            // Bonne lettre
            for (int i = 0; i < longueurMot; i++)
            {
                if (motADeviner[i] == lettre)
                    lettresTrouvees++;
            }
            envoyerPacket(c1->socket, joueurCourant, "Bonne lettre !");
            envoyerPacket(c2->socket, joueurCourant, "Bonne lettre !");
            printf("→ Bonne lettre ! (%d/%d trouvées)\n", lettresTrouvees, longueurMot);
        }
        else
        {
            // Mauvaise lettre
            joueur->essaisRestants--;
            envoyerPacket(c1->socket, joueurCourant, "Mauvaise lettre");
            envoyerPacket(c2->socket, joueurCourant, "Mauvaise lettre");
            printf("→ Mauvaise lettre ! (reste %d essais)\n", joueur->essaisRestants);
        }

        // Alternance des joueurs
        joueurCourant = (joueurCourant == 1) ? 2 : 1;
    }

    // Fin de partie : envoi de END puis résultat
    envoyerPacket(c1->socket, 1, "END");
    envoyerPacket(c2->socket, 2, "END");

    sleep(1);

    // Déterminer le gagnant
    if (lettresTrouvees == longueurMot)
    {
        // Le dernier joueur qui a trouvé la dernière lettre gagne
        joueurCourant = (joueurCourant == 1) ? 2 : 1;  // retour au dernier joueur
        printf("→ Joueur %d a gagné !\n", joueurCourant);
        
        if (joueurCourant == 1)
        {
            envoyerPacket(c1->socket, 1, "VICTOIRE");
            envoyerPacket(c2->socket, 2, "DEFAITE");
        }
        else
        {
            envoyerPacket(c1->socket, 1, "DEFAITE");
            envoyerPacket(c2->socket, 2, "VICTOIRE");
        }
    }
    else
    {
        // Les deux ont perdu (plus d'essais)
        printf("→ Les deux joueurs ont perdu ! Mot : %s\n", motADeviner);
        envoyerPacket(c1->socket, 1, "DEFAITE");
        envoyerPacket(c2->socket, 2, "DEFAITE");
    }

    return 1;
}

/* -------------------------------------------------------------------------- */
int traiterPacket(ClientData *c1, ClientData *c2, int identifiantClient)
{
    Packet p;
    ClientData *joueur = (identifiantClient == 1) ? c1 : c2;
    
    if (!joueur) return 0;
    
    int lus = recevoirPacket(joueur->socket, &p);
    if (lus <= 0)
        return 0;

    printf("[SERVEUR] Client %d | Message=%s\n", identifiantClient, p.message);

    if (strcmp(p.message, "start") == 0)
    {
        printf("→ Commande start reçue du client %d.\n", identifiantClient);

        if (!c1 || !c2)
        {
            envoyerPacket(joueur->socket, identifiantClient, 
                         "Erreur: Il faut 2 joueurs connectés");
            return lus;
        }

        int res = jeuDuPendu(c1, c2);
        if (res == 0)
            return 0;

        return lus;
    }
    else if (strcmp(p.message, "exit") == 0)
    {
        printf("Client %d demande une fermeture.\n", identifiantClient);
        envoyerPacket(joueur->socket, identifiantClient, "Au revoir");
        return 0;
    }
    else
    {
        envoyerPacket(joueur->socket, identifiantClient, "Commande inconnue");
    }
    
    return lus;
}

/* -------------------------------------------------------------------------- */
void boucleServeur(int socketEcoute)
{
    socklen_t longueurAdresse = sizeof(struct sockaddr_in);
    struct sockaddr_in clientAddr;

    client1 = NULL;
    client2 = NULL;

    while (1)
    {
        fd_set readfds;
        FD_ZERO(&readfds);

        int maxfd = socketEcoute;
        FD_SET(socketEcoute, &readfds);

        if (client1 && client1->socket >= 0)
        {
            FD_SET(client1->socket, &readfds);
            if (client1->socket > maxfd)
                maxfd = client1->socket;
        }
        if (client2 && client2->socket >= 0)
        {
            FD_SET(client2->socket, &readfds);
            if (client2->socket > maxfd)
                maxfd = client2->socket;
        }

        FD_SET(STDIN_FILENO, &readfds);
        if (STDIN_FILENO > maxfd)
            maxfd = STDIN_FILENO;

        int ready = select(maxfd + 1, &readfds, NULL, NULL, NULL);
        if (ready < 0)
        {
            if (errno == EINTR)
                continue;
            perror("select");
            break;
        }

        // Nouvelle connexion
        if (FD_ISSET(socketEcoute, &readfds))
        {
            int s = accept(socketEcoute, (struct sockaddr *)&clientAddr, &longueurAdresse);
            if (s < 0)
            {
                perror("accept");
            }
            else
            {
                if (!client1)
                {
                    ClientData *c = malloc(sizeof(ClientData));
                    if (!c)
                    {
                        perror("malloc");
                        close(s);
                    }
                    else
                    {
                        c->id = 1;
                        c->socket = s;
                        c->addr = clientAddr;
                        c->essaisRestants = 10;
                        client1 = c;
                        printf("Client 1 connecté : %s\n", inet_ntoa(c->addr.sin_addr));
                        envoyerPacket(s, 1, "Bienvenue client 1");
                    }
                }
                else if (!client2)
                {
                    ClientData *c = malloc(sizeof(ClientData));
                    if (!c)
                    {
                        perror("malloc");
                        close(s);
                    }
                    else
                    {
                        c->id = 2;
                        c->socket = s;
                        c->addr = clientAddr;
                        c->essaisRestants = 10;
                        client2 = c;
                        printf("Client 2 connecté : %s\n", inet_ntoa(c->addr.sin_addr));
                        envoyerPacket(s, 2, "Bienvenue client 2");
                    }
                }
                else
                {
                    printf("Serveur plein : refus du client %s\n", 
                           inet_ntoa(clientAddr.sin_addr));
                    char *msg = "Serveur plein\n";
                    send(s, msg, strlen(msg), 0);
                    close(s);
                }
            }
        }

        // Activité sur client1
        if (client1 && client1->socket >= 0 && FD_ISSET(client1->socket, &readfds))
        {
            int lus = traiterPacket(client1, client2, 1);
            if (lus <= 0)
            {
                printf("Client 1 (%s) déconnecté.\n", inet_ntoa(client1->addr.sin_addr));
                close(client1->socket);
                free(client1);
                client1 = NULL;
            }
        }

        // Activité sur client2
        if (client2 && client2->socket >= 0 && FD_ISSET(client2->socket, &readfds))
        {
            int lus = traiterPacket(client1, client2, 2);
            if (lus <= 0)
            {
                printf("Client 2 (%s) déconnecté.\n", inet_ntoa(client2->addr.sin_addr));
                close(client2->socket);
                free(client2);
                client2 = NULL;
            }
        }

        // Stdin
        if (FD_ISSET(STDIN_FILENO, &readfds))
        {
            char line[LG_MESSAGE];
            if (fgets(line, sizeof(line), stdin) == NULL)
            {
                printf("stdin fermé (EOF)\n");
            }
            else
            {
                line[strcspn(line, "\n")] = 0;
                if (strcmp(line, "exit") == 0)
                {
                    printf("Arrêt serveur demandé par stdin.\n");
                    if (client1)
                    {
                        close(client1->socket);
                        free(client1);
                        client1 = NULL;
                    }
                    if (client2)
                    {
                        close(client2->socket);
                        free(client2);
                        client2 = NULL;
                    }
                    break;
                }
                if (client1)
                    envoyerPacket(client1->socket, client1->id, line);
                if (client2)
                    envoyerPacket(client2->socket, client2->id, line);
            }
        }
    }

    if (client1)
    {
        close(client1->socket);
        free(client1);
        client1 = NULL;
    }
    if (client2)
    {
        close(client2->socket);
        free(client2);
        client2 = NULL;
    }
}

/* -------------------------------------------------------------------------- */
int main()
{
    srand(time(NULL));  // Initialiser le générateur aléatoire
    
    int socketEcoute;
    socklen_t longueurAdresse;
    struct sockaddr_in pointDeRencontreLocal;

    creationSocket(&socketEcoute, &longueurAdresse, &pointDeRencontreLocal);
    boucleServeur(socketEcoute);

    close(socketEcoute);
    return 0;
}