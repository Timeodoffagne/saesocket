#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <string.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ctype.h>
#include <sys/select.h>

#define LG_MESSAGE 256
#define MAX_ESSAIS 10

typedef struct
{
    int destinataire;
    char message[LG_MESSAGE];
} Packet;

typedef struct
{
    int monRole;
    int essaisRestants;
    char motSecret[LG_MESSAGE];
    char motMasque[LG_MESSAGE * 2];
    char lettresJouees[27];
} GameState;

GameState currentState;

/* -------------------------------------------------------------------------- */
int creationDeSocket(const char *ip_dest, int port_dest)
{
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
    {
        perror("Erreur en création de la socket");
        exit(EXIT_FAILURE);
    }
    printf("[SOCKET] Socket créée (%d)\n", sock);

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

    printf("[CONNEXION] Connecté à %s:%d\n", ip_dest, port_dest);
    return sock;
}

/* -------------------------------------------------------------------------- */
int creerSocketEcoute(int port)
{
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
    {
        perror("Erreur création socket d'écoute");
        return -1;
    }

    int opt = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;  // Écoute sur TOUTES les interfaces (0.0.0.0)
    addr.sin_port = htons(port);

    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        perror("Erreur bind socket d'écoute");
        close(sock);
        return -1;
    }

    if (listen(sock, 1) < 0)
    {
        perror("Erreur listen socket d'écoute");
        close(sock);
        return -1;
    }

    printf("[P2P SERVER] En écoute sur 0.0.0.0:%d (toutes interfaces)...\n", port);
    return sock;
}

/* -------------------------------------------------------------------------- */
int envoyerPacket(int sock, int destinataire, const char *msg)
{
    Packet p;
    memset(&p, 0, sizeof(Packet));

    p.destinataire = destinataire;
    strncpy(p.message, msg, LG_MESSAGE - 1);
    p.message[LG_MESSAGE - 1] = '\0';

    char buffer[sizeof(Packet)];
    memset(buffer, 0, sizeof(buffer));

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

/* -------------------------------------------------------------------------- */
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

    printf("[CLIENT REÇOIT] Source=%d | Message='%s'\n", p->destinataire, p->message);

    return rec;
}

/* -------------------------------------------------------------------------- */
void clearScreen()
{
    printf("\033[2J\033[H");
}

