#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>

extern int errno;
int port;

int main(int argc, char *argv[]) {
    int sd;
    struct sockaddr_in server;
    char input[256]; 

    if (argc != 3) {
        printf("Sintaxa: %s <adresa_server> <port>\n", argv[0]);
        return -1;
    }

    port = atoi(argv[2]);

    if ((sd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("Eroare la socket().\n");
        return errno;
    }

    // Configurare server 
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = inet_addr(argv[1]);
    server.sin_port = htons(port);

    if (connect(sd, (struct sockaddr *)&server, sizeof(struct sockaddr)) == -1) {
        perror("[client]Eroare la connect().\n");
        return errno;
    }

    printf("Conectat la serverul de la adresa %s:%d\n", argv[1], port);

    while (1) {
        printf("Introduceti o comanda (GET_SOSIRI, GET_PLECARI, GET_MERS, UPDATE_DELAY, UPDATE_EARLY, EXIT):\n");
        fflush(stdout);

       
        if (!fgets(input, sizeof(input), stdin)) {
            perror("[client]Eroare la citirea comenzii.\n");
            continue;
        }

        
        input[strcspn(input, "\n")] = '\0';

        if (strcmp(input, "EXIT") == 0) {
            printf("Deconectare...\n");
            break;
        }

        char *command = strtok(input, " "); 

        if (!command) {
            printf("Comanda invalida!\n");
            continue;
        }

        
        size_t buffer_size = 256;
        char *buffer = (char *)malloc(buffer_size);
        if (!buffer) {
            perror("[client]Eroare la alocarea memoriei pentru buffer.\n");
            return errno;
        }

        if (strcmp(command, "GET_SOSIRI") == 0 || strcmp(command, "GET_PLECARI") == 0) {
            char *param = strtok(NULL, " ");
            if (!param) {
                snprintf(buffer, buffer_size, "%s 25", command); 
            } else {
                snprintf(buffer, buffer_size, "%s %s", command, param);
            }
        } else if (strcmp(command, "GET_MERS") == 0) {
            snprintf(buffer, buffer_size, "%s 1", command);
        } else if (strcmp(command, "UPDATE_DELAY") == 0 || strcmp(command, "UPDATE_EARLY") == 0) {
            char *train_id = strtok(NULL, " ");
            char *time = strtok(NULL, " ");
            char *type = strtok(NULL, " ");

            if (train_id && time && type) {
                snprintf(buffer, buffer_size, "%s %s %s %s", command, train_id, time, type);
            } else {
                printf("Format invalid! Folositi: UPDATE_DELAY/UPDATE_EARLY <tren_id> <timp> <tip>\n");
                free(buffer);
                continue;
            }
        } else {
            printf("Comanda invalida!\n");
            free(buffer);
            continue;
        }

        
        buffer[buffer_size - 1] = '\0';

       
        if (write(sd, buffer, strlen(buffer)) <= 0) {
            perror("[client]Eroare la write() spre server.\n");
            free(buffer);
            return errno;
        }

        free(buffer);

        
        size_t response_size = 1024;
        char *response = (char *)malloc(response_size);
        if (!response) {
            perror("[client]Eroare de alocare memorie.\n");
            return errno;
        }

        size_t total_read = 0;
        ssize_t bytes_read;
        int response_complete = 0;


        while (!response_complete && (bytes_read = read(sd, response + total_read, response_size - total_read - 1)) > 0) {
            total_read += bytes_read;

            if (total_read >= response_size - 1) {
                response_size *= 2;
                char *new_response = realloc(response, response_size);
                if (!new_response) {
                    perror("[client]Eroare la realocarea memoriei.\n");
                    free(response);
                    return errno;
                }
                response = new_response;
            }

            if (response[total_read - 1] == '\n') {
                response_complete = 1;
            }
        }

        if (bytes_read < 0) {
            perror("[client]Eroare la read() de la server.\n");
            free(response);
            return errno;
        }

  
        response[total_read] = '\0';

        printf("[client]Raspunsul serverului: %s\n", response);

        free(response);
    }

    close(sd);
    return 0;
}

