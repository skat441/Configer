#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include "../network_ops.h"

int get_cisco_config(const char *host, const char *user, const char *pass) {
    FILE *fp;
    char script_path[] = "/tmp/telnet_XXXXXX";
    char command[512];
    int result;
    
    // Создаем временный файл
    int fd = mkstemp(script_path);
    if (fd == -1) {
        perror("mkstemp");
        return -1;
    }
    
    // Пишем expect скрипт
    fp = fdopen(fd, "w");
    if (!fp) {
        perror("fdopen");
        close(fd);
        return -1;
    }
    
    fprintf(fp, "#!/usr/bin/expect -f\n");
    fprintf(fp, "set timeout 30\n");
    fprintf(fp, "log_user 1\n\n");
    fprintf(fp, "spawn telnet %s\n", host);
    fprintf(fp, "\n");
    fprintf(fp, "expect \"Username:\"\n");
    fprintf(fp, "send \"%s\\r\"\n", user);
    fprintf(fp, "\n");
    fprintf(fp, "expect \"Password:\"\n");
    fprintf(fp, "send \"%s\\r\"\n", pass);
    fprintf(fp, "\n");
    fprintf(fp, "expect \"#\"\n");
    fprintf(fp, "send \"terminal length 0\\r\"\n");
    fprintf(fp, "\n");
    fprintf(fp, "expect \"#\"\n");
    fprintf(fp, "send \"show running-config\\r\"\n");
    fprintf(fp, "\n");
    fprintf(fp, "send \"exit\\r\"\n");
    fprintf(fp, "expect eof\n");
    
    fclose(fp);
    
    // Делаем скрипт исполняемым
    chmod(script_path, 0755);
    
    // Запускаем expect скрипт и сохраняем вывод
    snprintf(command, sizeof(command), "%s > cisco.cfg", script_path);
    result = system(command);
    
    // Удаляем временный файл
    unlink(script_path);
    
    // Извлекаем только конфигурацию
    system("sed -n '/Building configuration/,/end/p' cisco.cfg | sed '1d;$d' > cisco_clean.cfg");
    system("mv cisco_clean.cfg cisco.cfg");
    
    return result == 0 ? 0 : -1;
}