/* -------------------------------------------------------------------------- */
void afficherLePendu(const char *state)
{
    const char *try_paths[] = {
        "../../assets/pendu.txt",
        "../assets/pendu.txt",
        "assets/pendu.txt"};
    FILE *file = NULL;
    for (int i = 0; i < 3; ++i)
    {
        file = fopen(try_paths[i], "r");
        if (file)
            break;
    }
    if (!file)
    {
        fprintf(stderr, "Erreur lors de l'ouverture du fichier pendu.txt.\n");
        return;
    }

    int stade = atoi(state);
    char line[256];
    int debut = 0, fin = 0;

    int offset = (MAX_ESSAIS - stade) * 32;
    debut = offset;
    fin = offset + 32;

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

/* -------------------------------------------------------------------------- */
char *traiterLettre(char lettre)
{
    static char feedback[LG_MESSAGE];
    int lettre_trouvee = 0;

    lettre = toupper(lettre);

    if (strchr(currentState.lettresJouees, lettre))
    {
        snprintf(feedback, LG_MESSAGE, "La lettre '%c' a déjà été jouée.", lettre);
        return feedback;
    }

    size_t len = strlen(currentState.lettresJouees);
    currentState.lettresJouees[len] = lettre;
    currentState.lettresJouees[len + 1] = '\0';

    for (int i = 0; i < strlen(currentState.motSecret); i++)
    {
        if (toupper(currentState.motSecret[i]) == lettre)
        {
            currentState.motMasque[i * 2] = currentState.motSecret[i];
            lettre_trouvee = 1;
        }
    }

    if (lettre_trouvee)
    {
        snprintf(feedback, LG_MESSAGE, "BRAVO ! La lettre '%c' est correcte.", lettre);
    }
    else
    {
        currentState.essaisRestants--;
        snprintf(feedback, LG_MESSAGE, "DOMMAGE ! La lettre '%c' n'est pas dans le mot.", lettre);
    }

    return feedback;
}

/* -------------------------------------------------------------------------- */
int jeuDuPenduV4(int sock_p2p, int ID_CLIENT)
{
    Packet p;
    int ret;
    int partie_finie = 0;

    // Réinitialiser l'état
    currentState.monRole = ID_CLIENT;
    currentState.essaisRestants = MAX_ESSAIS;
    memset(currentState.motSecret, 0, LG_MESSAGE);
    memset(currentState.motMasque, 0, LG_MESSAGE * 2);
    memset(currentState.lettresJouees, 0, sizeof(currentState.lettresJouees));

    printf("\n╔════════════════════════════════════════════════════════════╗\n");
    printf("║              JEU DU PENDU - MODE P2P                       ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n");

    // ================== PHASE D'INITIALISATION ==================
    if (currentState.monRole == 1) // C1 : Maître du Jeu
    {
        printf("\n|--- VOUS ÊTES LE MAÎTRE DU JEU (C1) ---|\n");
        printf("Entrez le mot secret (sans espace) : ");
        fflush(stdout);

        char mot[LG_MESSAGE];
        if (fgets(mot, sizeof(mot), stdin) == NULL)
            return 0;
        mot[strcspn(mot, "\n")] = 0;

        strncpy(currentState.motSecret, mot, LG_MESSAGE);

        // Créer le mot masqué initial
        for (size_t i = 0; i < strlen(mot); i++)
        {
            currentState.motMasque[i * 2] = '_';
            currentState.motMasque[i * 2 + 1] = ' ';
        }
        currentState.motMasque[strlen(mot) * 2] = '\0';

        // Envoyer le paquet d'initialisation à C2
        char init_msg[LG_MESSAGE];
        snprintf(init_msg, LG_MESSAGE, "READY_WORD:%s|%d", currentState.motMasque, currentState.essaisRestants);
        envoyerPacket(sock_p2p, ID_CLIENT, init_msg);

        printf("Mot sélectionné. Attente des tentatives de C2...\n");
    }
    else // C2 : Devineur
    {
        printf("\n|--- VOUS ÊTES LE DEVINEUR (C2) ---|\n");
        printf("En attente du mot secret du Maître du Jeu (C1)...\n");

        // Attendre spécifiquement READY_WORD
        int ready_word_recu = 0;
        while (!ready_word_recu)
        {
            ret = recevoirPacket(sock_p2p, &p);
            if (ret <= 0)
                return 0;

            if (strstr(p.message, "READY_WORD:") == p.message)
            {
                char temp_msg[LG_MESSAGE];
                strncpy(temp_msg, p.message + strlen("READY_WORD:"), LG_MESSAGE);

                char *token = strtok(temp_msg, "|");
                if (token)
                    strncpy(currentState.motMasque, token, sizeof(currentState.motMasque));

                token = strtok(NULL, "|");
                if (token)
                    currentState.essaisRestants = atoi(token);

                printf("Partie lancée! Mot: %s (Essais: %d)\n", currentState.motMasque, currentState.essaisRestants);
                ready_word_recu = 1;
            }
            else
            {
                printf("[DEBUG] Message ignoré en attente de READY_WORD: %s\n", p.message);
            }
        }
    }

    // ================== BOUCLE DE JEU ==================
    while (currentState.essaisRestants > 0 && !partie_finie)
    {
        clearScreen();
        printf("|========================================================|\n");
        printf("|              JEU DU PENDU - P2P V4                     |\n");
        printf("|========================================================|\n\n");
        printf("Mot : %s\n", currentState.motMasque);
        printf("Essais restants : %d ♥\n", currentState.essaisRestants);
        printf("Lettres jouées : %s\n\n", currentState.lettresJouees);

        char essais_str[4];
        sprintf(essais_str, "%d", currentState.essaisRestants);
        afficherLePendu(essais_str);

        if (currentState.monRole == 2) // C2 : Devineur
        {
            printf("\nC'est votre tour (C2) : Deviner.\n");
            printf("Votre lettre : ");
            fflush(stdout);

            char buffer[256];
            if (fgets(buffer, sizeof(buffer), stdin) == NULL)
                return 0;
            buffer[strcspn(buffer, "\n")] = 0;

            if (strlen(buffer) != 1 || !isalpha(buffer[0]))
            {
                printf("Entrée invalide. Une seule lettre alphabétique attendue.\n");
                sleep(1);
                continue;
            }

            char lettre[2] = {toupper(buffer[0]), '\0'};
            envoyerPacket(sock_p2p, ID_CLIENT, lettre);

            printf("\nEn attente de la validation du Maître du Jeu (C1)...\n");

            ret = recevoirPacket(sock_p2p, &p);
            if (ret <= 0)
                return 0;

            if (strstr(p.message, "UPDATE:") == p.message)
            {
                char temp_msg[LG_MESSAGE];
                strncpy(temp_msg, p.message + strlen("UPDATE:"), LG_MESSAGE);

                char *token = strtok(temp_msg, "|");
                if (token)
                    strncpy(currentState.motMasque, token, sizeof(currentState.motMasque));

                token = strtok(NULL, "|");
                if (token)
                    currentState.essaisRestants = atoi(token);

                char *feedback_token = strtok(NULL, "|");
                if (feedback_token)
                    printf("-> RÉSULTAT : %s\n", feedback_token);

                char *lettres_jouees_token = strtok(NULL, "|");
                if (lettres_jouees_token)
                    strncpy(currentState.lettresJouees, lettres_jouees_token, sizeof(currentState.lettresJouees));

                if (currentState.essaisRestants <= 0 || strstr(currentState.motMasque, "_") == NULL)
                {
                    partie_finie = 1;
                }

                sleep(2);
            }
            else if (strstr(p.message, "END_GAME:") == p.message)
            {
                partie_finie = 1;
            }
        }
        else // C1 : Maître du Jeu
        {
            printf("\nEn attente de la lettre du Devineur (C2)...\n");

            ret = recevoirPacket(sock_p2p, &p);
            if (ret <= 0)
                return 0;

            if (strlen(p.message) == 1 && isalpha(p.message[0]))
            {
                char lettre = p.message[0];
                printf("C2 joue : %c\n", lettre);

                char *feedback = traiterLettre(lettre);

                int victoire = (strstr(currentState.motMasque, "_") == NULL);
                int defaite = (currentState.essaisRestants <= 0);

                partie_finie = victoire || defaite;

                char update_msg[LG_MESSAGE];
                snprintf(update_msg, LG_MESSAGE, "UPDATE:%s|%d|%s|%s",
                         currentState.motMasque,
                         currentState.essaisRestants,
                         feedback,
                         currentState.lettresJouees);
                envoyerPacket(sock_p2p, ID_CLIENT, update_msg);

                if (partie_finie)
                {
                    char result_msg[LG_MESSAGE];
                    if (victoire)
                        snprintf(result_msg, LG_MESSAGE, "END_GAME:VICTOIRE:%s", currentState.motSecret);
                    else
                        snprintf(result_msg, LG_MESSAGE, "END_GAME:DEFAITE:%s", currentState.motSecret);

                    envoyerPacket(sock_p2p, ID_CLIENT, result_msg);
                }
                sleep(2);
            }
        }
    }

    // ================== PHASE DE FIN DE PARTIE ==================
    clearScreen();
    printf("|========================================================|\n");
    printf("|              PARTIE TERMINÉE                           |\n");
    printf("|========================================================|\n\n");

    if (currentState.monRole == 1)
    {
        if (strstr(currentState.motMasque, "_") == NULL)
        {
            printf("RÉSULTAT: Le devineur (C2) a GAGNÉ ! Mot secret: %s\n", currentState.motSecret);
        }
        else
        {
            printf("RÉSULTAT: Le devineur (C2) a PERDU ! Mot secret: %s\n", currentState.motSecret);
        }
    }
    else
    {
        // Attendre END_GAME si pas déjà reçu
        if (strstr(p.message, "END_GAME:") == NULL)
        {
            int end_game_recu = 0;
            while (!end_game_recu)
            {
                ret = recevoirPacket(sock_p2p, &p);
                if (ret <= 0)
                    return 0;

                if (strstr(p.message, "END_GAME:") == p.message)
                {
                    end_game_recu = 1;
                }
                else
                {
                    printf("[DEBUG] Message ignoré en attente de END_GAME: %s\n", p.message);
                }
            }
        }

        if (strstr(p.message, "END_GAME:") == p.message)
        {
            char temp_msg[LG_MESSAGE];
            strncpy(temp_msg, p.message + strlen("END_GAME:"), LG_MESSAGE);

            char *result = strtok(temp_msg, ":");
            char *mot_secret = strtok(NULL, ":");

            printf("RÉSULTAT FINAL : %s\n", result);
            printf("Le mot secret était : %s\n", mot_secret);
        }
    }

    // ================== PHASE DE REJEU ==================
    printf("\n\nVoulez-vous rejouer (et inverser les rôles) ? (y/n) ");
    fflush(stdout);
    char line[4];
    if (fgets(line, sizeof(line), stdin) && (line[0] == 'y' || line[0] == 'Y'))
    {
        printf("✓ Rejeu avec inversion des rôles en P2P (sans retour au serveur)\n");
        return 1; // Signal de rejeu
    }

    return 0;
}

/* -------------------------------------------------------------------------- */
void boucleClientP2P(int sock_matchmaking)
{
    Packet p;
    int ret;
    int ID_CLIENT = 0;

    // Recevoir l'ID initial du serveur
    ret = recevoirPacket(sock_matchmaking, &p);
    if (ret > 0)
    {
        ID_CLIENT = p.destinataire;
        printf("\n╔════════════════════════════════════════════════════════════╗\n");
        printf("║  Vous êtes le joueur #%d                                    ║\n", ID_CLIENT);
        printf("║  %s  ║\n", p.message);
        printf("╚════════════════════════════════════════════════════════════╝\n");
    }
    else
    {
        printf("[ERREUR] lors de la réception de l'ID.\n");
        return;
    }

    // Demander au joueur s'il veut commencer
    printf("\n> Tapez 'start' pour rechercher une partie : ");
    fflush(stdout);
    
    char buffer[256];
    if (fgets(buffer, sizeof(buffer), stdin) == NULL)
    {
        printf("[ERREUR] de lecture.\n");
        return;
    }
    buffer[strcspn(buffer, "\n")] = 0;
    
    if (strcmp(buffer, "start") != 0)
    {
        printf("Commande invalide. Au revoir !\n");
        return;
    }
    
    printf("\n🎮 Recherche d'une partie...\n");
    envoyerPacket(sock_matchmaking, ID_CLIENT, "start");

    // Attendre les informations P2P du serveur
    ret = recevoirPacket(sock_matchmaking, &p);
    if (ret <= 0)
    {
        printf("[ERREUR] Connexion perdue avec le serveur\n");
        return;
    }

    int sock_p2p = -1;

    // C1 devient serveur P2P
    if (strstr(p.message, "P2P_SERVER:") == p.message)
    {
        int port = atoi(p.message + strlen("P2P_SERVER:"));
        printf("\n[P2P] Vous êtes le serveur P2P. Écoute sur le port %d\n", port);

        int listen_sock = creerSocketEcoute(port);
        if (listen_sock < 0)
        {
            printf("[ERREUR] Impossible de créer le socket d'écoute P2P\n");
            return;
        }

        printf("[P2P] ✓ Socket d'écoute créé\n");
        
        // IMPORTANT : Informer le serveur qu'on écoute MAINTENANT
        // Le serveur va attendre ce signal avant de dire à C2 de se connecter
        envoyerPacket(sock_matchmaking, 1, "P2P_LISTENING");
        printf("[P2P] ✓ Signal 'P2P_LISTENING' envoyé au serveur\n");

        printf("[P2P] En attente de la connexion de C2...\n");

        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        sock_p2p = accept(listen_sock, (struct sockaddr *)&client_addr, &addr_len);

        if (sock_p2p < 0)
        {
            perror("accept P2P");
            close(listen_sock);
            return;
        }

        printf("[P2P] ✓ C2 connecté depuis %s\n", inet_ntoa(client_addr.sin_addr));
        close(listen_sock);
    }
    // C2 se connecte à C1
    else if (strstr(p.message, "P2P_CONNECT:") == p.message)
    {
        char temp[LG_MESSAGE];
        strncpy(temp, p.message + strlen("P2P_CONNECT:"), LG_MESSAGE);

        char *ip = strtok(temp, ":");
        char *port_str = strtok(NULL, ":");

        if (!ip || !port_str)
        {
            printf("[ERREUR] Format P2P_CONNECT invalide\n");
            return;
        }

        int port = atoi(port_str);
        printf("\n[P2P] Connexion à C1 (%s:%d)...\n", ip, port);

        // Tentatives de connexion (C1 pourrait ne pas être prêt immédiatement)
        int tentatives = 0;
        while (tentatives < 15)  // Augmenté à 15 tentatives
        {
            sock_p2p = socket(AF_INET, SOCK_STREAM, 0);
            if (sock_p2p < 0)
            {
                perror("socket P2P");
                return;
            }

            struct sockaddr_in serv;
            memset(&serv, 0, sizeof(serv));
            serv.sin_family = AF_INET;
            serv.sin_port = htons(port);
            inet_aton(ip, &serv.sin_addr);

            if (connect(sock_p2p, (struct sockaddr *)&serv, sizeof(serv)) == 0)
            {
                printf("[P2P] ✓ Connecté à C1 en P2P\n");
                break;
            }

            close(sock_p2p);
            sock_p2p = -1;
            tentatives++;
            printf("[P2P] Tentative %d/15... (attente 2s)\n", tentatives);
            sleep(2);  // Augmenté à 2 secondes
        }

        if (sock_p2p < 0)
        {
            printf("[ERREUR] Impossible de se connecter à C1\n");
            return;
        }
    }
    else
    {
        printf("[ERREUR] Message inattendu du serveur: %s\n", p.message);
        return;
    }

    // Fermer la connexion avec le serveur de matchmaking
    close(sock_matchmaking);
    printf("\n[P2P] ✓ Connexion P2P établie ! Le serveur est libéré.\n");
    printf("[P2P] ✓ Communication directe entre les deux joueurs.\n");
    sleep(1);

    // Boucle de jeu avec rejeu possible en P2P
    int continuer = 1;
    while (continuer)
    {
        int rejeu = jeuDuPenduV4(sock_p2p, ID_CLIENT);

        if (rejeu == 1)
        {
            // Inverser les rôles
            ID_CLIENT = (ID_CLIENT == 1) ? 2 : 1;
            printf("\n🔄 Rôles inversés ! Vous êtes maintenant le joueur #%d\n", ID_CLIENT);
            sleep(2);
        }
        else
        {
            continuer = 0;
        }
    }

    close(sock_p2p);
    printf("\n👋 Fin de la session P2P.\n");
}

/* -------------------------------------------------------------------------- */
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

    printf("\n╔════════════════════════════════════════════════════════════╗\n");
    printf("║          CLIENT V4 - MODE P2P                              ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n");

    int sock = creationDeSocket(ip_dest, port_dest);
    boucleClientP2P(sock);

    printf("\n[ARRÊT] Client terminé.\n");
    return 0;
}