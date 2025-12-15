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

typedef struct
{
    int destinataire;
    char message[LG_MESSAGE];
} Packet;

/* ========================================================================== */
/*                          FONCTIONS RÉSEAU                                  */
/* ========================================================================== */

int creationDeSocket(const char *ip_dest, int port_dest)
{
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
    {
        perror("Erreur création socket");
        exit(EXIT_FAILURE);
    }
    printf("[CLIENT] Socket créée (%d)\n", sock);

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
        perror("Erreur connexion");
        close(sock);
        exit(EXIT_FAILURE);
    }

    printf("[CLIENT] Connecté au serveur %s:%d\n", ip_dest, port_dest);
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
        printf("[CLIENT] ENVOI | Dest=%d | Message='%s'\n", destinataire, msg);
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

    printf("[CLIENT] RECU | Dest=%d | Message='%s'\n", p->destinataire, p->message);
    
    return rec;
}

/* ========================================================================== */
/*                          AFFICHAGE                                         */
/* ========================================================================== */

void clearScreen()
{
    printf("\033[2J\033[H");
}

/* -------------------------------------------------------------------------- */
void afficherLePendu(int essaisRestants)
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
        fprintf(stderr, "[CLIENT] Erreur fichier pendu.txt\n");
        return;
    }

    int stade = essaisRestants;
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

/* ========================================================================== */
/*                          RÔLE MAÎTRE (choisit le mot)                     */
/* ========================================================================== */

