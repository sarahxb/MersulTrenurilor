#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <errno.h>
#include <libxml/parser.h>
#include <libxml/tree.h>

#define PORT 2908
#define MAX_CLIENTS 100
#define QUEUE_SIZE 100
volatile int server_quit = 0;
typedef enum {
    GET_SOSIRI,
    GET_PLECARI,
    GET_MERS,
    UPDATE_DELAY,
    UPDATE_EARLY
} CommandType;

typedef struct {
    CommandType type;
    int client_id; 
    char args[256]; 
} Command;

typedef struct {
    Command queue[QUEUE_SIZE];
    int front, rear;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} CommandQueue;

// datele thread-ului
typedef struct {
    int client_id;
} ClientThreadData;


CommandQueue commandQueue;


void initQueue(CommandQueue *q) {
    q->front = q->rear = 0;
    pthread_mutex_init(&q->mutex, NULL);
    pthread_cond_init(&q->cond, NULL);
}

void enqueue(CommandQueue *q, Command cmd) {
    pthread_mutex_lock(&q->mutex);
    q->queue[q->rear] = cmd;
    q->rear = (q->rear + 1) % QUEUE_SIZE;
    pthread_cond_signal(&q->cond);
    pthread_mutex_unlock(&q->mutex);
}

Command dequeue(CommandQueue *q) {
    pthread_mutex_lock(&q->mutex);
    while (q->front == q->rear) { 
        pthread_cond_wait(&q->cond, &q->mutex);
    }
    Command cmd = q->queue[q->front];
    q->front = (q->front + 1) % QUEUE_SIZE;
    pthread_mutex_unlock(&q->mutex);
    return cmd;
}









void sendTrainSchedule(int client_id) {

    xmlDoc *doc = xmlReadFile("train_schedule.xml", NULL, 0);
    if (doc == NULL) {
        const char *error_response = "Eroare la citirea fișierului XML!";
        write(client_id, error_response, strlen(error_response));
        return;
    }

    
    xmlNode *root = xmlDocGetRootElement(doc);
    xmlNode *cur_node = NULL;

 
    time_t now = time(NULL);
    struct tm *local = localtime(&now);
    char display_date[11]; 
    snprintf(display_date, sizeof(display_date), "%04d-%02d-%02d",
             local->tm_year + 1900, local->tm_mon + 1, local->tm_mday);

  
    size_t buffer_size = 1024;
    char *response = (char *)malloc(buffer_size);
    if (!response) {
        const char *error_response = "Eroare de memorie!";
        write(client_id, error_response, strlen(error_response));
        xmlFreeDoc(doc);
        return;
    }

    
    size_t response_length = snprintf(response, buffer_size, "Orarul trenurilor pentru data %s:\n", display_date);

   
    for (cur_node = root->children; cur_node; cur_node = cur_node->next) {
        if (cur_node->type == XML_ELEMENT_NODE &&
            xmlStrcmp(cur_node->name, (const xmlChar *)"train") == 0) {
            xmlChar *id = xmlGetProp(cur_node, (const xmlChar *)"id");
            xmlChar *departure = NULL;
            xmlChar *arrival = NULL;
            xmlChar *route = NULL;

            xmlNode *child = NULL;
            for (child = cur_node->children; child; child = child->next) {
                if (child->type == XML_ELEMENT_NODE) {
                    if (xmlStrcmp(child->name, (const xmlChar *)"departure") == 0) {
                        departure = xmlNodeGetContent(child);
                    } else if (xmlStrcmp(child->name, (const xmlChar *)"arrival") == 0) {
                        arrival = xmlNodeGetContent(child);
                    } else if (xmlStrcmp(child->name, (const xmlChar *)"ruta") == 0) {
                        route = xmlNodeGetContent(child);
                    }
                }
            }

            
            if (id && departure && arrival && route) {
                size_t needed = snprintf(NULL, 0, "Tren ID: %s, Ruta: %s, Plecare: %s, Sosire: %s\n",
                                         (char *)id, (char *)route, (char *)departure, (char *)arrival) + 1;
                if (response_length + needed > buffer_size) {
                    buffer_size *= 2;
                    char *new_response = (char *)realloc(response, buffer_size);
                    if (!new_response) {
                        const char *error_response = "Eroare de memorie!";
                        write(client_id, error_response, strlen(error_response));
   
                        free(response);
                        xmlFreeDoc(doc);
                        return;
                    }
                    response = new_response;
                }
                response_length += snprintf(response + response_length, buffer_size - response_length,
                                            "Tren ID: %s, Ruta: %s, Plecare: %s, Sosire: %s\n",
                                            (char *)id, (char *)route, (char *)departure, (char *)arrival);
            }

            
        }
    }

    write(client_id, response, response_length);

  
    free(response);
    xmlFreeDoc(doc);
}






