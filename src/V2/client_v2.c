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

typedef struct
{
    int monRole; // 1 (Choix du mot), 2 (Devineur)
    int essaisRestants;
    char motSecret[LG_MESSAGE];
    char motMasque[LG_MESSAGE];
    char lettresJouees[LG_MESSAGE];
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
    memset(&p, 0, sizeof(Packet)); // IMPORTANT: initialiser toute la structure

    p.destinataire = destinataire;
    strncpy(p.message, msg, LG_MESSAGE - 1);
    p.message[LG_MESSAGE - 1] = '\0';

    char buffer[sizeof(Packet)];
    memset(buffer, 0, sizeof(buffer)); // IMPORTANT: initialiser le buffer

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
        fprintf(stderr, "Erreur lors de l'ouverture du fichier pendu.txt. Assurez-vous qu'il est accessible via un des chemins: '../../assets/pendu.txt', '../assets/pendu.txt', 'assets/pendu.txt'.\n");
        return;
    }

    int stade = atoi(state);
    char line[256];
    int debut = 0, fin = 0;

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
//  FONCTION : jeu du pendu V2
//  RETOURNE 1 si REPLAY est demandé, 0 sinon
// =====================================================
int jeuDuPenduV2(int sock, int ID_CLIENT)
{
    Packet p;
    int ret;

    // Initialisation : mon rôle est déterminé par mon ID de connexion initial
    currentState.monRole = ID_CLIENT;
    currentState.essaisRestants = 10;
    memset(currentState.motSecret, 0, LG_MESSAGE);
    memset(currentState.motMasque, 0, LG_MESSAGE);
    memset(currentState.lettresJouees, 0, LG_MESSAGE);

    // Si je suis C1, je dois choisir le mot et initialiser la partie
    if (currentState.monRole == 1)
    {
        printf("\n|--- VOUS ÊTES LE MAÎTRE DU JEU (C1) ---|\n");
        printf("Entrez le mot secret : ");
        fflush(stdout);

        char mot[LG_MESSAGE];
        if (fgets(mot, sizeof(mot), stdin) == NULL)
            return 0;
        mot[strcspn(mot, "\n")] = 0;

        strncpy(currentState.motSecret, mot, LG_MESSAGE);

        // Créer le mot masqué initial (ex: A _ B _ )
        // On n'utilise pas la fonction masquerMot existante car elle n'est pas adaptée.
        memset(currentState.motMasque, 0, LG_MESSAGE);
        for (size_t i = 0; i < strlen(mot); i++)
        {
            strcat(currentState.motMasque, "_");
            strcat(currentState.motMasque, " "); // Ajouter un espace pour la lisibilité
        }

        // Préparer et envoyer le paquet d'initialisation à C2 (via S)
        char init_msg[LG_MESSAGE];
        snprintf(init_msg, LG_MESSAGE, "READY_WORD:%s|%d", currentState.motMasque, currentState.essaisRestants);
        envoyerPacket(sock, ID_CLIENT, init_msg); 
    }
    else
    {
        printf("\n|--- VOUS ÊTES LE DEVINEUR (C2) ---|\n");
        printf("En attente du mot secret du Maître du Jeu (C1)...\n");

        // Attendre le paquet READY_WORD de C1 (reçu avec destinataire=1)
        while (1)
        {
            ret = recevoirPacket(sock, &p);
            if (ret <= 0)
                return 0;

            if (strstr(p.message, "READY_WORD:") == p.message)
            {
                // Parser READY_WORD:<Mot Caché>|<Essais>
                char *token = strtok(p.message + strlen("READY_WORD:"), "|");
                strncpy(currentState.motMasque, token, LG_MESSAGE);
                token = strtok(NULL, "|");
                currentState.essaisRestants = atoi(token);
                printf("Partie lancée! Mot: %s (Essais: %d)\n", currentState.motMasque, currentState.essaisRestants);
                break;
            }
            else
            {
                printf("Serveur: %s\n", p.message);
            }
        }
    }

    // Boucle de jeu
    while (currentState.essaisRestants > 0 &&
           (strstr(currentState.motMasque, "_") != NULL)) // Condition de victoire: si plus de "_"
    {
        clearScreen();
        printf("|========================================================|\n");
        printf("|              JEU DU PENDU - RELAIS                     |\n");
        printf("|========================================================|\n\n");
        printf("Mot : %s\n", currentState.motMasque);
        printf("Essais restants : %d ♥\n", currentState.essaisRestants);
        printf("Lettres jouées : %s\n\n", currentState.lettresJouees);
        
        // ********** CORRECTION D'APPEL DE afficherLePendu **********
        char essais_str[4];
        sprintf(essais_str, "%d", currentState.essaisRestants);
        afficherLePendu(essais_str); 
        // ***********************************************************

        if (currentState.monRole == 2) // C2 : Devineur -> envoie sa lettre
        {
            printf("\nC'est votre tour (C2) !\n");
            printf("Votre lettre : ");
            fflush(stdout);

            char buffer[256];
            if (fgets(buffer, sizeof(buffer), stdin) == NULL)
                return 0;
            buffer[strcspn(buffer, "\n")] = 0;

            if (strlen(buffer) != 1)
                continue;

            // Envoyer la lettre (le serveur la relaye à C1)
            envoyerPacket(sock, ID_CLIENT, buffer);

            // Attendre la réponse de C1 (via S)
            printf("\nEn attente de la validation du Maître du Jeu (C1)...\n");

            ret = recevoirPacket(sock, &p);
            if (ret <= 0)
                return 0;

            if (strstr(p.message, "UPDATE:") == p.message)
            {
                // Parser UPDATE:<Mot Caché>|<Essais>|<Feedback>
                // La logique de parsing de mise à jour devrait être implémentée ici
                // ...
                printf("Feedback: %s\n", p.message);
            }
            else if (strstr(p.message, "END_GAME:") == p.message)
            {
                // ... (Logique de fin de partie)
                break;
            }
            sleep(2);
        }
        else // C1 : Maître du Jeu -> attend une lettre, traite, puis envoie l'état
        {
            printf("\nEn attente de la lettre du Devineur (C2)...\n");

            ret = recevoirPacket(sock, &p);
            if (ret <= 0)
                return 0;

            char lettre = p.message[0];
            printf("C2 joue : %c\n", lettre);

            // Logique de traitement de la lettre par C1
            char feedback[LG_MESSAGE] = "Lettre traitée.";

            // ... (Ici, intégrer la LOGIQUE du pendu de V1 : vérification, décrément essais, etc.)
            // NOTE: Cette logique doit être implémentée par l'utilisateur.
            
            // Envoi de la mise à jour à C2
            char update_msg[LG_MESSAGE];
            snprintf(update_msg, LG_MESSAGE, "UPDATE:%s|%d|%s", currentState.motMasque, currentState.essaisRestants, feedback);
            envoyerPacket(sock, ID_CLIENT, update_msg);

            // Vérifier la condition de victoire/défaite (C1 seul responsable)
            if (/* condition de fin de partie */ 0)
            {
                // Envoyer le message de fin de jeu
                envoyerPacket(sock, ID_CLIENT, "END_GAME:VICTOIRE:MOT");
                break;
            }
            sleep(2);
        }
    }

    // Logique de Rejeu 
    printf("Fin de la partie. Voulez-vous rejouer (et inverser les rôles) ? (y/n) ");
    char line[4];
    if (fgets(line, sizeof(line), stdin) && (line[0] == 'y' || line[0] == 'Y'))
    {
        envoyerPacket(sock, ID_CLIENT, "REPLAY");
        printf("Demande de rejeu envoyée. Attente de l'adversaire ou du serveur...\n");
        return 1; // REPLAY demandé
    }
    
    return 0; // REPLAY non demandé
}

