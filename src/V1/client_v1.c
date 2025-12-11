#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <string.h>
#include <netinet/in.h>
#include <arpa/inet.h>

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
//  FONCTION : envoyer un message
// =====================================================
void envoyerMessage(int sock, const char *message)
{
    int nb = send(sock, message, strlen(message) + 1, 0);
    if (nb <= 0)
    {
        perror("Erreur en écriture (send)");
        close(sock);
        exit(EXIT_FAILURE);
    }
    // printf("[DEBUG] Message envoyé : '%s' (%d octets)\n", message, nb);
}

// =====================================================
//  FONCTION : recevoir une réponse du serveur
//  -> retourne un pointeur sur buffer statique
// =====================================================
const char *recevoirMessage(int sock)
{
    static char buffer[256];

    memset(buffer, 0, sizeof(buffer));

    // printf("[DEBUG] En attente de la réponse du serveur...\n");
    int nb = recv(sock, buffer, sizeof(buffer) - 1, 0);

    if (nb < 0)
    {
        perror("Erreur en lecture (recv)");
        close(sock);
        exit(EXIT_FAILURE);
    }
    else if (nb == 0)
    {
        printf("Serveur déconnecté.\n");
        close(sock);
        return "Error";
    }

    return buffer;
}

void clearScreen()
{
    printf("\033[2J\033[H");
}
void afficherLePendu(const char *state)
{
    const char *try_paths[] = {
        "../../assets/pendu.txt", /* when running from src/V0 */
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

    int stade = atoi(state);  // Convertit "0","1","2" en int

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
//  FONCTION : jeu du pendu V0
// =====================================================
void jeuDuPenduV0(int sock, const char *ip_dest)
{
    static char buffer[256];
    const char *reponse;
    const char *motCache;
    const char *penduStade;

    // --- Attente de "start x" ---
    reponse = recevoirMessage(sock);
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
        motCache = recevoirMessage(sock);
        clearScreen();
        printf("--------------------=== Jeu du pendu V0 ===--------------------");
        // printf("[DEBUG] motCache = %s\n", motCache);
        if (strcmp(motCache, "END") != 0)
        {
            printf("\n\nMot : %s    ", motCache);
            // Réception du nombre d'essais
            penduStade = recevoirMessage(sock);
            printf("Essais restants : %s\n", penduStade);
            // Demande d'une lettre
            afficherLePendu(penduStade);
            printf("Votre lettre : ");
            fgets(buffer, sizeof(buffer), stdin);
            buffer[strcspn(buffer, "\n")] = 0;
            envoyerMessage(sock, buffer);
        }
        // Réception du retour du serveur : Bonne lettre / Mauvaise lettre / VICTOIRE / DEFAITE
        reponse = recevoirMessage(sock);
        // printf("[DEBUG] reponse = %s\n", reponse);
    }

    // --- Fin du jeu ---
    if (strcmp(reponse, "VICTOIRE") == 0) {
    printf("\n!!! Gagné !!!\n\n");
    } else {
        printf("\n!!! Perdu !!!\n\n");

        FILE *file = NULL;
        const char *paths[] = {
            "../../assets/pendu.txt",
            "../assets/pendu.txt",
            "assets/pendu.txt"
        };

        // Test des trois chemins
        for (int i = 0; i < 3; i++) {
            file = fopen(paths[i], "r");
            if (file != NULL) {
                // printf("[DEBUG] Fichier trouvé : %s\n", paths[i]);
                break;
            }
        }

        if (file == NULL) {
            perror("Impossible d'ouvrir pendu.txt");
            return;
        }

        char line[256];
        int debut = 320;
        int fin = 352;

        // Saute jusqu’à "debut"
        for (int i = 0; i < debut; i++) {
            if (!fgets(line, sizeof(line), file))
                break;
        }

        // Affiche le bloc final
        for (int i = debut; i < fin; i++) {
            if (!fgets(line, sizeof(line), file))
                break;
            printf("%s", line);
        }

        fclose(file);
    }

}

/* Nouveau : jeu du pendu en mode 2 joueurs (V1) */
void jeuDuPenduV1(int sock)
{
    char buffer[256];
    const char *msg;

    /* Attendre message initial : "WAIT <len>" ou "START <len>" */
    msg = recevoirMessage(sock);
    if (!msg) return;

    if (strncmp(msg, "WAIT", 4) == 0)
    {
        printf("Connecté en tant que joueur 1 : attente du second joueur...\n");
        printf("Info serveur : %s\n", msg);
    }
    else if (strncmp(msg, "START", 5) == 0)
    {
        printf("Connecté en tant que joueur 2 : vous jouez en premier !\n");
        printf("Info serveur : %s\n", msg);
    }
    else
    {
        printf("Message inattendu du serveur : %s\n", msg);
        return;
    }

    /* Boucle principale : traiter TURN / UPDATE / WIN / LOSE / OPP_DISCONNECT */
    while (1)
    {
        msg = recevoirMessage(sock);
        if (!msg) return;

        if (strcmp(msg, "TURN") == 0)
        {
            /* On reçoit mot masqué puis essais */
            const char *motCache = recevoirMessage(sock);
            const char *essais = recevoirMessage(sock);
            clearScreen();
            printf("C'est votre tour.\nMot : %s\nEssais restants : %s\n", motCache, essais);
            afficherLePendu(essais);
            printf("Votre lettre : ");
            if (fgets(buffer, sizeof(buffer), stdin) == NULL) return;
            buffer[strcspn(buffer, "\n")] = 0;
            envoyerMessage(sock, buffer);

            /* recevoir feedback */
            const char *feedback = recevoirMessage(sock);
            if (!feedback) return;
            printf("Serveur : %s\n", feedback);
        }
        else if (strcmp(msg, "UPDATE") == 0)
        {
            const char *motCache = recevoirMessage(sock);
            const char *essais = recevoirMessage(sock);
            clearScreen();
            printf("État mis à jour :\nMot : %s\nEssais restants : %s\n", motCache, essais);
            afficherLePendu(essais);
        }
        else if (strcmp(msg, "WIN") == 0)
        {
            printf("\n!!! Vous avez gagné !!!\n");
            break;
        }
        else if (strcmp(msg, "LOSE") == 0)
        {
            printf("\n!!! Vous avez perdu !!!\n");
            break;
        }
        else if (strcmp(msg, "OPP_DISCONNECT") == 0)
        {
            printf("L'adversaire s'est déconnecté. Fin de la partie.\n");
            break;
        }
        else
        {
            /* message libre du serveur */
            printf("Serveur : %s\n", msg);
        }
    }
}

/* Boucle principale du client V1 : après connexion, lancer jeu V1 */
void boucleClient(int sock, const char *ip_dest)
{
    /* Attendre le premier message du serveur (WAIT ou START) et gérer la partie */
    jeuDuPenduV1(sock);

    /* Après la partie on revient au shell utilisateur si besoin (ici on ferme) */
    printf("Fin de la session V1.\n");
}

// =====================================================
//  MAIN
// =====================================================
int main(int argc, char *argv[])
{
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
    boucleClient(sock, ip_dest);

    close(sock);
    return 0;
}