int parseTime(const char *time_str) {
    int hours, minutes;
    if (sscanf(time_str, "%d:%d", &hours, &minutes) == 2) {
        return hours * 60 + minutes; 
    }
    return -1; 
}



void sendFilteredTrainDetails(int client_id, const char *command, const char *time_arg) {

    xmlDoc *doc = xmlReadFile("train_schedule.xml", NULL, 0);
    if (doc == NULL) {
        const char *error_response = "Eroare la citirea fișierului XML!";
        write(client_id, error_response, strlen(error_response));
        return;
    }


    xmlNode *root = xmlDocGetRootElement(doc);
    xmlNode *cur_node = NULL;


    int current_time;
    if (time_arg && strcmp(time_arg, "25") != 0) { 
        current_time = parseTime(time_arg);
        if (current_time == -1) {
            const char *error_response = "Format incorect pentru timp! Folosiți HH:MM.\n";
            write(client_id, error_response, strlen(error_response));
            xmlFreeDoc(doc);
            return;
        }
    } else {
        time_t now = time(NULL);
        struct tm *local = localtime(&now);
        current_time = local->tm_hour * 60 + local->tm_min;
    }

    int next_hour_time = (current_time + 60) % (24 * 60); 

    
    size_t response_size = 1024;
    char *response = (char *)malloc(response_size);
    if (!response) {
        perror("Eroare la alocarea memoriei pentru răspuns.");
        xmlFreeDoc(doc);
        return;
    }
    response[0] = '\0';
    int train_found = 0;

    if (strcmp(command, "GET_PLECARI") == 0) {
        strcat(response, "Plecari trenuri în următoarea oră:\n");
        for (cur_node = root->children; cur_node; cur_node = cur_node->next) {
            if (cur_node->type == XML_ELEMENT_NODE &&
                xmlStrcmp(cur_node->name, (const xmlChar *)"train") == 0) {
                xmlChar *id = xmlGetProp(cur_node, (const xmlChar *)"id");
                xmlChar *departure = NULL;
                xmlChar *route = NULL;

                xmlNode *child = NULL;
                for (child = cur_node->children; child; child = child->next) {
                    if (child->type == XML_ELEMENT_NODE) {
                        if (xmlStrcmp(child->name, (const xmlChar *)"departure") == 0) {
                            departure = xmlNodeGetContent(child);
                        } else if (xmlStrcmp(child->name, (const xmlChar *)"ruta") == 0) {
                            route = xmlNodeGetContent(child);
                        }
                    }
                }

                if (departure) {
                    int arr_time = parseTime((char *)departure);
                    if (arr_time >= current_time && arr_time < next_hour_time) {
                        size_t needed_size = strlen(response) + 100 + strlen((char *)id) + strlen((char *)route) + strlen((char *)departure);
                        if (needed_size >= response_size) {
                            response_size *= 2;
                            char *new_response = realloc(response, response_size);
                            if (!new_response) {
                                perror("Eroare la realocarea memoriei.");
                                free(response);
                                xmlFreeDoc(doc);
                                return;
                            }
                            response = new_response;
                        }
                        snprintf(response + strlen(response), response_size - strlen(response),
                                 "Tren ID: %s, Ruta: %s, Plecari: %s\n",
                                 (char *)id, (char *)route, (char *)departure);
                        train_found = 1;
                    }
                }
            }
        }
    } else if (strcmp(command, "GET_SOSIRI") == 0) {
        strcat(response, "Sosiri trenuri în următoarea oră:\n");
        for (cur_node = root->children; cur_node; cur_node = cur_node->next) {
            if (cur_node->type == XML_ELEMENT_NODE &&
                xmlStrcmp(cur_node->name, (const xmlChar *)"train") == 0) {
                xmlChar *id = xmlGetProp(cur_node, (const xmlChar *)"id");
                xmlChar *arrival = NULL;
                xmlChar *route = NULL;

                xmlNode *child = NULL;
                for (child = cur_node->children; child; child = child->next) {
                    if (child->type == XML_ELEMENT_NODE) {
                        if (xmlStrcmp(child->name, (const xmlChar *)"arrival") == 0) {
                            arrival = xmlNodeGetContent(child);
                        } else if (xmlStrcmp(child->name, (const xmlChar *)"ruta") == 0) {
                            route = xmlNodeGetContent(child);
                        }
                    }
                }

                if (arrival) {
                    int arr_time = parseTime((char *)arrival);
                    if (arr_time >= current_time && arr_time < next_hour_time) {
                        size_t needed_size = strlen(response) + 100 + strlen((char *)id) + strlen((char *)route) + strlen((char *)arrival);
                        if (needed_size >= response_size) {
                            response_size *= 2;
                            char *new_response = realloc(response, response_size);
                            if (!new_response) {
                                perror("Eroare la realocarea memoriei.");
                                free(response);
                                xmlFreeDoc(doc);
                                return;
                            }
                            response = new_response;
                        }
                        snprintf(response + strlen(response), response_size - strlen(response),
                                 "Tren ID: %s, Ruta: %s, Sosire: %s\n",
                                 (char *)id, (char *)route, (char *)arrival);
                        train_found = 1;
                    }
                }
            }
        }
    } else {
        const char *error_response = "Comandă necunoscută!";
        write(client_id, error_response, strlen(error_response));
        free(response);
        xmlFreeDoc(doc);
        return;
    }

    if (!train_found) {
        snprintf(response, response_size, "Nu exista trenuri in acest interval (%02d:%02d - %02d:%02d)\n",
                 current_time / 60, current_time % 60, next_hour_time / 60, next_hour_time % 60);
    }

  
    write(client_id, response, strlen(response));
    free(response);
    xmlFreeDoc(doc);
}