// =====================================================
//  FONCTION : RECUPERATION D'UN MOT
// =====================================================
char *creationMot()
{
    printf("Entrez un mot à deviner (max %d caractères) : ", LG_MESSAGE - 1);
    fflush(stdout);
    char *mot = (char *)malloc(LG_MESSAGE * sizeof(char));
    if (fgets(mot, LG_MESSAGE, stdin) == NULL)
    {
        strcpy(mot, "exemple");
    }
    else
    {
        mot[strcspn(mot, "\n")] = 0; // Retirer le saut de ligne
    }
}

// =====================================================
//  FONCTION : MASQUER LE MOT
// =====================================================
char *masquerMot(const char *mot)
{
    int len = strlen(mot);
    char *motMasque = (char *)malloc((len + 1) * sizeof(char));
    for (int i = 0; i < len; i++)
    {
        // Cette logique ne semble pas correcte pour masquer le mot
        if (mot[i] >= 'a' && mot[i] <= 'z')
            motMasque[i] = '_';
        else if (mot[i] >= 'A' && mot[i] <= 'Z')
            motMasque[i] = '_';
        else
            motMasque[i] = mot[i]; // Garder les caractères non alphabétiques
    }
    motMasque[len] = '\0';
    return motMasque;
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
            
            int replay_requested = 0; // Flag pour gérer la demande de rejeu

            // Vérifier si c'est un message d'attente ou le début de la partie
            if (strcmp(p.message, "start") == 0)
            {
                // Le serveur a lancé la partie immédiatement
                printf("Partie lancée !\n");
                sleep(1);
                // ********** CORRECTION D'APPEL ET LOGIQUE DE REJEU **********
                if (jeuDuPenduV2(sock, *ID_CLIENT) == 1)
                {
                    replay_requested = 1;
                }
                // ************************************************************
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
                        // ********** CORRECTION D'APPEL ET LOGIQUE DE REJEU **********
                        if (jeuDuPenduV2(sock, *ID_CLIENT) == 1)
                        {
                            replay_requested = 1;
                            break; // Sortir pour relancer l'attente de rejeu
                        }
                        break;
                    } 
                    // ********** NOUVELLE LOGIQUE DE REJEU/INVERSION **********
                    else if (strstr(p.message, "REPLAY_START:") == p.message)
                    {
                        char *id_str = p.message + strlen("REPLAY_START:");
                        int new_id = atoi(id_str);
                        *ID_CLIENT = new_id;
                        printf("Rôles inversés ! Vous êtes maintenant le joueur #%d. Nouvelle partie...\n", *ID_CLIENT);
                        sleep(1);
                        if (jeuDuPenduV2(sock, *ID_CLIENT) == 0) // Si on ne redemande pas REPLAY
                        {
                            break; // On sort de la boucle d'attente et on revient au menu
                        }
                        // Si REPLAY est redemandé, on continue la boucle d'attente
                        continue;
                    }
                    else if (strcmp(p.message, "REPLAY") == 0)
                    {
                        // L'adversaire a demandé de rejouer (relais du serveur)
                        printf("L'adversaire a demandé une nouvelle partie. Voulez-vous accepter ? (y/n) ");
                        fflush(stdout);
                        char replay_line[4];
                        if (fgets(replay_line, sizeof(replay_line), stdin) && (replay_line[0] == 'y' || replay_line[0] == 'Y'))
                        {
                             envoyerPacket(sock, *ID_CLIENT, "REPLAY");
                             printf("Demande de rejeu envoyée. Attente de l'adversaire...\n");
                             continue;
                        } else {
                            // Si le client refuse, il ne fait rien, et sort de la boucle d'attente.
                            break; 
                        }
                    }
                    // *******************************************************
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

            // Si le rejeu a été demandé, on revient au début de la boucle pour gérer l'attente
            if (replay_requested) continue;

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