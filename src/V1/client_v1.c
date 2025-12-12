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
    strncpy(p.message, msg, LG_MESSAGE);

    char buffer[sizeof(Packet)];
    memcpy(buffer, &p.destinataire, sizeof(int));
    memcpy(buffer + sizeof(int), p.message, LG_MESSAGE);

    return send(sock, buffer, sizeof(Packet), 0);
}

// =====================================================
//  FONCTION : recevoir une réponse du serveur
//  -> retourne un pointeur sur buffer statique
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
        "../../assets/pendu.txt", /* when running from src/V1 */
        "../assets/pendu.txt",    /* when running from src/ */
        "assets/pendu.txt"        /* when running from project root */
    };
    FILE *file = NULL;
    int used_index = -1;
    for (int i = 0; i < (int)(sizeof(try_paths) / sizeof(try_paths[0])); ++i)
    {
        file = fopen(try_paths[i], "r");
        if (file)
        {
            used_index = i;
            break;
        }
    }
    if (!file)
    {
        fprintf(stderr, "Erreur lors de l'ouverture du fichier pendu.txt. Chemins essayés:\n");
        for (int i = 0; i < (int)(sizeof(try_paths) / sizeof(try_paths[0])); ++i)
            fprintf(stderr, "  - %s\n", try_paths[i]);
        return;
    }
    else
    {
        /* optional debug info */
        // printf("Chargement de pendu depuis : %s\n", try_paths[used_index]);
    }

    int stade = atoi(state); // Convertit "0","1","2" en int

    char line[256];
    int debut = 0;
    int fin = 0;

    switch (stade)
    {
    case 10:
        debut = 0;
        fin = 32;
        break;

    case 9:
        debut = 32;
        fin = 64;
        break;

    case 8:
        debut = 64;
        fin = 96;
        break;

    case 7:
        debut = 96;
        fin = 128;
        break;

    case 6:
        debut = 128;
        fin = 160;
        break;

    case 5:
        debut = 160;
        fin = 192;
        break;

    case 4:
        debut = 192;
        fin = 224;
        break;

    case 3:
        debut = 224;
        fin = 256;
        break;

    case 2:
        debut = 256;
        fin = 288;
        break;

    case 1:
        debut = 288;
        fin = 320;
        break;

    case 0:
        debut = 320;
        fin = 352;
        break;

    default:
        printf("[DEBUG] Stade invalide : %d\n", stade);
        fclose(file);
        return;
    }

    // Saute les lignes jusqu'au bloc voulu
    for (int i = 0; i < debut; i++)
        fgets(line, sizeof(line), file);

    // Affiche le bloc complet
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
    static char buffer[256];
    const char *reponse;
    const char *motCache;
    const char *penduStade;
    const char *penduStadeAdversaire = "";

    // --- Attente de "start x" ---
    Packet p;
    int ret = recevoirPacket(sock, &p);
    if (ret <= 0)
    {
        printf("Erreur de réception ou connexion fermée par le serveur.\n");
        return;
    }
    else
    {
        reponse = p.message;
    }
    // printf("[DEBUG] Serveur %s : %s\n", ip_dest, reponse);

    if (strcmp(reponse, "start x") != 0)
    {
        printf("Erreur : lancement refusé.\n");
        return;
    }

    // --- Boucle principale ---
    while (strcmp(reponse, "VICTOIRE") != 0 &&
           strcmp(reponse, "DEFAITE") != 0)
    {
        // Réception du mot masqué
        int currentID = 0;
        Packet p;
        int ret = recevoirPacket(sock, &p);
        if (ret <= 0)
        {
            printf("Erreur de réception ou connexion fermée par le serveur.\n");
            return;
        }
        else
        {
            motCache = p.message;
            currentID = p.destinataire;
        }

        printf("[DEBUG] motCache = %s\nLe Joueur numero %d joue ce tour", motCache, currentID);

        clearScreen();
        printf("--------------------=== Jeu du pendu V1 ===--------------------");
        if (strcmp(motCache, "END") != 0)
        {
            printf("\n\nMot : %s    ", motCache);
            // Réception du nombre d'essais
            ret = recevoirPacket(sock, &p);
            int destinataire = 0;
            if (ret <= 0)
            {
                printf("Erreur de réception ou connexion fermée par le serveur.\n");
                return;
            }
            else
            {
                destinataire = p.destinataire;
            }
            if (destinataire == ID_CLIENT)
            {
                penduStade = p.message;
            }
            else
            {
                penduStadeAdversaire = p.message;
            }
            printf("Essais restants : %s\n", penduStade);
            printf("Essais restants adversaire : %s\n", penduStadeAdversaire);
            // Demande d'une lettre
            afficherLePendu(penduStade);
            if (currentID == ID_CLIENT)
            {
                printf("Votre lettre : ");
                fgets(buffer, sizeof(buffer), stdin);
                buffer[strcspn(buffer, "\n")] = 0;
                envoyerPacket(sock, ID_CLIENT, buffer);
            }
            else
            {
                printf("En attente de la lettre de l'adversaire...\n");
            }
        }
        // Réception du retour du serveur : Bonne lettre / Mauvaise lettre / VICTOIRE / DEFAITE
        ret = recevoirPacket(sock, &p);
        int destinataire = 0;
        if (ret <= 0)
        {
            printf("Erreur de réception ou connexion fermée par le serveur.\n");
            return;
        }
        else
        {
            reponse = p.message;
            destinataire = p.destinataire;
        }
        if (destinataire != ID_CLIENT && strcmp(reponse, "VICTOIRE") == 0)
        {
            reponse = "DEFAITE";
        } else if (destinataire != ID_CLIENT && strcmp(reponse, "DEFAITE") == 0)
        {
            reponse = "VICTOIRE";
        }
        printf("[DEBUG] reponse = %s\n", reponse);
    }

    // --- Fin du jeu ---
    if (strcmp(reponse, "VICTOIRE") == 0)
    {
        printf("\n!!! Gagné !!!\n\n");
    }
    else
    {
        printf("\n!!! Perdu !!!\n\n");

        FILE *file = NULL;
        const char *paths[] = {
            "../../assets/pendu.txt",
            "../assets/pendu.txt",
            "assets/pendu.txt"};

        // Test des trois chemins
        for (int i = 0; i < 3; i++)
        {
            file = fopen(paths[i], "r");
            if (file != NULL)
            {
                // printf("[DEBUG] Fichier trouvé : %s\n", paths[i]);
                break;
            }
        }

        if (file == NULL)
        {
            perror("Impossible d'ouvrir pendu.txt");
            return;
        }

        char line[256];
        int debut = 320;
        int fin = 352;

        // Saute jusqu’à "debut"
        for (int i = 0; i < debut; i++)
        {
            if (!fgets(line, sizeof(line), file))
                break;
        }

        // Affiche le bloc final
        for (int i = debut; i < fin; i++)
        {
            if (!fgets(line, sizeof(line), file))
                break;
            printf("%s", line);
        }

        fclose(file);
    }
}