void updateTrainTime(int client_id, const char *args, int is_delay) {
    char train_id[10], time_adjustment[10], type[10];
    if (sscanf(args, "%s %s %s", train_id, time_adjustment, type) == 3) {

        xmlDoc *doc = xmlReadFile("train_schedule.xml", NULL, 0);
        if (doc == NULL) {
            const char *error_response = "Eroare la citirea fișierului XML!";
            write(client_id, error_response, strlen(error_response));
            return;
        }

        xmlNode *root = xmlDocGetRootElement(doc);
        xmlNode *cur_node = NULL;
        for (cur_node = root->children; cur_node; cur_node = cur_node->next) {
            if (cur_node->type == XML_ELEMENT_NODE &&
                xmlStrcmp(cur_node->name, (const xmlChar *)"train") == 0) {
                xmlChar *id = xmlGetProp(cur_node, (const xmlChar *)"id");
                if (id && strcmp((char *)id, train_id) == 0) {
                    xmlNode *child = NULL;
                    for (child = cur_node->children; child; child = child->next) {
                        if (child->type == XML_ELEMENT_NODE && strcmp((char *)child->name, type) == 0) {
                            char *current_time = (char *)xmlNodeGetContent(child);
                            int adjustment = atoi(time_adjustment);

                          
                            int hours, minutes;
                            sscanf(current_time, "%d:%d", &hours, &minutes);

                          
                            int total_minutes = hours * 60 + minutes;
                            total_minutes += is_delay ? adjustment : -adjustment;

                           
                            if (total_minutes < 0) total_minutes += 24 * 60;
                            total_minutes %= 24 * 60;

                          
                            hours = total_minutes / 60;
                            minutes = total_minutes % 60;

                            
                            char updated_time[6]; 
                            snprintf(updated_time, sizeof(updated_time), "%02d:%02d", hours, minutes);
                            xmlNodeSetContent(child, (const xmlChar *)updated_time);

                            printf("Actualizat %s pentru trenul %s cu %s de %d minute.\n",
                                   type, train_id, is_delay ? "întârziere" : "ajustare mai devreme", adjustment);

                            char response[256];
                            snprintf(response, sizeof(response),
                                     "Modificare actualizată cu succes! Noua oră pentru %s este %s.\n",
                                     type, updated_time);
                            write(client_id, response, strlen(response));

                            break;
                        }
                    }
                    break;
                }
            }
        }

       
        xmlSaveFormatFileEnc("train_schedule.xml", doc, "UTF-8", 1);
        xmlFreeDoc(doc);
    } else {
        const char *error_response = "Comandă incorectă! Format: <train_id> <time_adjustment> <departure|arrival>\n";
        write(client_id, error_response, strlen(error_response));
    }
}



void handleGetSosiri(int client_id, const char *args) {
    

sendFilteredTrainDetails(client_id, "GET_SOSIRI", args);
}

void handleGetPlecari(int client_id, const char *args) {
  

sendFilteredTrainDetails(client_id, "GET_PLECARI", args);
}

void handleGetMers(int client_id, const char *args) {
  
sendTrainSchedule(client_id);
}


void handleUpdateDelay(int client_id, const char *args) {
   
updateTrainTime(client_id, args, 1);
}


void handleUpdateEstimareSosire(int client_id, const char *args) {
  
updateTrainTime(client_id, args, 0);
}



