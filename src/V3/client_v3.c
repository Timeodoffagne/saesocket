#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <string.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ctype.h>

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

    printf("[CLIENT REÇOIT] Source=%d | Message='%s'\n", p->destinataire, p->message);

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
        "assets/pendu.txt"};

    // Test des trois chemins
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

    switch (stade) {
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

int jeuDuPendu(int sock, int ID_CLIENT)
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

// =====================================================
//  PHASE D'INITIALISATION
// =====================================================
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
        envoyerPacket(sock, ID_CLIENT, init_msg);

        printf("Mot sélectionné. Attente des tentatives de C2...\n");
    }
    else // C2 : Devineur
    {
        printf("\n|--- VOUS ÊTES LE DEVINEUR (C2) ---|\n");
        printf("En attente du mot secret du Maître du Jeu (C1)...\n");

        // CORRECTION: Attendre spécifiquement READY_WORD, ignorer les autres messages
        int ready_word_recu = 0;
        while (!ready_word_recu)
        {
            ret = recevoirPacket(sock, &p);
            if (ret <= 0)
                return 0;

            // Vérifier si c'est le message READY_WORD venant de C1
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
            else if (strcmp(p.message, "PARTNER_DISCONNECTED") == 0)
            {
                printf("L'adversaire s'est déconnecté. Fin de la partie.\n");
                return 0;
            }
            else
            {
                // Ignorer les messages parasites (REPLAY, REPLAY_START, etc.)
                printf("[DEBUG] Message ignoré en attente de READY_WORD: %s\n", p.message);
            }
        }
    }

// =====================================================
//  BOUCLE DE JEU
// =====================================================
    while (currentState.essaisRestants > 0 && !partie_finie)
    {
        clearScreen();
        printf("|========================================================|\n");
        printf("|              JEU DU PENDU - RELAIS V2                  |\n");
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
            envoyerPacket(sock, ID_CLIENT, lettre);

            printf("\nEn attente de la validation du Maître du Jeu (C1)...\n");

            ret = recevoirPacket(sock, &p);
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
            else if (strcmp(p.message, "PARTNER_DISCONNECTED") == 0)
            {
                printf("L'adversaire s'est déconnecté. Fin de la partie.\n");
                return 0;
            }
        }
        else // C1 : Maître du Jeu
        {
            printf("\nEn attente de la lettre du Devineur (C2)...\n");

            ret = recevoirPacket(sock, &p);
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
                envoyerPacket(sock, ID_CLIENT, update_msg);

                if (partie_finie)
                {
                    char result_msg[LG_MESSAGE];
                    if (victoire)
                        snprintf(result_msg, LG_MESSAGE, "END_GAME:VICTOIRE:%s", currentState.motSecret);
                    else
                        snprintf(result_msg, LG_MESSAGE, "END_GAME:DEFAITE:%s", currentState.motSecret);

                    envoyerPacket(sock, ID_CLIENT, result_msg);
                }
                sleep(2);
            }
            else if (strcmp(p.message, "PARTNER_DISCONNECTED") == 0)
            {
                printf("L'adversaire s'est déconnecté. Fin de la partie.\n");
                return 0;
            }
        }
    }

// =====================================================
//  PHASE DE FIN DE PARTIE
// =====================================================
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
        // CORRECTION: Attendre END_GAME si pas déjà reçu
        if (strstr(p.message, "END_GAME:") == NULL)
        {
            int end_game_recu = 0;
            while (!end_game_recu)
            {
                ret = recevoirPacket(sock, &p);
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

// =====================================================
//  PHASE DE REJEU
// =====================================================
    printf("\n\nVoulez-vous rejouer (et inverser les rôles) ? (y/n) ");
    fflush(stdout);
    char line[4];
    if (fgets(line, sizeof(line), stdin) && (line[0] == 'y' || line[0] == 'Y'))
    {
        envoyerPacket(sock, ID_CLIENT, "REPLAY");
        printf("Demande de rejeu envoyée. Attente de l'adversaire ou du serveur...\n");
        return 1;
    }

    return 0;
}

void boucleClient(int sock, int *ID_CLIENT)
{
    char buffer[256];

    Packet p;
    int ret = recevoirPacket(sock, &p);
    if (ret > 0)
    {
        *ID_CLIENT = p.destinataire;
        printf("\n|========================================================|");
        printf("\n|  Vous êtes le joueur #%d                                |", *ID_CLIENT);
        printf("\n|  %s  |", p.message);
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
            printf("👋 Fermeture du client.\n");
            break;
        }
        else if (strcmp(buffer, "start") == 0)
        {
            envoyerPacket(sock, *ID_CLIENT, "start");
            printf("🎮 Recherche d'une partie...\n");

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
                    if (jeuDuPendu(sock, *ID_CLIENT) == 1)
                    {
                        // Attendre la réponse REPLAY_START
                        continue;
                    }
                    break;
                }
                else if (strstr(p.message, "REPLAY_START:") == p.message)
                {
                    char *id_str = p.message + strlen("REPLAY_START:");
                    int new_id = atoi(id_str);
                    *ID_CLIENT = new_id;
                    printf("Rôles inversés ! Vous êtes maintenant le joueur #%d.\n", *ID_CLIENT);
                    sleep(1);
                    
                    if (jeuDuPendu(sock, *ID_CLIENT) == 1)
                    {
                        // Attendre la réponse REPLAY_START
                        continue;
                    }
                    break;
                }
                else if (strcmp(p.message, "REPLAY") == 0)
                {
                    printf("L'adversaire a demandé une nouvelle partie. Voulez-vous accepter ? (y/n) ");
                    fflush(stdout);
                    char replay_line[4];
                    if (fgets(replay_line, sizeof(replay_line), stdin) && (replay_line[0] == 'y' || replay_line[0] == 'Y'))
                    {
                        envoyerPacket(sock, *ID_CLIENT, "REPLAY");
                        printf("Demande de rejeu acceptée. Attente du serveur...\n");
                        continue;
                    }
                    else
                    {
                        printf("Rejeu refusé.\n");
                        break;
                    }
                }
                else if (strcmp(p.message, "EN_ATTENTE_ADVERSAIRE") == 0)
                {
                    printf("En attente de l'adversaire...\n");
                }
                else
                {
                    printf("[INFO] Message serveur: %s\n", p.message);
                }
            }
        }
        else
        {
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