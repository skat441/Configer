#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include "../network_ops.h"

int get_cisco_csr_config(const char *host, const char *user, const char *pass) {
    FILE *fp;
    char script_path[] = "/tmp/ssh_XXXXXX";
    char command[512];
    int result;
    
    // Создаем временный файл
    int fd = mkstemp(script_path);
    if (fd == -1) {
        perror("mkstemp");
        return -1;
    }
    
    // Пишем expect скрипт для SSH
    fp = fdopen(fd, "w");
    if (!fp) {
        perror("fdopen");
        close(fd);
        return -1;
    }
    
    fprintf(fp, "#!/usr/bin/expect -f\n");
    fprintf(fp, "set timeout 10\n");  // Увеличиваем таймаут
    fprintf(fp, "log_user 1\n\n");
    
    // Отключаем вывод приглашений и служебной информации
    fprintf(fp, "set stty_init -echo\n\n");
    
    fprintf(fp, "spawn ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null %s@%s\n", user, host);
    fprintf(fp, "\n");
    
    // Ожидаем запрос пароля
    fprintf(fp, "expect {\n");
    fprintf(fp, "    \"password:\" {\n");
    fprintf(fp, "        send \"%s\\r\"\n", pass);
    fprintf(fp, "    }\n");
    fprintf(fp, "    \"Password:\" {\n");
    fprintf(fp, "        send \"%s\\r\"\n", pass);
    fprintf(fp, "    }\n");
    fprintf(fp, "    \"yes/no\" {\n");
    fprintf(fp, "        send \"yes\\r\"\n");
    fprintf(fp, "        exp_continue\n");
    fprintf(fp, "    }\n");
    fprintf(fp, "    timeout {\n");
    fprintf(fp, "        puts \"Connection timeout\"\n");
    fprintf(fp, "        exit 1\n");
    fprintf(fp, "    }\n");
    fprintf(fp, "}\n");
    fprintf(fp, "\n");
    
    // Ожидаем приглашение
    fprintf(fp, "expect {\n");
    fprintf(fp, "    \"#\" {\n");
    fprintf(fp, "        send \"terminal length 0\\r\"\n");
    fprintf(fp, "    }\n");
    fprintf(fp, "    \">\" {\n");
    fprintf(fp, "        send \"enable\\r\"\n");
    fprintf(fp, "        expect \"Password:\"\n");
    fprintf(fp, "        send \"%s\\r\"\n", pass);
    fprintf(fp, "        expect \"#\"\n");
    fprintf(fp, "        send \"terminal length 0\\r\"\n");
    fprintf(fp, "    }\n");
    fprintf(fp, "}\n");
    fprintf(fp, "\n");
    
    // Ожидаем приглашение после terminal length
    fprintf(fp, "expect \"#\"\n");
    
    // Отправляем команду и ЖДЕМ ОКОНЧАНИЯ ВЫВОДА
    fprintf(fp, "send \"show running-config\\r\"\n");
    fprintf(fp, "\n");
    
    // Ждем приглашение, но с таймаутом и читаем ВЕСЬ вывод
    fprintf(fp, "expect {\n");
    fprintf(fp, "    \"#\" {\n");
    fprintf(fp, "        # Команда завершена, приглашение получено\n");
    fprintf(fp, "    }\n");
    fprintf(fp, "    timeout {\n");
    fprintf(fp, "        puts \"Timeout waiting for command completion\"\n");
    fprintf(fp, "    }\n");
    fprintf(fp, "}\n");
    fprintf(fp, "\n");
    
    // Даем небольшую паузу для завершения буферизации
    fprintf(fp, "sleep 1\n");
    
    // Отправляем exit
    fprintf(fp, "send \"exit\\r\"\n");
    fprintf(fp, "expect eof\n");
    
    fclose(fp);
    
    // Делаем скрипт исполняемым
    chmod(script_path, 0755);
    
    // Запускаем expect скрипт
    snprintf(command, sizeof(command), "%s > cisco_raw.cfg 2>&1", script_path);
    result = system(command);
    
    system(
        "awk '/^Building configuration/,/^end$/ {print}' cisco_raw.cfg | "
        "grep -v '^Router#' | "
        "grep -v '^Router>' | "
        "sed '1d;$d' > cisco_clean.cfg && "
        "mv cisco_clean.cfg cisco.cfg"
    );
    
    // Удаляем временные файлы
    unlink(script_path);
    unlink("cisco_raw.cfg");
    
    // Проверка - не пустой ли файл конфигурации
    struct stat st;
    if (stat("cisco.cfg", &st) == 0 && st.st_size > 1000) {
        printf("Configuration saved successfully (%ld bytes)\n", st.st_size);
        return 0;
    } else {
        fprintf(stderr, "Warning: Configuration file is empty or too small\n");
        return -1;
    }
    
    return result == 0 ? 0 : -1;
}