void *commandProcessor(void *arg) {
    while (1) {
        Command cmd = dequeue(&commandQueue);
        switch (cmd.type) {
            case GET_SOSIRI:
                handleGetSosiri(cmd.client_id, cmd.args);
                break;
            case GET_PLECARI:
                handleGetPlecari(cmd.client_id, cmd.args);
                break;
            case GET_MERS:
                handleGetMers(cmd.client_id, cmd.args);
                break;
            case UPDATE_DELAY:
                handleUpdateDelay(cmd.client_id, cmd.args);
                break;
            case UPDATE_EARLY:
                handleUpdateEstimareSosire(cmd.client_id, cmd.args);
                break;
        }
    }
    return NULL;
}

// thread-ul pentru fiecare client
void *clientHandler(void *arg) {
    ClientThreadData *data = (ClientThreadData *)arg;
    int client_id = data->client_id;
    free(data);

    char buffer[256];
    while (1) {
        bzero(buffer, sizeof(buffer));
        if (read(client_id, buffer, sizeof(buffer)) <= 0) {
            printf("Client %d deconectat.\n", client_id);
            close(client_id);
            break;
        }

        
        Command cmd;


        char *command_name = strtok(buffer, " "); 

        
        if (strcmp(command_name, "GET_SOSIRI") == 0) {
            char *time = strtok(NULL, " "); 
            if (time) {
                cmd = (Command){GET_SOSIRI, client_id, ""};
                strcpy(cmd.args, time);  
                enqueue(&commandQueue, cmd);
            }
        } else if (strcmp(command_name, "GET_PLECARI") == 0) {
            char *time = strtok(NULL, " "); 
            if (time) {
                cmd = (Command){GET_PLECARI, client_id, ""};
                strcpy(cmd.args, time);  
                enqueue(&commandQueue, cmd);
            }
        } else if (strcmp(command_name, "GET_MERS") == 0) {
            char *date = strtok(NULL, " "); 
            if (date) {
                cmd = (Command){GET_MERS, client_id, ""};
                strcpy(cmd.args, date);  
                enqueue(&commandQueue, cmd);
            }
        } else if (strcmp(command_name, "UPDATE_DELAY") == 0) {
            char *train_id = strtok(NULL, " ");  
            char *delay = strtok(NULL, " ");  
            
            char *type = strtok(NULL, " "); 
            if (train_id && delay && type) {
                cmd = (Command){UPDATE_DELAY, client_id, ""};
                
                         snprintf(cmd.args, sizeof(cmd.args), "%s %s %s", train_id, delay,type);  
                enqueue(&commandQueue, cmd);
            }
        } else if (strcmp(command_name, "UPDATE_EARLY") == 0) {
            char *train_id = strtok(NULL, " ");  
            char *early_arrival = strtok(NULL, " ");  
            char *type = strtok(NULL, " "); 
            if (train_id && early_arrival && type) {
                cmd = (Command){UPDATE_EARLY, client_id, ""};
                snprintf(cmd.args, sizeof(cmd.args), "%s %s %s", train_id, early_arrival,type); 
                enqueue(&commandQueue, cmd);
            }
        }
    }
    return NULL;
}

void loadTrainData() {
    printf("[server]Loaded train data.\n");
}


int main() {
    int server_fd, client_fd;
    struct sockaddr_in server, client;
    socklen_t client_len = sizeof(client);
    pthread_t processor_thread;

    
    initQueue(&commandQueue);

    // crearea thread-ului de procesare a comenzilor
    pthread_create(&processor_thread, NULL, commandProcessor, NULL);

    // configurare server
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("Eroare la socket()");
        exit(EXIT_FAILURE);
    }

    int on = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

    bzero(&server, sizeof(server));
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = INADDR_ANY;
    server.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&server, sizeof(server)) < 0) {
        perror("Eroare la bind()");
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, MAX_CLIENTS) < 0) {
        perror("Eroare la listen()");
        exit(EXIT_FAILURE);
    }

    printf("Server pornit la portul %d...\n", PORT);
    loadTrainData();

    while (1) {
        client_fd = accept(server_fd, (struct sockaddr *)&client, &client_len);
        if (client_fd < 0) {
            perror("Eroare la accept()");
            continue;
        }

        printf("Client conectat, descriptor: %d\n", client_fd);

        // crearea unui thread pentru fiecare client
        ClientThreadData *data = (ClientThreadData *)malloc(sizeof(ClientThreadData));
        data->client_id = client_fd;

        pthread_t client_thread;
        pthread_create(&client_thread, NULL, clientHandler, data);
        pthread_detach(client_thread);
    }

    close(server_fd);
    return 0;
}

