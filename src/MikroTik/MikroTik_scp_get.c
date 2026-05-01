#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <time.h>
#include "../network_ops.h"

int mikrotik_scp_get(const char *mikrotik_user,
                     const char *mikrotik_host,
                     const char *mikrotik_password,
                     const char *remote_path,
                     const char *local_path) {
    char host_spec[512];
    char source_spec[1024];
    char export_base[64];
    char export_file[80];
    char export_command[128];
    time_t now;
    struct tm *tm_now;
    pid_t pid;

    (void)remote_path;

    now = time(NULL);
    tm_now = localtime(&now);
    if (tm_now == NULL || strftime(export_base, sizeof(export_base), "backup-%Y%m%d-%H%M%S", tm_now) == 0) {
        fprintf(stderr, "Не удалось сформировать имя backup файла\n");
        return 1;
    }

    snprintf(export_file, sizeof(export_file), "%s.rsc", export_base);
    snprintf(export_command, sizeof(export_command), "export file=%s", export_base);
    snprintf(host_spec, sizeof(host_spec), "%s@%s", mikrotik_user, mikrotik_host);
    snprintf(source_spec, sizeof(source_spec), "%s:%s", host_spec, export_file);

    pid = fork();
    if (pid == 0) {
        execl("/usr/bin/sshpass",
              "sshpass",
              "-p", mikrotik_password,
              "/usr/bin/ssh",
              "-o", "StrictHostKeyChecking=no",
              "-o", "UserKnownHostsFile=/dev/null",
              host_spec,
              export_command,
              (char *)0);

        perror("execl failed");
        _exit(1);
    } else if (pid > 0) {
        int status;
        waitpid(pid, &status, 0);
        if (!(WIFEXITED(status) && WEXITSTATUS(status) == 0)) {
            printf("Ошибка экспорта конфигурации\n");
            return 1;
        }
    } else {
        perror("fork failed");
        return 1;
    }

    pid = fork();
    if (pid == 0) {
        // Дочерний процесс - выполняем scp через sshpass
        execl("/usr/bin/sshpass", 
              "sshpass",
              "-p", mikrotik_password,
              "/usr/bin/scp",
              "-o", "StrictHostKeyChecking=no",
              "-o", "UserKnownHostsFile=/dev/null",
              source_spec,
              local_path,
              (char *)0);
        
        // Если дошли сюда - ошибка
        perror("execl failed");
        return 1;
    } else if (pid > 0) {
        // Родительский процесс - ждем завершения
        int status;
        waitpid(pid, &status, 0);
        
        if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
            printf("Файл %s успешно скопирован в %s\n", export_file, local_path);
            return 0;
        } else {
            printf("Ошибка копирования\n");
            return 1;
        }
    } else {
        perror("fork failed");
        return 1;
    }
}