void roleMaitre(int sock, int ID_CLIENT)
{
    char motSecret[LG_MESSAGE];
    char motAffiche[LG_MESSAGE * 2];
    char lettresDevinees[LG_MESSAGE] = {0};
    int essaisRestants = 10;
    int longueurMot = 0;
    int lettresTrouvees = 0;
    
    clearScreen();
    printf("\n|========================================================|\n");
    printf("|              VOUS ÊTES LE MAÎTRE DU JEU               |\n");
    printf("|========================================================|\n\n");
    
    // Demander le mot
    printf("Entrez le mot à faire deviner (lettres uniquement) : ");
    fflush(stdout);
    
    if (fgets(motSecret, sizeof(motSecret), stdin) == NULL)
    {
        printf("[ERREUR] Lecture mot\n");
        return;
    }
    
    motSecret[strcspn(motSecret, "\n")] = 0;
    longueurMot = strlen(motSecret);
    
    // Convertir en majuscules
    for (int i = 0; i < longueurMot; i++)
    {
        motSecret[i] = toupper(motSecret[i]);
    }
    
    printf("\n[MAÎTRE] Mot choisi : %s (%d lettres)\n", motSecret, longueurMot);
    printf("[MAÎTRE] En attente de l'adversaire...\n\n");
    
    // Envoyer "PRET" pour signaler qu'on a choisi le mot
    envoyerPacket(sock, ID_CLIENT, "PRET");
    
    // Construire le mot masqué initial
    memset(motAffiche, 0, sizeof(motAffiche));
    for (int i = 0; i < longueurMot; i++)
    {
        strcat(motAffiche, "_ ");
    }
    
    // Envoyer le mot masqué initial à l'adversaire
    char msg[LG_MESSAGE];
    snprintf(msg, LG_MESSAGE, "MOT=%s|ESSAIS=%d", motAffiche, essaisRestants);
    envoyerPacket(sock, ID_CLIENT, msg);
    
    // Boucle de jeu
    while (lettresTrouvees < longueurMot && essaisRestants > 0)
    {
        clearScreen();
        printf("|========================================================|\n");
        printf("|              JEU DU PENDU - MAÎTRE                     |\n");
        printf("|========================================================|\n\n");
        printf("Mot secret : %s\n", motSecret);
        printf("Mot affiché : %s\n", motAffiche);
        printf("Lettres devinées : %s\n", lettresDevinees);
        printf("Essais restants adversaire : %d ♥\n\n", essaisRestants);
        
        afficherLePendu(essaisRestants);
        
        printf("\n[MAÎTRE] En attente de la lettre de l'adversaire...\n");
        
        // Recevoir la lettre
        Packet p;
        int ret = recevoirPacket(sock, &p);
        if (ret <= 0)
        {
            printf("[ERREUR] Déconnexion adversaire\n");
            return;
        }
        
        // Vérifier si c'est une demande de rejeu
        if (strncmp(p.message, "REJEU", 5) == 0)
        {
            printf("\n[MAÎTRE] L'adversaire demande à rejouer avec rôles inversés\n");
            printf("Acceptez-vous ? (oui/non) : ");
            fflush(stdout);
            
            char reponse[10];
            if (fgets(reponse, sizeof(reponse), stdin) != NULL)
            {
                reponse[strcspn(reponse, "\n")] = 0;
                
                if (strcmp(reponse, "oui") == 0)
                {
                    envoyerPacket(sock, ID_CLIENT, "REJEU=OUI");
                    return;  // Retour pour changer de rôle
                }
                else
                {
                    envoyerPacket(sock, ID_CLIENT, "REJEU=NON");
                    printf("Partie terminée. Au revoir !\n");
                    return;
                }
            }
        }
        
        char lettre = toupper(p.message[0]);
        printf("\n[MAÎTRE] Lettre reçue : '%c'\n", lettre);
        
        // Vérifier si déjà jouée
        if (strchr(lettresDevinees, lettre))
        {
            printf("[MAÎTRE] → Lettre déjà jouée\n");
            snprintf(msg, LG_MESSAGE, "DEJA_JOUEE|MOT=%s|ESSAIS=%d", 
                     motAffiche, essaisRestants);
            envoyerPacket(sock, ID_CLIENT, msg);
            sleep(2);
            continue;
        }
        
        // Ajouter aux lettres devinées
        strncat(lettresDevinees, &lettre, 1);
        
        // Vérifier si bonne lettre
        int bonneLettre = 0;
        for (int i = 0; i < longueurMot; i++)
        {
            if (motSecret[i] == lettre)
            {
                bonneLettre = 1;
                lettresTrouvees++;
            }
        }
        
        if (bonneLettre)
        {
            printf("[MAÎTRE] → Bonne lettre !\n");
            
            // Mettre à jour motAffiche
            memset(motAffiche, 0, sizeof(motAffiche));
            for (int i = 0; i < longueurMot; i++)
            {
                if (strchr(lettresDevinees, motSecret[i]))
                {
                    strncat(motAffiche, &motSecret[i], 1);
                    strcat(motAffiche, " ");
                }
                else
                {
                    strcat(motAffiche, "_ ");
                }
            }
            
            snprintf(msg, LG_MESSAGE, "BONNE|MOT=%s|ESSAIS=%d", 
                     motAffiche, essaisRestants);
            envoyerPacket(sock, ID_CLIENT, msg);
        }
        else
        {
            printf("[MAÎTRE] → Mauvaise lettre\n");
            essaisRestants--;
            
            snprintf(msg, LG_MESSAGE, "MAUVAISE|MOT=%s|ESSAIS=%d", 
                     motAffiche, essaisRestants);
            envoyerPacket(sock, ID_CLIENT, msg);
        }
        
        sleep(2);
    }
    
    // Fin de partie
    clearScreen();
    if (lettresTrouvees >= longueurMot)
    {
        printf("\n|================================|\n");
        printf("|          DÉFAITE               |\n");
        printf("|================================|\n");
        printf("\nL'adversaire a trouvé le mot : %s\n\n", motSecret);
        
        snprintf(msg, LG_MESSAGE, "FIN|VICTOIRE|MOT=%s", motSecret);
        envoyerPacket(sock, ID_CLIENT, msg);
    }
    else
    {
        printf("\n|================================|\n");
        printf("|          VICTOIRE !            |\n");
        printf("|================================|\n");
        printf("\nL'adversaire n'a pas trouvé le mot : %s\n\n", motSecret);
        afficherLePendu(0);
        
        snprintf(msg, LG_MESSAGE, "FIN|DEFAITE|MOT=%s", motSecret);
        envoyerPacket(sock, ID_CLIENT, msg);
    }
    
    // Proposition de rejeu
    printf("\nRejouer avec rôles inversés ? (oui/non) : ");
    fflush(stdout);
    
    char reponse[10];
    if (fgets(reponse, sizeof(reponse), stdin) != NULL)
    {
        reponse[strcspn(reponse, "\n")] = 0;
        
        if (strcmp(reponse, "oui") == 0)
        {
            envoyerPacket(sock, ID_CLIENT, "REJEU=OUI");
            
            // Attendre la réponse de l'adversaire
            Packet p;
            if (recevoirPacket(sock, &p) > 0)
            {
                if (strncmp(p.message, "REJEU=OUI", 9) == 0)
                {
                    printf("\n[INFO] Changement de rôle...\n");
                    sleep(1);
                    return;  // Retour pour changer de rôle
                }
                else
                {
                    printf("\nL'adversaire a refusé. Partie terminée.\n");
                }
            }
        }
        else
        {
            envoyerPacket(sock, ID_CLIENT, "REJEU=NON");
            printf("Partie terminée. Au revoir !\n");
        }
    }
}

