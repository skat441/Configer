#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <time.h>
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
    char error_log[] = "/tmp/cisco_put_error_XXXXXX";
    struct stat st;

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
    fprintf(fp, "proc handle_error {error_msg} {\n");
    fprintf(fp, "    puts stderr $error_msg\n");
    fprintf(fp, "    exit 1\n");
    fprintf(fp, "}\n\n");
    fprintf(fp, "spawn telnet %s\n", host);
    fprintf(fp, "\n");
    fprintf(fp, "expect {\n");
    fprintf(fp, "    timeout {\n");
    fprintf(fp, "        handle_error \"Error: Telnet connection timeout to %s\"\n", host);
    fprintf(fp, "    }\n");
    fprintf(fp, "    eof {\n");
    fprintf(fp, "        handle_error \"Error: Connection closed (EOF) to %s\"\n", host);
    fprintf(fp, "    }\n");
    fprintf(fp, "    \"Connection refused\" {\n");
    fprintf(fp, "        handle_error \"Error: Connection refused by %s\"\n", host);
    fprintf(fp, "    }\n");
    fprintf(fp, "    \"No route to host\" {\n");
    fprintf(fp, "        handle_error \"Error: No route to host %s\"\n", host);
    fprintf(fp, "    }\n");
    fprintf(fp, "    \"Username:\"\n");
    fprintf(fp, "}\n");
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

    int error_fd = mkstemp(error_log);
    if (error_fd != -1) {
        close(error_fd);
    }

    // Запускаем expect скрипт и сохраняем вывод
    snprintf(command, sizeof(command), "(%s 2>> %s) > cisco.cfg", script_path, error_log);
    result = system(command);

    if (result == -1) {
        fprintf(stderr, "Error: Failed to execute Expect script\n");
        unlink(script_path);
        unlink(error_log);
        return -1;
    }

    // Проверяем, был ли создан файл cisco.cfg
    if (stat("cisco.cfg", &st) != 0) {
        fprintf(stderr, "Error: No output file created. Connection to %s failed.\n", host);
        unlink(script_path);
        unlink(error_log);
        return -1;
    }

    // Проверяем размер файла
    if (st.st_size == 0) {
        fprintf(stderr, "Error: Empty configuration received from %s\n", host);
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

    // Удаляем временный файл
    unlink(script_path);
    unlink(error_log);

    // Извлекаем только конфигурацию
    system("sed -n '/Building configuration/,/end/p' cisco.cfg | sed '1d;$d' > cisco_clean.cfg");
    system("mv cisco_clean.cfg cisco.cfg");

    return result == 0 ? 0 : -1;
}