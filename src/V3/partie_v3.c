// ====================================================
// CODE ANNEXE DE LA VERSION 3
// ====================================================

// Définir _POSIX_C_SOURCE avant tous les includes
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <string.h>
#include <netinet/in.h>
#include <errno.h>
#include <sys/select.h>

#define LG_MESSAGE 256

typedef struct
{
    int destinataire;
    char message[LG_MESSAGE];
} Packet;

// =====================================================
//  FONCTION : envoi un packet à un destinataire
// =====================================================
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

// =====================================================
//  FONCTION : gere la reception d'un paquet
// =====================================================
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

    return lus;
}

// ==========================================================================
//                    GESTION D'UNE PARTIE COMPLÈTE
// ==========================================================================
void gererPartie(int id_partie, int socket_c1, int socket_c2, 
                 const char *ip_c1, const char *ip_c2)
{
    printf("[Partie %d] Démarrage : %s vs %s\n", id_partie, ip_c1, ip_c2);

    // Envoyer les messages de bienvenue
    envoyerPacket(socket_c1, 1, "Bienvenue client 1 ! Tapez 'start' pour jouer.");
    envoyerPacket(socket_c2, 2, "Bienvenue client 2 ! Tapez 'start' pour jouer.");

    int c1_pret = 0;
    int c2_pret = 0;
    int partie_active = 1;

    while (partie_active)
    {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(socket_c1, &readfds);
        FD_SET(socket_c2, &readfds);

        int maxfd = (socket_c1 > socket_c2) ? socket_c1 : socket_c2;

        struct timeval timeout;
        timeout.tv_sec = 60; // Timeout de 60 secondes
        timeout.tv_usec = 0;

        int ready = select(maxfd + 1, &readfds, NULL, NULL, &timeout);
        
        if (ready < 0)
        {
            if (errno == EINTR)
                continue;
            perror("select");
            break;
        }
        else if (ready == 0)
        {
            printf("[Partie %d] Timeout - inactivité\n", id_partie);
            continue;
        }

        // ACTIVITÉ CLIENT 1
        if (FD_ISSET(socket_c1, &readfds))
        {
            Packet p;
            int lus = recevoirPacket(socket_c1, &p);
            
            if (lus <= 0)
            {
                printf("[Partie %d] Client 1 déconnecté\n", id_partie);
                envoyerPacket(socket_c2, 2, "PARTNER_DISCONNECTED");
                partie_active = 0;
                break;
            }

            printf("[Partie %d] C1->S: %s\n", id_partie, p.message);

            // Commande 'start'
            if (strcmp(p.message, "start") == 0)
            {
                c1_pret = 1;
                if (c1_pret && c2_pret)
                {
                    printf("[Partie %d] Les deux clients prêts -> lancement\n", id_partie);
                    envoyerPacket(socket_c1, 1, "start");
                    envoyerPacket(socket_c2, 2, "start");
                    c1_pret = 0;
                    c2_pret = 0;
                }
            }
            // Commande 'REPLAY'
            else if (strcmp(p.message, "REPLAY") == 0)
            {
                c1_pret = 1;
                if (c1_pret && c2_pret)
                {
                    printf("[Partie %d] REPLAY accepté -> inversion\n", id_partie);
                    envoyerPacket(socket_c1, 2, "REPLAY_START:2");
                    envoyerPacket(socket_c2, 1, "REPLAY_START:1");
                    c1_pret = 0;
                    c2_pret = 0;
                }
                else
                {
                    // Relayer à C2
                    envoyerPacket(socket_c2, 1, p.message);
                }
            }
            // Commande 'exit'
            else if (strcmp(p.message, "exit") == 0)
            {
                printf("[Partie %d] Client 1 quitte\n", id_partie);
                envoyerPacket(socket_c2, 2, "PARTNER_DISCONNECTED");
                partie_active = 0;
                break;
            }
            // Relayer tous les autres messages à C2
            else
            {
                envoyerPacket(socket_c2, 1, p.message);
            }
        }

        // ACTIVITÉ CLIENT 2
        if (FD_ISSET(socket_c2, &readfds))
        {
            Packet p;
            int lus = recevoirPacket(socket_c2, &p);
            
            if (lus <= 0)
            {
                printf("[Partie %d] Client 2 déconnecté\n", id_partie);
                envoyerPacket(socket_c1, 1, "PARTNER_DISCONNECTED");
                partie_active = 0;
                break;
            }

            printf("[Partie %d] C2->S: %s\n", id_partie, p.message);

            // Commande 'start'
            if (strcmp(p.message, "start") == 0)
            {
                c2_pret = 1;
                if (c1_pret && c2_pret)
                {
                    printf("[Partie %d] Les deux clients prêts -> lancement\n", id_partie);
                    envoyerPacket(socket_c1, 1, "start");
                    envoyerPacket(socket_c2, 2, "start");
                    c1_pret = 0;
                    c2_pret = 0;
                }
            }
            // Commande 'REPLAY'
            else if (strcmp(p.message, "REPLAY") == 0)
            {
                c2_pret = 1;
                if (c1_pret && c2_pret)
                {
                    printf("[Partie %d] REPLAY accepté -> inversion\n", id_partie);
                    envoyerPacket(socket_c1, 2, "REPLAY_START:2");
                    envoyerPacket(socket_c2, 1, "REPLAY_START:1");
                    c1_pret = 0;
                    c2_pret = 0;
                }
                else
                {
                    // Relayer à C1
                    envoyerPacket(socket_c1, 2, p.message);
                }
            }
            // Commande 'exit'
            else if (strcmp(p.message, "exit") == 0)
            {
                printf("[Partie %d] Client 2 quitte\n", id_partie);
                envoyerPacket(socket_c1, 1, "PARTNER_DISCONNECTED");
                partie_active = 0;
                break;
            }
            // Relayer tous les autres messages à C1
            else
            {
                envoyerPacket(socket_c1, 2, p.message);
            }
        }
    }

    printf("[Partie %d] Fin de la partie\n", id_partie);
    close(socket_c1);
    close(socket_c2);
}

// ==========================================================================
//                                   MAIN
// ==========================================================================
int main(int argc, char *argv[])
{
    if (argc != 6)
    {
        fprintf(stderr, "Usage: %s <id_partie> <socket_c1> <socket_c2> <ip_c1> <ip_c2>\n", argv[0]);
        return EXIT_FAILURE;
    }

    int id_partie = atoi(argv[1]);
    int socket_c1 = atoi(argv[2]);
    int socket_c2 = atoi(argv[3]);
    const char *ip_c1 = argv[4];
    const char *ip_c2 = argv[5];

    // Gérer la partie
    gererPartie(id_partie, socket_c1, socket_c2, ip_c1, ip_c2);

    return 0;
}