/* ========================================================================== */
/*                          RÔLE DEVINETTE (devine le mot)                   */
/* ========================================================================== */

void roleDevinette(int sock, int ID_CLIENT)
{
    char motAffiche[LG_MESSAGE];
    char lettresJouees[LG_MESSAGE] = {0};
    int essaisRestants = 10;
    int partieEnCours = 1;
    
    clearScreen();
    printf("\n|========================================================|\n");
    printf("|            VOUS DEVEZ DEVINER LE MOT                  |\n");
    printf("|========================================================|\n\n");
    printf("[DEVINETTE] En attente du mot...\n");
    
    // Recevoir le mot masqué initial
    Packet p;
    int ret = recevoirPacket(sock, &p);
    if (ret <= 0)
    {
        printf("[ERREUR] Déconnexion\n");
        return;
    }
    
    // Parser MOT=... |ESSAIS=...
    if (sscanf(p.message, "MOT=%[^|]|ESSAIS=%d", motAffiche, &essaisRestants) == 2)
    {
        printf("[DEVINETTE] Partie commencée !\n");
    }
    
    // Boucle de jeu
    while (partieEnCours)
    {
        clearScreen();
        printf("|========================================================|\n");
        printf("|              JEU DU PENDU - DEVINETTE                 |\n");
        printf("|========================================================|\n\n");
        printf("Mot : %s\n", motAffiche);
        printf("Essais restants : %d ♥\n", essaisRestants);
        printf("Lettres jouées : %s\n\n", lettresJouees);
        
        afficherLePendu(essaisRestants);
        
        printf("\nVotre lettre : ");
        fflush(stdout);
        
        char buffer[10];
        if (fgets(buffer, sizeof(buffer), stdin) == NULL)
        {
            printf("[ERREUR] Lecture\n");
            return;
        }
        
        buffer[strcspn(buffer, "\n")] = 0;
        
        if (strlen(buffer) == 0)
        {
            printf("Entrez une lettre !\n");
            sleep(1);
            continue;
        }
        
        char lettre = toupper(buffer[0]);
        
        // Envoyer la lettre
        char msg[10];
        snprintf(msg, sizeof(msg), "%c", lettre);
        envoyerPacket(sock, ID_CLIENT, msg);
        
        // Ajouter aux lettres jouées
        if (!strchr(lettresJouees, lettre))
        {
            strncat(lettresJouees, &lettre, 1);
            strcat(lettresJouees, " ");
        }
        
        // Recevoir le résultat
        ret = recevoirPacket(sock, &p);
        if (ret <= 0)
        {
            printf("[ERREUR] Déconnexion\n");
            return;
        }
        
        // Traiter la réponse
        if (strncmp(p.message, "FIN", 3) == 0)
        {
            // Fin de partie
            clearScreen();
            
            if (strstr(p.message, "VICTOIRE"))
            {
                printf("\n|================================|\n");
                printf("|          VICTOIRE !            |\n");
                printf("|================================|\n");
            }
            else
            {
                printf("\n|================================|\n");
                printf("|          DÉFAITE               |\n");
                printf("|================================|\n");
                afficherLePendu(0);
            }
            
            // Extraire le mot
            char *motPtr = strstr(p.message, "MOT=");
            if (motPtr)
            {
                printf("\nLe mot était : %s\n\n", motPtr + 4);
            }
            
            partieEnCours = 0;
            
            // Proposition de rejeu
            printf("\nRejouer avec rôles inversés ? (oui/non) : ");
            fflush(stdout);
            
            char reponse[10];
            if (fgets(reponse, sizeof(reponse), stdin) != NULL)
            {
                reponse[strcspn(reponse, "\n")] = 0;
                
                if (strcmp(reponse, "oui") == 0)
                {
                    envoyerPacket(sock, ID_CLIENT, "REJEU=OUI");
                    
                    // Attendre réponse adversaire
                    if (recevoirPacket(sock, &p) > 0)
                    {
                        if (strncmp(p.message, "REJEU=OUI", 9) == 0)
                        {
                            printf("\n[INFO] Changement de rôle...\n");
                            sleep(1);
                            return;
                        }
                        else
                        {
                            printf("\nL'adversaire a refusé. Partie terminée.\n");
                        }
                    }
                }
                else
                {
                    envoyerPacket(sock, ID_CLIENT, "REJEU=NON");
                    printf("Partie terminée. Au revoir !\n");
                }
            }
        }
        else if (strncmp(p.message, "BONNE", 5) == 0)
        {
            printf("\n✓ Bonne lettre !\n");
            sscanf(p.message, "BONNE|MOT=%[^|]|ESSAIS=%d", motAffiche, &essaisRestants);
        }
        else if (strncmp(p.message, "MAUVAISE", 8) == 0)
        {
            printf("\n✗ Mauvaise lettre\n");
            sscanf(p.message, "MAUVAISE|MOT=%[^|]|ESSAIS=%d", motAffiche, &essaisRestants);
        }
        else if (strncmp(p.message, "DEJA_JOUEE", 10) == 0)
        {
            printf("\n⚠ Lettre déjà jouée\n");
        }
        
        sleep(2);
    }
}

