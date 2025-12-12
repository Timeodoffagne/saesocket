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

    // Attente de "start" du serveur
    int ret = recevoirPacket(sock, &p);
    if (ret <= 0)
    {
        printf("Erreur de réception ou connexion fermée par le serveur.\n");
        return;
    }

    if (strcmp(p.message, "start") != 0)
    {
        printf("Message du serveur: %s\n", p.message);
        return;
    }

    printf("\n=== Partie lancée ===\n");

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
                    printf("\n╔════════════════════════════════╗\n");
                    printf("║    🎉  VICTOIRE !  🎉         ║\n");
                    printf("╚════════════════════════════════╝\n\n");
                }
                else
                {
                    clearScreen();
                    printf("\n╔════════════════════════════════╗\n");
                    printf("║    😢  DÉFAITE...  😢         ║\n");
                    printf("╚════════════════════════════════╝\n\n");
                    afficherLePendu("0");
                }
            }
            else
            {
                // L'autre joueur a gagné/perdu
                if (strcmp(p.message, "VICTOIRE") == 0)
                {
                    clearScreen();
                    printf("\n╔════════════════════════════════╗\n");
                    printf("║    😢  DÉFAITE...  😢         ║\n");
                    printf("╚════════════════════════════════╝\n\n");
                    afficherLePendu("0");
                }
                else
                {
                    clearScreen();
                    printf("\n╔════════════════════════════════╗\n");
                    printf("║    🎉  VICTOIRE !  🎉         ║\n");
                    printf("╚════════════════════════════════╝\n\n");
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
        printf("╔════════════════════════════════════════════════════════╗\n");
        printf("║              JEU DU PENDU - EN LIGNE                  ║\n");
        printf("╚════════════════════════════════════════════════════════╝\n\n");
        printf("Mot : %s\n", motCache);
        printf("Vos essais restants : %s ❤️\n", essaisMoi);
        printf("Essais adversaire : %s 💔\n\n", essaisAdversaire);

        afficherLePendu(essaisMoi);

        if (currentID == ID_CLIENT)
        {
            printf("\n🎯 C'est votre tour !\n");
            printf("Votre lettre : ");
            fflush(stdout);
            
            if (fgets(buffer, sizeof(buffer), stdin) == NULL)
            {
                printf("Erreur de lecture.\n");
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
            printf("\n⏳ En attente du joueur %d...\n", currentID);
        }

        // Réception du retour : Bonne/Mauvaise lettre, etc.
        ret = recevoirPacket(sock, &p);
        if (ret <= 0) return;

        printf("\n");
        if (strcmp(p.message, "Bonne lettre !") == 0)
        {
            printf("✅ %s\n", p.message);
        }
        else if (strcmp(p.message, "Mauvaise lettre") == 0)
        {
            printf("❌ %s\n", p.message);
        }
        else
        {
            printf("ℹ️  %s\n", p.message);
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
        printf("\n╔════════════════════════════════════════════════════════╗\n");
        printf("║  Vous êtes le joueur #%d                               ║\n", *ID_CLIENT);
        printf("║  %s  ║\n", p.message);
        printf("╚════════════════════════════════════════════════════════╝\n");
    }
    else
    {
        printf("Erreur lors de la réception de l'ID.\n");
        return;
    }

    while (1)
    {
        printf("\n> Commandes : 'start' (jouer) | 'exit' (quitter)\n");
        printf("> Votre choix : ");
        fflush(stdout);
        
        if (fgets(buffer, sizeof(buffer), stdin) == NULL)
        {
            printf("Erreur de lecture.\n");
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
            printf("👋 Fermeture du client.\n");
            break;
        }
        else if (strcmp(buffer, "start") == 0)
        {
            envoyerPacket(sock, *ID_CLIENT, "start");
            printf("🎮 Recherche d'une partie...\n");
            
            // Le serveur peut répondre avec un message d'attente ou lancer la partie
            ret = recevoirPacket(sock, &p);
            if (ret <= 0)
            {
                printf("Erreur de réception.\n");
                break;
            }
            
            // Si c'est un message d'attente
            if (strstr(p.message, "attente") != NULL || strstr(p.message, "En attente") != NULL)
            {
                printf("⏳ %s\n", p.message);
                printf("   En attente de l'adversaire...\n");
                
                // Attendre que l'adversaire rejoigne et que le serveur lance la partie
                ret = recevoirPacket(sock, &p);
                if (ret <= 0)
                {
                    printf("Erreur de réception.\n");
                    break;
                }
                
                // Si c'est "start", on lance le jeu
                if (strcmp(p.message, "start") == 0)
                {
                    printf("✅ Adversaire trouvé ! Début de la partie...\n");
                    sleep(1);
                    jeuDuPenduV1(sock, *ID_CLIENT);
                }
                else
                {
                    printf("Message inattendu: %s\n", p.message);
                }
            }
            else if (strcmp(p.message, "start") == 0)
            {
                // Lancement immédiat
                printf("✅ Partie trouvée ! Début de la partie...\n");
                sleep(1);
                jeuDuPenduV1(sock, *ID_CLIENT);
            }
            else
            {
                printf("Serveur: %s\n", p.message);
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
                printf("Erreur de réception ou connexion fermée.\n");
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