// =====================================================
//  BOUCLE PRINCIPALE DU CLIENT
// =====================================================
void boucleClient(int sock, const char *ip_dest, int ID_CLIENT)
{
    char buffer[256];
    while (1)
    {
        // Saisie utilisateur
        printf("\nEntrez un message à envoyer au serveur ('exit' pour quitter et 'start x' pour jouer au pendu V1) : ");
        fgets(buffer, sizeof(buffer), stdin);
        buffer[strcspn(buffer, "\n")] = 0; // Retirer \n

        if (strcmp(buffer, "exit") == 0)
        {
            printf("Fermeture du client.\n");
            break;
        }
        else if (strncmp(buffer, "start x", 7) == 0)
        {

            // Envoi au serveur pour lancer la partie
            envoyerPacket(sock, ID_CLIENT, "start x");

            printf("Démarrage d'une partie de pendu V1...\n");
            jeuDuPenduV1(sock, ip_dest, ID_CLIENT);

            // IMPORTANT : ne pas renvoyer "start x" une 2e fois
            continue;
        }

        // Envoi du message
        envoyerPacket(sock, ID_CLIENT, buffer);

        Packet p;
        // Réception
        int ret = recevoirPacket(sock, &p);
        char *reponse;
        if (ret <= 0)
        {
            printf("Erreur de réception ou connexion fermée par le serveur.\n");
            break;
        }
        else
        {
            reponse = p.message;
            ID_CLIENT = p.destinataire;
        }

        printf("Serveur %s : %s\n", ip_dest, reponse);
        if (strcmp(reponse, "Error") == 0)
        {
            break;
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

    // Création + connexion
    int sock = creationDeSocket(ip_dest, port_dest);

    // Boucle de communication client ↔ serveur
    boucleClient(sock, ip_dest, ID_CLIENT);

    close(sock);
    return 0;
}
