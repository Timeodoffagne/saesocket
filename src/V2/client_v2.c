#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <string.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ctype.h> // Pour toupper

#define LG_MESSAGE 256
#define MAX_ESSAIS 10

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
    char motMasque[LG_MESSAGE * 2]; // Le mot masqué a des espaces: "_ L _"
    char lettresJouees[27]; // 26 lettres + '\0'
} GameState;

GameState currentState;

// Déclaration anticipée
void jeuDuPendu(int sock, int ID_CLIENT);

// =====================================================
//  FONCTION : création + connexion socket
// =====================================================
int creationDeSocket(const char *ip_dest, int port_dest)
{
    // ... (code inchangé)
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
void afficherLePendu(int essaisRestants)
{
    // Utiliser directement l'état des essais restants pour déterminer l'image
    int stade = (MAX_ESSAIS - essaisRestants); 
    
    // Remarque : Le code d'ouverture de fichier de la V1 est très spécifique
    // à un chemin de fichier. Pour fonctionner, vous devez vous assurer
    // que le fichier 'pendu.txt' est accessible dans un des chemins de recherche.
    
    // Pour l'exemple, affichons un message simple :
    printf("\n[IMAGE PENDU : ÉTAPE %d/%d]\n", stade, MAX_ESSAIS);
    // (Conservez votre fonction originale si vous avez bien le fichier 'pendu.txt')
}

// =====================================================
//  FONCTION : LOGIQUE DE JEU C1 (Maître du Jeu)
// =====================================================
// Met à jour GameState en fonction de la lettre, retourne le feedback
char *traiterLettre(char lettre)
{
    static char feedback[LG_MESSAGE];
    int lettre_trouvee = 0;
    
    lettre = toupper(lettre); // Traiter en majuscule

    // 1. Déjà jouée
    if (strchr(currentState.lettresJouees, lettre))
    {
        snprintf(feedback, LG_MESSAGE, "La lettre '%c' a déjà été jouée.", lettre);
        return feedback;
    }

    // 2. Ajouter la lettre à la liste jouée
    size_t len = strlen(currentState.lettresJouees);
    currentState.lettresJouees[len] = lettre;
    currentState.lettresJouees[len + 1] = '\0';
    
    // 3. Remplacer les "_" dans le mot masqué
    for (int i = 0; i < strlen(currentState.motSecret); i++)
    {
        if (toupper(currentState.motSecret[i]) == lettre)
        {
            // Le mot masqué a le format "L _ E _ T _ T _ R _ E" (2*i pour la lettre, 2*i+1 pour l'espace)
            currentState.motMasque[i * 2] = currentState.motSecret[i]; 
            lettre_trouvee = 1;
        }
    }

    // 4. Mettre à jour les essais
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


// =====================================================
//  FONCTION : jeu du pendu
// =====================================================
void jeuDuPendu(int sock, int ID_CLIENT)
{
    Packet p;
    int ret;
    int partie_finie = 0;

    // Réinitialiser l'état pour la nouvelle partie
    currentState.monRole = ID_CLIENT;
    currentState.essaisRestants = MAX_ESSAIS;
    memset(currentState.motSecret, 0, LG_MESSAGE);
    memset(currentState.motMasque, 0, LG_MESSAGE * 2);
    memset(currentState.lettresJouees, 0, sizeof(currentState.lettresJouees));

    // ================== PHASE D'INITIALISATION ==================
    if (currentState.monRole == 1) // C1 : Maître du Jeu
    {
        printf("\n|--- VOUS ÊTES LE MAÎTRE DU JEU (C1) ---|\n");
        printf("Entrez le mot secret : ");
        fflush(stdout);

        char mot[LG_MESSAGE];
        if (fgets(mot, sizeof(mot), stdin) == NULL)
            return;
        mot[strcspn(mot, "\n")] = 0;

        strncpy(currentState.motSecret, mot, LG_MESSAGE);

        // Créer le mot masqué initial (ex: A _ B _ )
        for (size_t i = 0; i < strlen(mot); i++)
        {
            currentState.motMasque[i * 2] = '_';
            currentState.motMasque[i * 2 + 1] = ' ';
        }
        currentState.motMasque[strlen(mot) * 2] = '\0'; // Fin de chaîne

        // Envoyer le paquet d'initialisation à C2 (via S)
        char init_msg[LG_MESSAGE];
        snprintf(init_msg, LG_MESSAGE, "READY_WORD:%s|%d", currentState.motMasque, currentState.essaisRestants);
        envoyerPacket(sock, ID_CLIENT, init_msg);
        
        printf("Mot sélectionné. Attente des tentatives de C2...\n");
    }
    else // C2 : Devineur
    {
        printf("\n|--- VOUS ÊTES LE DEVINEUR (C2) ---|\n");
        printf("En attente du mot secret du Maître du Jeu (C1)...\n");

        // Attendre le paquet READY_WORD de C1 (source=1)
        while (1)
        {
            ret = recevoirPacket(sock, &p);
            if (ret <= 0)
                return;

            if (p.destinataire == 1 && strstr(p.message, "READY_WORD:") == p.message)
            {
                char temp_msg[LG_MESSAGE];
                strncpy(temp_msg, p.message + strlen("READY_WORD:"), LG_MESSAGE);

                char *token = strtok(temp_msg, "|");
                if (token) strncpy(currentState.motMasque, token, sizeof(currentState.motMasque));
                
                token = strtok(NULL, "|");
                if (token) currentState.essaisRestants = atoi(token);
                
                printf("Partie lancée! Mot: %s (Essais: %d)\n", currentState.motMasque, currentState.essaisRestants);
                break;
            }
            else if (strcmp(p.message, "PARTNER_DISCONNECTED") == 0) {
                printf("L'adversaire s'est déconnecté. Fin de la partie.\n");
                return;
            }
            else
            {
                printf("Message serveur : %s\n", p.message);
            }
        }
    }

    // ================== BOUCLE DE JEU ==================
    while (currentState.essaisRestants > 0 && !partie_finie)
    {
        clearScreen();
        printf("|========================================================|\n");
        printf("|              JEU DU PENDU - RELAIS V2                  |\n");
        printf("|========================================================|\n\n");
        printf("Mot : %s\n", currentState.motMasque);
        printf("Essais restants : %d ♥\n", currentState.essaisRestants);
        printf("Lettres jouées : %s\n\n", currentState.lettresJouees);
        afficherLePendu(currentState.essaisRestants);

        if (currentState.monRole == 2) // C2 : Devineur -> envoie sa lettre et attend
        {
            printf("\nC'est votre tour (C2) : Deviner.\n");
            printf("Votre lettre : ");
            fflush(stdout);

            char buffer[256];
            if (fgets(buffer, sizeof(buffer), stdin) == NULL)
                return;
            buffer[strcspn(buffer, "\n")] = 0;

            if (strlen(buffer) != 1 || !isalpha(buffer[0]))
            {
                printf("Entrée invalide. Une seule lettre alphabétique attendue.\n");
                sleep(1);
                continue;
            }

            // Envoyer la lettre (le serveur la relaye à C1)
            char lettre[2] = {toupper(buffer[0]), '\0'};
            envoyerPacket(sock, ID_CLIENT, lettre);

            // Attendre la réponse de C1 (via S)
            printf("\nEn attente de la validation du Maître du Jeu (C1)...\n");
            
            ret = recevoirPacket(sock, &p);
            if (ret <= 0) return;
            
            if (p.destinataire == 1 && strstr(p.message, "UPDATE:") == p.message)
            {
                char temp_msg[LG_MESSAGE];
                strncpy(temp_msg, p.message + strlen("UPDATE:"), LG_MESSAGE);

                // Parser UPDATE:<Mot Caché>|<Essais>|<Feedback>
                char *token = strtok(temp_msg, "|");
                if (token) strncpy(currentState.motMasque, token, sizeof(currentState.motMasque));
                
                token = strtok(NULL, "|");
                if (token) currentState.essaisRestants = atoi(token);
                
                char *feedback_token = strtok(NULL, "|");
                if (feedback_token) printf("-> RÉSULTAT : %s\n", feedback_token);
                
                sleep(2);
            }
            else if (p.destinataire == 1 && strstr(p.message, "END_GAME:") == p.message)
            {
                partie_finie = 1;
            }
            else if (strcmp(p.message, "PARTNER_DISCONNECTED") == 0) {
                printf("L'adversaire s'est déconnecté. Fin de la partie.\n");
                return;
            }
        }
        else // C1 : Maître du Jeu -> attend une lettre, traite, puis envoie l'état
        {
            printf("\nEn attente de la lettre du Devineur (C2)...\n");

            ret = recevoirPacket(sock, &p);
            if (ret <= 0) return;
            
            if (p.destinataire == 2 && strlen(p.message) == 1 && isalpha(p.message[0]))
            {
                char lettre = p.message[0];
                printf("C2 joue : %c\n", lettre);

                // LOGIQUE DE JEU CÔTÉ C1
                char *feedback = traiterLettre(lettre);

                // Vérifier la condition de victoire/défaite (C1 seul responsable)
                int victoire = (strstr(currentState.motMasque, "_") == NULL);
                int defaite = (currentState.essaisRestants <= 0);
                
                partie_finie = victoire || defaite;

                // Envoi de la mise à jour à C2
                char update_msg[LG_MESSAGE];
                snprintf(update_msg, LG_MESSAGE, "UPDATE:%s|%d|%s", currentState.motMasque, currentState.essaisRestants, feedback);
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
            else if (strcmp(p.message, "PARTNER_DISCONNECTED") == 0) {
                printf("L'adversaire s'est déconnecté. Fin de la partie.\n");
                return;
            }
        }
    }
    
    // ================== PHASE DE FIN DE PARTIE ==================
    clearScreen();
    printf("|========================================================|\n");
    printf("|              PARTIE TERMINÉE                           |\n");
    printf("|========================================================|\n\n");
    
    // Afficher le résultat final
    if (currentState.monRole == 1) // C1 (Maître) a déjà la logique
    {
        if (strstr(currentState.motMasque, "_") == NULL) {
             printf("RÉSULTAT: Le devineur (C2) a GAGNÉ ! Mot secret: %s\n", currentState.motSecret);
        } else {
             printf("RÉSULTAT: Le devineur (C2) a PERDU ! Mot secret: %s\n", currentState.motSecret);
        }
    } 
    else // C2 (Devineur) doit attendre le message de fin de jeu
    {
        if (!partie_finie) // S'assurer qu'on reçoit le dernier message si la boucle s'est terminée par déconnexion ou timeout
        {
            ret = recevoirPacket(sock, &p);
            if (ret <= 0) return;
        }

        if (p.destinataire == 1 && strstr(p.message, "END_GAME:") == p.message)
        {
            char temp_msg[LG_MESSAGE];
            strncpy(temp_msg, p.message + strlen("END_GAME:"), LG_MESSAGE);

            char *result = strtok(temp_msg, ":");
            char *mot_secret = strtok(NULL, ":");

            printf("RÉSULTAT FINAL : %s\n", result);
            printf("Le mot secret était : %s\n", mot_secret);
        }
    }


    // ================== PHASE DE REJEU ET SWAP ==================
    printf("\n\nVoulez-vous rejouer (et inverser les rôles) ? (y/n) ");
    char line[4];
    if (fgets(line, sizeof(line), stdin) && (line[0] == 'y' || line[0] == 'Y'))
    {
        envoyerPacket(sock, ID_CLIENT, "REPLAY");
        printf("Demande de rejeu envoyée. Attente de l'adversaire...\n");
        
        // Attendre le signal de ROLE_SWAP du serveur
        while(1) {
            ret = recevoirPacket(sock, &p);
            if (ret <= 0) return;
            
            if (strstr(p.message, "ROLE_SWAP:") == p.message) {
                int new_role = atoi(p.message + strlen("ROLE_SWAP:"));
                printf("\n\n[INFO] RÔLES INVERSÉS ! Votre nouveau rôle est : C%d\n", new_role);
                *(&ID_CLIENT) = new_role; // Mise à jour de l'ID_CLIENT
                break;
            }
            else if (strcmp(p.message, "PARTNER_DISCONNECTED") == 0) {
                printf("L'adversaire s'est déconnecté. Impossible de rejouer.\n");
                return;
            }
            else if (strcmp(p.message, "REPLAY_REQUESTED") == 0) {
                printf("L'adversaire a aussi demandé le rejeu. Le serveur traite l'inversion...\n");
            }
        }
    }
}

// =====================================================
//  BOUCLE PRINCIPALE DU CLIENT
// =====================================================
void boucleClient(int sock, int *ID_CLIENT)
{
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

    int rejouer = 1;
    while (rejouer) // Boucle pour le rejeu
    {
        char buffer[256];
        printf("\n> Commandes : 'start' (jouer) | 'exit' (quitter)\n");
        printf("> Votre choix : ");
        fflush(stdout);

        if (fgets(buffer, sizeof(buffer), stdin) == NULL)
            break;

        buffer[strcspn(buffer, "\n")] = 0;

        if (strcmp(buffer, "exit") == 0)
        {
            envoyerPacket(sock, *ID_CLIENT, "exit");
            printf("Fermeture du client.\n");
            rejouer = 0;
        }
        else if (strcmp(buffer, "start") == 0)
        {
            // Le serveur ne gère plus la logique "start", mais les clients l'utilisent comme un signal
            // Dans cette version relais, le client 1 (Maître) commence la partie dès que les deux sont connectés.
            // On envoie le "start" pour signaler au serveur qu'on est prêt.
            envoyerPacket(sock, *ID_CLIENT, "start");
            
            // Attendre le début effectif de la partie (le Maître du Jeu envoie le mot)
            printf("Recherche d'une partie...\n");
            
            // On suppose que la partie commence immédiatement après le "start"
            // (ou après que le C2 reçoive le mot de C1 via le serveur)
            jeuDuPendu(sock, *ID_CLIENT);
            
            // Si le rôle a été inversé dans jeuDuPendu, ID_CLIENT a été mis à jour
            if (currentState.monRole != *ID_CLIENT)
                *ID_CLIENT = currentState.monRole;
            
        }
        else
        {
            printf("Commande inconnue.\n");
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