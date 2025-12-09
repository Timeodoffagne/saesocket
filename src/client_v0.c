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
    if (sock < 0) {
        perror("Erreur en création de la socket");
        exit(EXIT_FAILURE);
    }
    printf("Socket créée (%d)\n", sock);

    struct sockaddr_in serv;
    memset(&serv, 0, sizeof(serv));

    serv.sin_family = AF_INET;
    serv.sin_port = htons(port_dest);

    if (inet_aton(ip_dest, &serv.sin_addr) == 0) {
        fprintf(stderr, "Adresse IP invalide !\n");
        exit(EXIT_FAILURE);
    }

    if (connect(sock, (struct sockaddr *)&serv, sizeof(serv)) == -1) {
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
    if (nb <= 0) {
        perror("Erreur en écriture (send)");
        close(sock);
        exit(EXIT_FAILURE);
    }
    printf("Message envoyé : '%s' (%d octets)\n", message, nb);
}


// =====================================================
//  FONCTION : recevoir une réponse du serveur
// =====================================================
void recevoirMessage(int sock)
{
    char buffer[256];
    memset(buffer, 0, sizeof(buffer));

    // ⚠️ Bloque ici en attendant la réponse du serveur
    int nb = recv(sock, buffer, sizeof(buffer) - 1, 0);

    if (nb < 0) {
        perror("Erreur en lecture (recv)");
        close(sock);
        exit(EXIT_FAILURE);
    } else if (nb == 0) {
        printf("Serveur déconnecté.\n");
        return;
    }

    printf("Réponse du serveur : %s\n", buffer);
}



// =====================================================
//  PROGRAMME PRINCIPAL
// =====================================================
int main(int argc, char *argv[])
{
    if (argc < 3) {
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

    // Message à envoyer
    const char *msg = "start x";

    // Envoi
    envoyerMessage(sock, msg);

    // ➜ Attendre la réponse du serveur
    recevoirMessage(sock);

    close(sock);
    return 0;
}
