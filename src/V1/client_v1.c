#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <string.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define LG_MESSAGE 256

typedef struct
{
    int destinataire;         // 1 ou 2
    char message[LG_MESSAGE]; // message texte
} Packet;

// =====================================================
//  FONCTION : création + connexion socket
// =====================================================
int creationDeSocket(const char *ip_dest, int port_dest)
{
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
    {
        perror("Erreur en création de la socket");
        exit(EXIT_FAILURE);
    }
    printf("Socket créée (%d)\n", sock);

    struct sockaddr_in serv;
    memset(&serv, 0, sizeof(serv));

    serv.sin_family = AF_INET;
    serv.sin_port = htons(port_dest);

    if (inet_aton(ip_dest, &serv.sin_addr) == 0)
    {
        fprintf(stderr, "Adresse IP invalide !\n");
        exit(EXIT_FAILURE);
    }

    if (connect(sock, (struct sockaddr *)&serv, sizeof(serv)) == -1)
    {
        perror("Erreur de connexion au serveur");
        close(sock);
        exit(EXIT_FAILURE);
    }

    printf("Connexion au serveur %s:%d réussie !\n", ip_dest, port_dest);
    return sock;
}

// =====================================================
//  FONCTION : envoyer un packet
// =====================================================
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

// =====================================================
//  FONCTION : recevoir une réponse du serveur
// =====================================================
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
    p->message[LG_MESSAGE - 1] = '\0';  // sécurité

    return rec;
}

// =====================================================
//  FONCTION : nettoyer l'écran
// =====================================================
void clearScreen()
{
    printf("\033[2J\033[H");
}

// =====================================================
//  FONCTION : afficher le pendu selon l'état
// =====================================================
void afficherLePendu(const char *state)
{
    const char *try_paths[] = {
        "../../assets/pendu.txt",
        "../assets/pendu.txt",
        "assets/pendu.txt"
    };
    FILE *file = NULL;
    for (int i = 0; i < 3; ++i)
    {
        file = fopen(try_paths[i], "r");
        if (file) break;
    }
    if (!file)
    {
        fprintf(stderr, "Erreur lors de l'ouverture du fichier pendu.txt.\n");
        return;
    }

    int stade = atoi(state);
    char line[256];
    int debut = 0, fin = 0;

    switch (stade)
    {
    case 10: debut = 0; fin = 32; break;
    case 9: debut = 32; fin = 64; break;
    case 8: debut = 64; fin = 96; break;
    case 7: debut = 96; fin = 128; break;
    case 6: debut = 128; fin = 160; break;
    case 5: debut = 160; fin = 192; break;
    case 4: debut = 192; fin = 224; break;
    case 3: debut = 224; fin = 256; break;
    case 2: debut = 256; fin = 288; break;
    case 1: debut = 288; fin = 320; break;
    case 0: debut = 320; fin = 352; break;
    default:
        printf("[DEBUG] Stade invalide : %d\n", stade);
        fclose(file);
        return;
    }

    for (int i = 0; i < debut; i++)
        fgets(line, sizeof(line), file);

    for (int i = debut; i < fin; i++)
    {
        if (fgets(line, sizeof(line), file) == NULL)
            break;
        printf("%s", line);
    }

    fclose(file);
}

