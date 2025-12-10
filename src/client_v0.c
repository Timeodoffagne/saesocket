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
    printf("Message envoyé : '%s' (%d octets)\n", message, nb);
}

// =====================================================
//  FONCTION : recevoir une réponse du serveur
//  → retourne un pointeur sur buffer statique
// =====================================================
const char *recevoirMessage(int sock)
{
    static char buffer[256];

    memset(buffer, 0, sizeof(buffer));

    printf("En attente de la réponse du serveur...\n");
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

// =====================================================
//  FONCTION : jeu du pendu V0
// =====================================================
void jeuDuPenduV0(int sock, const char *ip_dest)
{
    static char buffer[256];
    const char *reponse;
    const char *motCache;
    const char *penduStade;

    printf("=== Début du jeu du pendu V0 ===\n");

    // --- Attente de "start x" ---
    reponse = recevoirMessage(sock);
    printf("Serveur %s : %s\n", ip_dest, reponse);

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
        if (motCache != "END")
        {
            printf("Mot : %s\n", motCache);
            // Réception du nombre d'essais
            penduStade = recevoirMessage(sock);
            printf("Essais restants : %s\n", penduStade);
            // Demande d'une lettre
            printf("Votre lettre : ");
            fgets(buffer, sizeof(buffer), stdin);
            buffer[strcspn(buffer, "\n")] = 0;
            envoyerMessage(sock, buffer);
        }

        // Réception du retour du serveur : Bonne lettre / Mauvaise lettre / VICTOIRE / DEFAITE
        reponse = recevoirMessage(sock);
        printf("Serveur %s : %s\n", ip_dest, reponse);
    }

    // --- Fin du jeu ---
    if (strcmp(reponse, "VICTOIRE") == 0)
        printf("Bravo ! Vous avez gagné !\n");
    else
        printf("Perdu !\n");

    printf("=== Fin du jeu du pendu V0 ===\n");
}

// =====================================================
//  BOUCLE PRINCIPALE DU CLIENT
// =====================================================
void boucleClient(int sock, const char *ip_dest)
{
    char buffer[256];
    while (1)
    {
        // Saisie utilisateur
        printf("Entrez un message à envoyer au serveur ('exit' pour quitter et 'start x' pour jouer au pendu V0) : ");
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
            envoyerMessage(sock, "start x");

            printf("Démarrage d'une partie de pendu V0...\n");
            jeuDuPenduV0(sock, ip_dest);

            // IMPORTANT : ne pas renvoyer "start x" une 2e fois
            continue;
        }

        // Envoi du message
        envoyerMessage(sock, buffer);

        // Réception
        const char *reponse = recevoirMessage(sock);
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
