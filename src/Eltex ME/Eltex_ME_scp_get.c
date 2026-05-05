#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include "../network_ops.h"

int eltex_me_scp_get(const char *hostname,
                     const char *user,
                     const char *ip,
                     const char *pass,
                     const char *local_user,
                     const char *local_ip,
                     const char *local_pass,
                     const char *config_path) {
    // const char *hostname = "L1-AR2";
    // const char *user = "admin";
    // const char *ip = "192.168.192.106";
    // const char *pass = "password";
    // const char *local_user = "kirill";
    // const char *local_ip = "192.168.192.53";
    // const char *local_pass = "0512";//get in interactive mode from user, we need to save it for current session
    // const char *config_path = "./../../configs/L1-AR2";
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
    
    // Пишем expect скрипт
    fp = fdopen(fd, "w");
    if (!fp) {
        perror("fdopen");
        close(fd);
        return -1;
    }
    
    fprintf(fp, "#!/usr/bin/expect -f\n");
    fprintf(fp, "set timeout 30\n");
    fprintf(fp, "log_user 0\n\n");
    fprintf(fp, "spawn ssh %s@%s\n", user, ip);
    fprintf(fp, "\n");
    fprintf(fp, "expect \"Password:\"\n");
    fprintf(fp, "send \"%s\\r\"\n", pass);
    fprintf(fp, "\n");
    fprintf(fp, "expect \"#\"\n");
    fprintf(fp, "send \"copy fs://running-config scp://%s@%s/ vrf mgmt-intf\\r\"\n", local_user, local_ip);
    fprintf(fp, "\n");
    fprintf(fp, "expect \"password:\"\n");
    fprintf(fp, "send \"%s\\r\"\n", local_pass);
    fprintf(fp, "\n");
    fprintf(fp, "send \"quit\\r\"\n");
    fprintf(fp, "expect eof\n");
    
    fclose(fp);
    
    // Делаем скрипт исполняемым
    chmod(script_path, 0755);
    
    // Запускаем expect скрипт и сохраняем вывод
    snprintf(command, sizeof(command), "%s", script_path);
    result = system(command);

    // Удаляем временный файл
    unlink(script_path);
    
    //Перемещаем конфиг файл в рабочую директорию
    snprintf(command, sizeof(command), "mv ~/%s* %s", hostname, config_path);
    system(command);

    return result == 0 ? 0 : -1;
}