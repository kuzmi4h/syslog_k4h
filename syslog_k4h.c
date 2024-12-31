#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/stat.h>
#include <ctype.h>
#include <regex.h>

#define MAX_LINE 1024

// Структура для хранения информации о хостах
typedef struct {
    char ip[16];
    char name[32];
} Host;

Host hosts[10];
int host_count = 0;

// Функция для чтения файла host.ini
void read_host_ini(const char *filename) {
    FILE *file = fopen(filename, "r");
    if (!file) {
        perror("Error opening host.ini");
        exit(1);
    }

    while (fscanf(file, "%15s %31s", hosts[host_count].ip, hosts[host_count].name) == 2) {
        host_count++;
    }

    fclose(file);
}

// Функция для получения имени хоста по IP
const char* get_host_name(const char *ip) {
    for (int i = 0; i < host_count; i++) {
        if (strcmp(ip, hosts[i].ip) == 0) {
            return hosts[i].name;
        }
    }
    return ip; // Если имя не найдено, вернуть IP
}

// Функция для создания директории, если её нет
void create_directory(const char *path) {
    struct stat st = {0};
    if (stat(path, &st) == -1) {
        mkdir(path, 0700);
    }
}

// Функция для удаления частей сообщения на основе регулярных выражений
void remove_regex_parts(char *message, const char *pattern) {
    regex_t regex;
    regmatch_t matches[1];

    if (regcomp(&regex, pattern, REG_EXTENDED) != 0) {
        perror("Error compiling regex");
        exit(1);
    }

    while (regexec(&regex, message, 1, matches, 0) == 0) {
        int start = matches[0].rm_so;
        int end = matches[0].rm_eo;
        memmove(message + start, message + end, strlen(message + end) + 1);
    }

    regfree(&regex);
}

int main() {
    // Чтение конфигурационного файла
    const char *config_file = "syslog_k4h.conf";
    FILE *file = fopen(config_file, "r");
    if (!file) {
        perror("Error opening syslog_k4h.conf");
        exit(1);
    }

    int port;
    char log_filename[256];
    char host_filename[256];
    char log_path[256];
    char remove_regex[256];

    fscanf(file, "port = %d\n", &port);
    fscanf(file, "log_filename = %255s\n", log_filename);
    fscanf(file, "host_filename = %255s\n", host_filename);
    fscanf(file, "log_path = %255s\n", log_path);
    fscanf(file, "remove_regex = %255[^\n]\n", remove_regex);

    fclose(file);

    // Создание директории для логов, если её нет
    create_directory(log_path);

    // Чтение файла host.ini
    read_host_ini(host_filename);

    // Создание сокета
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("Error creating socket");
        exit(1);
    }

    struct sockaddr_in server_addr, client_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if (bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Error binding socket");
        exit(1);
    }

    char buffer[MAX_LINE];
    socklen_t client_len = sizeof(client_addr);

    while (1) {
        int n = recvfrom(sockfd, buffer, MAX_LINE, 0, (struct sockaddr *)&client_addr, &client_len);
        if (n > 0) {
            buffer[n] = '\0';

            // Удаление частей сообщения на основе регулярных выражений
            remove_regex_parts(buffer, remove_regex);

            // Получение текущего времени
            time_t now;
            time(&now);
            struct tm *timeinfo = localtime(&now);
            char datetime[64];
            strftime(datetime, sizeof(datetime), "%Y-%m-%d %H:%M:%S", timeinfo);

            // Получение имени хоста и IP адреса
            const char *host_name = get_host_name(inet_ntoa(client_addr.sin_addr));
            const char *host_ip = inet_ntoa(client_addr.sin_addr);

            // Формирование строки логов
            char log_line[MAX_LINE + 256];
            snprintf(log_line, sizeof(log_line), "%s Local0 Info %s %s %s\n", datetime, host_ip, host_name, buffer);

            // Создание имени файла логов
            char log_file[512];
            strftime(log_file, sizeof(log_file), log_filename, timeinfo);

            // Формирование полного пути к файлу логов
            char full_log_path[768];
            snprintf(full_log_path, sizeof(full_log_path), "%s/%s", log_path, log_file);

            // Запись логов в файл
            FILE *log_fp = fopen(full_log_path, "a");
            if (log_fp) {
                fprintf(log_fp, "%s", log_line);
                fclose(log_fp);
            } else {
                perror("Error opening log file");
            }
        }
    }

    close(sockfd);
    return 0;
}

