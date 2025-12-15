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
    int destinataire;
    char message[LG_MESSAGE];
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
    memset(&p, 0, sizeof(Packet));  // IMPORTANT: initialiser toute la structure
    
    p.destinataire = destinataire;
    strncpy(p.message, msg, LG_MESSAGE - 1);
    p.message[LG_MESSAGE - 1] = '\0';

    char buffer[sizeof(Packet)];
    memset(buffer, 0, sizeof(buffer));  // IMPORTANT: initialiser le buffer
    
    int dest_net = htonl(p.destinataire);
    memcpy(buffer, &dest_net, sizeof(int));
    memcpy(buffer + sizeof(int), p.message, LG_MESSAGE);

    int sent = send(sock, buffer, sizeof(Packet), 0);
    if (sent > 0)
    {
        printf("[CLIENT ENVOIE] Dest=%d | Message='%s'\n", destinataire, msg);
    }
    return sent;
}

// =====================================================
//  FONCTION : recevoir une réponse du serveur
// =====================================================
int recevoirPacket(int sock, Packet *p)
{
    char buffer[sizeof(int) + LG_MESSAGE];
    memset(buffer, 0, sizeof(buffer));
    
    int rec = recv(sock, buffer, sizeof(buffer), 0);

    if (rec <= 0)
        return rec;

    int dest_net;
    memcpy(&dest_net, buffer, sizeof(int));
    p->destinataire = ntohl(dest_net);

    memcpy(p->message, buffer + sizeof(int), LG_MESSAGE);
    p->message[LG_MESSAGE - 1] = '\0';

    printf("[CLIENT REÇOIT] Dest=%d | Message='%s'\n", p->destinataire, p->message);
    
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
void jeuDuPenduV1(int sock, int ID_CLIENT)
{
    char buffer[256];
    Packet p;
    int ret;

    printf("\n|=======================================================|\n");
    printf("|              PARTIE EN COURS                           |\n");
    printf("|========================================================|\n");

    // Boucle principale
    while (1)
    {
        // Réception du mot masqué
        printf("[DEBUG] Attente du mot masqué...\n");
        ret = recevoirPacket(sock, &p);
        if (ret <= 0)
        {
            printf("[ERREUR] de réception du mot masqué.\n");
            return;
        }

        char motCache[LG_MESSAGE];
        strcpy(motCache, p.message);
        int currentID = p.destinataire;
        printf("[DEBUG] Mot reçu: '%s', joueur actif: %d\n", motCache, currentID);

        // Si "END" → le serveur annonce la fin
        if (strcmp(motCache, "END") == 0)
        {
            printf("[DEBUG] Fin de partie détectée.\n");
            // Recevoir VICTOIRE ou DEFAITE
            ret = recevoirPacket(sock, &p);
            if (ret <= 0) return;

            if (p.destinataire == ID_CLIENT)
            {
                if (strcmp(p.message, "VICTOIRE") == 0)
                {
                    clearScreen();
                    printf("\n|================================|");
                    printf("\n|          VICTOIRE !            |");
                    printf("\n|================================|\n\n");
                }
                else
                {
                    clearScreen();
                    printf("\n|================================|");
                    printf("\n|          DÉFAITE...            |");
                    printf("\n|================================|\n\n");
                    afficherLePendu("0");
                }
            }
            else
            {
                // L'autre joueur a gagné/perdu
                if (strcmp(p.message, "VICTOIRE") == 0)
                {
                    clearScreen();
                    printf("\n|================================|");
                    printf("\n|          DÉFAITE...            |");
                    printf("\n|================================|\n\n");
                    afficherLePendu("0");
                }
                else
                {
                    clearScreen();
                    printf("\n|================================|");
                    printf("\n|          VICTOIRE !            |");
                    printf("\n|================================|\n\n");
                }
            }
            return;
        }

        // Réception des essais restants joueur 1
        printf("[DEBUG] Attente essais joueur 1...\n");
        ret = recevoirPacket(sock, &p);
        if (ret <= 0)
        {
            printf("[ERREUR] réception essais J1\n");
            return;
        }
        char essais1[16];
        strcpy(essais1, p.message);
        printf("[DEBUG] Essais J1 reçus: %s (dest=%d)\n", essais1, p.destinataire);

        // Réception des essais restants joueur 2
        printf("[DEBUG] Attente essais joueur 2...\n");
        ret = recevoirPacket(sock, &p);
        if (ret <= 0)
        {
            printf("[ERREUR] réception essais J2\n");
            return;
        }
        char essais2[16];
        strcpy(essais2, p.message);
        printf("[DEBUG] Essais J2 reçus: %s (dest=%d)\n", essais2, p.destinataire);

        // Déterminer mes essais et ceux de l'adversaire
        char *essaisMoi = (ID_CLIENT == 1) ? essais1 : essais2;
        char *essaisAdversaire = (ID_CLIENT == 1) ? essais2 : essais1;

        // Affichage
        printf("[DEBUG] Affichage de l'interface...\n");
        clearScreen();
        printf("|========================================================|\n");
        printf("|              JEU DU PENDU - EN LIGNE                   |\n");
        printf("|========================================================|\n\n");
        printf("Mot : %s\n", motCache);
        printf("Vos essais restants : %s ♥\n", essaisMoi);
        printf("Essais adversaire : %s ♥\n\n", essaisAdversaire);

        afficherLePendu(essaisMoi);

        if (currentID == ID_CLIENT)
        {
            printf("\nC'est votre tour !\n");
            printf("Votre lettre : ");
            fflush(stdout);
            
            if (fgets(buffer, sizeof(buffer), stdin) == NULL)
            {
                printf("[ERREUR] de lecture.\n");
                return;
            }
            
            buffer[strcspn(buffer, "\n")] = 0;
            
            if (strlen(buffer) == 0)
            {
                printf("Vous devez entrer une lettre !\n");
                sleep(1);
                continue;
            }
            
            envoyerPacket(sock, ID_CLIENT, buffer);
        }
        else
        {
            printf("\nEn attente du joueur %d...\n", currentID);
        }

        // Réception du retour : Bonne/Mauvaise lettre, etc.
        ret = recevoirPacket(sock, &p);
        if (ret <= 0) return;

        printf("\n");
        if (strcmp(p.message, "Bonne lettre !") == 0)
        {
            printf("%s\n", p.message);
        }
        else if (strcmp(p.message, "Mauvaise lettre") == 0)
        {
            printf("%s\n", p.message);
        }
        else if (strcmp(p.message, "Lettre déjà devinée") == 0)
        {
            printf("%s\n", p.message);
        }
        else
        {
            printf("%s\n", p.message);
        }
        
        sleep(2);
    }
}

// =====================================================
//  BOUCLE PRINCIPALE DU CLIENT
// =====================================================
void boucleClient(int sock, int *ID_CLIENT)
{
    char buffer[256];
    
    // Recevoir l'ID du serveur au début
    Packet p;
    int ret = recevoirPacket(sock, &p);
    if (ret > 0)
    {
        *ID_CLIENT = p.destinataire;
        printf("\n|========================================================|\n");
        printf("\n|  Vous êtes le joueur #%d                               |", *ID_CLIENT);
        printf("\n|  %s  |\n", p.message);
        printf("\n|========================================================|\n");
    }
    else
    {
        printf("[ERREUR] lors de la réception de l'ID.\n");
        return;
    }

    while (1)
    {
        printf("\n> Commandes : 'start' (jouer) | 'exit' (quitter)\n");
        printf("> Votre choix : ");
        fflush(stdout);
        
        if (fgets(buffer, sizeof(buffer), stdin) == NULL)
        {
            printf("[ERREUR] de lecture.\n");
            break;
        }
        
        buffer[strcspn(buffer, "\n")] = 0;

        if (strlen(buffer) == 0)
        {
            continue;
        }

        if (strcmp(buffer, "exit") == 0)
        {
            envoyerPacket(sock, *ID_CLIENT, "exit");
            printf("Fermeture du client.\n");
            break;
        }
        else if (strcmp(buffer, "start") == 0)
        {
            envoyerPacket(sock, *ID_CLIENT, "start");
            printf("Recherche d'une partie...\n");
            
            // Attendre la réponse du serveur
            ret = recevoirPacket(sock, &p);
            if (ret <= 0)
            {
                printf("Erreur de réception.\n");
                break;
            }
            
            // Vérifier si c'est un message d'attente ou le début de la partie
            if (strcmp(p.message, "start") == 0)
            {
                // Le serveur a lancé la partie immédiatement
                printf("Partie lancée !\n");
                sleep(1);
                jeuDuPenduV1(sock, *ID_CLIENT);
            }
            else if (strstr(p.message, "attente") != NULL || strstr(p.message, "En attente") != NULL)
            {
                // En attente de l'adversaire
                printf("%s\n", p.message);
                
                // Attendre le message "start" du serveur
                while (1)
                {
                    ret = recevoirPacket(sock, &p);
                    if (ret <= 0)
                    {
                        printf("Connexion perdue.\n");
                        return;
                    }
                    
                    if (strcmp(p.message, "start") == 0)
                    {
                        printf("Adversaire trouvé ! Début de la partie...\n");
                        sleep(1);
                        jeuDuPenduV1(sock, *ID_CLIENT);
                        break;
                    }
                    else
                    {
                        printf("Message serveur: %s\n", p.message);
                    }
                }
            }
            else
            {
                printf("Réponse serveur: %s\n", p.message);
            }
            
            continue;
        }
        else
        {
            // Message quelconque
            envoyerPacket(sock, *ID_CLIENT, buffer);

            ret = recevoirPacket(sock, &p);
            if (ret <= 0)
            {
                printf("[ERREUR] de réception ou connexion fermée.\n");
                break;
            }

            printf("Serveur : %s\n", p.message);
        }
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
    boucleClient(sock, &ID_CLIENT);

    close(sock);
    return 0;
}