/* ========================================================================== */
/*                          BOUCLE PRINCIPALE                                 */
/* ========================================================================== */

void boucleClient(int sock, int *ID_CLIENT, int *role)
{
    Packet p;
    
    // Recevoir l'ID et le rôle initial
    int ret = recevoirPacket(sock, &p);
    if (ret <= 0)
    {
        printf("[ERREUR] Réception ID\n");
        return;
    }
    
    *ID_CLIENT = p.destinataire;
    
    // Parser le rôle
    if (strstr(p.message, "ROLE=MAITRE"))
    {
        *role = 1;
        printf("\n[INFO] Vous êtes le joueur #%d (MAÎTRE)\n", *ID_CLIENT);
        printf("[INFO] %s\n", p.message);
    }
    else if (strstr(p.message, "ROLE=DEVINETTE"))
    {
        *role = 2;
        printf("\n[INFO] Vous êtes le joueur #%d (DEVINETTE)\n", *ID_CLIENT);
        printf("[INFO] %s\n", p.message);
    }
    
    // Attendre que les deux joueurs soient connectés
    if (*role == 1)
    {
        printf("\n[INFO] En attente du joueur 2...\n");
        ret = recevoirPacket(sock, &p);
        if (ret <= 0) return;
        
        if (strstr(p.message, "JOUEUR2_CONNECTE"))
        {
            printf("[INFO] Les deux joueurs sont connectés !\n");
        }
    }
    
    // Boucle principale avec changement de rôles
    int continuer = 1;
    while (continuer)
    {
        printf("\n> Tapez 'start' pour commencer ou 'exit' pour quitter : ");
        fflush(stdout);
        
        char buffer[256];
        if (fgets(buffer, sizeof(buffer), stdin) == NULL)
        {
            break;
        }
        
        buffer[strcspn(buffer, "\n")] = 0;
        
        if (strcmp(buffer, "exit") == 0)
        {
            envoyerPacket(sock, *ID_CLIENT, "exit");
            break;
        }
        else if (strcmp(buffer, "start") == 0)
        {
            if (*role == 1)
            {
                roleMaitre(sock, *ID_CLIENT);
                *role = 2;  // Inverser pour le prochain tour
            }
            else
            {
                roleDevinette(sock, *ID_CLIENT);
                *role = 1;  // Inverser pour le prochain tour
            }
        }
    }
}

/* ========================================================================== */
/*                                  MAIN                                      */
/* ========================================================================== */

int main(int argc, char *argv[])
{
    int ID_CLIENT = 0;
    int role = 0;
    
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
    boucleClient(sock, &ID_CLIENT, &role);

    close(sock);
    printf("[CLIENT] Déconnecté.\n");
    return 0;
}