#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include "../network_ops.h"

int put_cisco_config(const char *host, const char *user, const char *pass, const char *config_file) {
    FILE *fp;
    char script_path[] = "/tmp/telnet_put_XXXXXX";
    char command[1024];
    int result;
    char line[1024];
    char error_log[] = "/tmp/cisco_put_error_XXXXXX";
    struct stat st;
    FILE *config_fp;

    // Проверяем файл конфигурации
    config_fp = fopen(config_file, "r");
    if (!config_fp) {
        fprintf(stderr, "Cannot open config file: %s\n", config_file);
        return -1;
    }
    
    // Создаем временный файл для expect скрипта
    int fd = mkstemp(script_path);
    if (fd == -1) {
        perror("mkstemp");
        fclose(config_fp);
        return -1;
    }
    
    // Пишем expect скрипт
    fp = fdopen(fd, "w");
    if (!fp) {
        perror("fdopen");
        close(fd);
        fclose(config_fp);
        return -1;
    }
    
    fprintf(fp, "#!/usr/bin/expect -f\n");
    fprintf(fp, "set timeout 30\n");
    fprintf(fp, "log_user 0\n\n");
    fprintf(fp, "spawn telnet %s\n", host);
    fprintf(fp, "\n");
    fprintf(fp, "expect {\n");
    fprintf(fp, "    \"Connection refused\" {\n");
    fprintf(fp, "        puts \"Error: Connection refused by %s\"\n", host);
    fprintf(fp, "        exit 1\n");
    fprintf(fp, "    }\n");
    fprintf(fp, "    \"No route to host\" {\n");
    fprintf(fp, "        puts \"Error: No route to host %s\"\n", host);
    fprintf(fp, "        exit 1\n");
    fprintf(fp, "    }\n");
    fprintf(fp, "    timeout {\n");
    fprintf(fp, "        puts \"Error: Telnet connection timeout to %s\"\n", host);
    fprintf(fp, "        exit 1\n");
    fprintf(fp, "    }\n");
    fprintf(fp, "    \"Username:\"\n");
    fprintf(fp, "}\n");
    fprintf(fp, "send \"%s\\r\"\n", user);
    fprintf(fp, "\n");
    fprintf(fp, "expect \"Password:\"\n");
    fprintf(fp, "send \"%s\\r\"\n", pass);
    fprintf(fp, "\n");
    fprintf(fp, "expect \"#\"\n");
    fprintf(fp, "send \"configure terminal\\r\"\n");
    fprintf(fp, "\n");
    
    // Читаем конфиг и добавляем каждую строку как отдельную команду
    while (fgets(line, sizeof(line), config_fp)) {
        // Убираем перевод строки
        line[strcspn(line, "\n")] = 0;
        line[strcspn(line, "\r")] = 0;
        
        // Пропускаем пустые строки и комментарии
        if (strlen(line) == 0) continue;
        if (strncmp(line, "!!", 2) == 0) continue;
        if (strcmp(line, "!") == 0) continue;
        
        fprintf(fp, "expect \"#\"\n");
        fprintf(fp, "send \"%s\\r\"\n", line);
        fprintf(fp, "\n");
    }
    fclose(config_fp);
    
    fprintf(fp, "expect \"(config)#\"\n");
    fprintf(fp, "send \"commit replace\\r\"\n");
    fprintf(fp, "\n");
    
    // Обрабатываем подтверждение
    fprintf(fp, "expect {\n");
    fprintf(fp, "    \"Do you wish to proceed?\" {\n");
    fprintf(fp, "        send \"yes\\r\"\n");
    fprintf(fp, "        exp_continue\n");
    fprintf(fp, "    }\n");
    fprintf(fp, "    \"#\" {\n");
    fprintf(fp, "        send \"exit\\r\"\n");
    fprintf(fp, "    }\n");
    fprintf(fp, "    timeout {\n");
    fprintf(fp, "        puts \"Timeout waiting for commit\"\n");
    fprintf(fp, "        exit 1\n");
    fprintf(fp, "    }\n");
    fprintf(fp, "}\n");
    fprintf(fp, "\n");
    
    fprintf(fp, "expect \"#\"\n");
    fprintf(fp, "send \"exit\\r\"\n");
    fprintf(fp, "\n");
    fprintf(fp, "expect eof\n");
    
    fclose(fp);
    
    // Делаем скрипт исполняемым
    chmod(script_path, 0755);

    int error_fd = mkstemp(error_log);
    if (error_fd != -1) {
        close(error_fd);
    }
    
    // Запускаем expect скрипт и сохраняем вывод
    snprintf(command, sizeof(command), "%s > %s 2>&1", script_path, error_log);
    result = system(command);
    
    if (result == -1) {
        fprintf(stderr, "Error: Failed to execute Expect script\n");
        unlink(script_path);
        unlink(error_log);
        return -1;
    }

    if (stat(error_log, &st) == 0 && st.st_size > 0) {
        FILE *log_fp = fopen(error_log, "r");
        if (log_fp) {
            char log_line[256];
            while (fgets(log_line, sizeof(log_line), log_fp)) {
                if (strstr(log_line, "Error:")) {
                    fprintf(stderr, "%s", log_line);
                    result = -1;
                }
            }
            fclose(log_fp);
        }
    }

    // Удаляем временные файлы
    unlink(script_path);
    unlink(error_log);
    
    if (result == 0) {
        fprintf(stderr, "[+] Success! Configuration applied to %s\n", host);
        return 0;
    } else {
        return -1;
    }
}