// =====================================================
//  FONCTION : jeu du pendu V1
// =====================================================
void jeuDuPenduV1(int sock, const char *ip_dest, int ID_CLIENT)
{
    char buffer[256];
    Packet p;

    // Attente de "start" du serveur
    int ret = recevoirPacket(sock, &p);
    if (ret <= 0)
    {
        printf("Erreur de réception ou connexion fermée par le serveur.\n");
        return;
    }

    printf("[DEBUG] Serveur dit: %s (dest=%d)\n", p.message, p.destinataire);

    if (strcmp(p.message, "start") != 0)
    {
        printf("Erreur : lancement refusé.\n");
        return;
    }

    // Boucle principale
    while (1)
    {
        // Réception du mot masqué
        ret = recevoirPacket(sock, &p);
        if (ret <= 0)
        {
            printf("Erreur de réception.\n");
            return;
        }

        char *motCache = p.message;
        int currentID = p.destinataire;

        // Si "END" → le serveur annonce la fin
        if (strcmp(motCache, "END") == 0)
        {
            // Recevoir VICTOIRE ou DEFAITE
            ret = recevoirPacket(sock, &p);
            if (ret <= 0) return;

            if (p.destinataire == ID_CLIENT)
            {
                if (strcmp(p.message, "VICTOIRE") == 0)
                {
                    clearScreen();
                    printf("\n!!! Gagné !!!\n\n");
                }
                else
                {
                    clearScreen();
                    printf("\n!!! Perdu !!!\n\n");
                    afficherLePendu("0");
                }
            }
            else
            {
                // L'autre joueur a gagné/perdu
                if (strcmp(p.message, "VICTOIRE") == 0)
                {
                    clearScreen();
                    printf("\n!!! Perdu !!!\n\n");
                    afficherLePendu("0");
                }
                else
                {
                    clearScreen();
                    printf("\n!!! Gagné !!!\n\n");
                }
            }
            return;
        }

        // Réception des essais restants pour chaque joueur
        ret = recevoirPacket(sock, &p);
        if (ret <= 0) return;
        char essaisMoi[16];
        strcpy(essaisMoi, p.message);

        ret = recevoirPacket(sock, &p);
        if (ret <= 0) return;
        char essaisAdversaire[16];
        strcpy(essaisAdversaire, p.message);

        // Affichage
        clearScreen();
        printf("--------------------=== Jeu du pendu V1 ===--------------------\n");
        printf("\nMot : %s\n", motCache);
        printf("Vos essais restants : %s\n", essaisMoi);
        printf("Essais adversaire : %s\n", essaisAdversaire);

        afficherLePendu(essaisMoi);

        if (currentID == ID_CLIENT)
        {
            printf("\nVotre lettre : ");
            fgets(buffer, sizeof(buffer), stdin);
            buffer[strcspn(buffer, "\n")] = 0;
            envoyerPacket(sock, ID_CLIENT, buffer);
        }
        else
        {
            printf("\nEn attente de la lettre de l'adversaire...\n");
        }

        // Réception du retour : Bonne/Mauvaise lettre, etc.
        ret = recevoirPacket(sock, &p);
        if (ret <= 0) return;

        printf("[INFO] %s\n", p.message);
        sleep(1);
    }
}

// =====================================================
//  BOUCLE PRINCIPALE DU CLIENT
// =====================================================
void boucleClient(int sock, const char *ip_dest, int *ID_CLIENT)
{
    char buffer[256];
    
    // Recevoir l'ID du serveur au début
    Packet p;
    int ret = recevoirPacket(sock, &p);
    if (ret > 0)
    {
        *ID_CLIENT = p.destinataire;
        printf("Vous êtes le client #%d\n", *ID_CLIENT);
        printf("Message du serveur: %s\n", p.message);
    }

    while (1)
    {
        printf("\nEntrez un message ('exit' pour quitter, 'start' pour jouer) : ");
        fgets(buffer, sizeof(buffer), stdin);
        buffer[strcspn(buffer, "\n")] = 0;

        if (strcmp(buffer, "exit") == 0)
        {
            envoyerPacket(sock, *ID_CLIENT, "exit");
            printf("Fermeture du client.\n");
            break;
        }
        else if (strcmp(buffer, "start") == 0)
        {
            envoyerPacket(sock, *ID_CLIENT, "start");
            printf("Démarrage d'une partie de pendu V1...\n");
            jeuDuPenduV1(sock, ip_dest, *ID_CLIENT);
            continue;
        }

        envoyerPacket(sock, *ID_CLIENT, buffer);

        ret = recevoirPacket(sock, &p);
        if (ret <= 0)
        {
            printf("Erreur de réception ou connexion fermée.\n");
            break;
        }

        printf("Serveur %s : %s\n", ip_dest, p.message);
    }
}

// =====================================================
//  MAIN
// =====================================================
int main(int argc, char *argv[])
{
    int ID_CLIENT = 0;
    if (argc < 3)
    {
        printf("USAGE : %s ip port\n", argv[0]);
        return EXIT_FAILURE;
    }

    char ip_dest[16];
    int port_dest;

    strncpy(ip_dest, argv[1], 15);
    ip_dest[15] = '\0';

    sscanf(argv[2], "%d", &port_dest);

    int sock = creationDeSocket(ip_dest, port_dest);
    boucleClient(sock, ip_dest, &ID_CLIENT);

    close(sock);
    return 0;
}