#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <time.h>
#include <sys/stat.h>
#include <libgen.h>
#include <string.h>
#include "../network_ops.h"

int mikrotik_scp_send(const char *mikrotik_user,
                      const char *mikrotik_host,
                      const char *mikrotik_password,
                      const char *local_path,
                      const char *remote_path) {
    char host_spec[512];
    char dest_spec[1024];
    char import_command[256];
    char temp_name[128];
    pid_t pid;
    int status;

    snprintf(host_spec, sizeof(host_spec), "%s@%s", mikrotik_user, mikrotik_host);
    snprintf(dest_spec, sizeof(dest_spec), "%s:%s", host_spec, remote_path);
    
    // Генерируем временное имя для импорта
    snprintf(temp_name, sizeof(temp_name), "config_%ld", time(NULL));
    snprintf(import_command, sizeof(import_command), "/import %s", remote_path);

    printf("Sending configuration to MikroTik device %s...\n", mikrotik_host);
    
    // Копируем файл на устройство через SCP
    pid = fork();
    if (pid == 0) {
        execl("/usr/bin/sshpass",
              "sshpass",
              "-p", mikrotik_password,
              "/usr/bin/scp",
              "-o", "StrictHostKeyChecking=no",
              "-o", "UserKnownHostsFile=/dev/null",
              local_path,
              dest_spec,
              (char *)0);
        
        perror("execl failed (scp send)");
        _exit(1);
    } else if (pid > 0) {
        waitpid(pid, &status, 0);
        
        if (!(WIFEXITED(status) && WEXITSTATUS(status) == 0)) {
            fprintf(stderr, "Failed to copy configuration to MikroTik\n");
            return 1;
        }
    } else {
        perror("fork failed (scp send)");
        return 1;
    }
    
    // Импортируем конфигурацию на MikroTik
    printf("Importing configuration on MikroTik...\n");
    
    pid = fork();
    if (pid == 0) {
        execl("/usr/bin/sshpass",
              "sshpass",
              "-p", mikrotik_password,
              "/usr/bin/ssh",
              "-o", "StrictHostKeyChecking=no",
              "-o", "UserKnownHostsFile=/dev/null",
              host_spec,
              import_command,
              (char *)0);
        
        perror("execl failed (ssh import)");
        _exit(1);
    } else if (pid > 0) {
        waitpid(pid, &status, 0);
        
        if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
            printf("Configuration successfully imported on MikroTik\n");
            
            // Удаляем файл с устройства после импорта
            char remove_cmd[1024];
            snprintf(remove_cmd, sizeof(remove_cmd), 
                     "sshpass -p '%s' ssh -o StrictHostKeyChecking=no %s \"rm %s\" 2>/dev/null",
                     mikrotik_password, host_spec, remote_path);
            system(remove_cmd);
            
            return 0;
        } else {
            fprintf(stderr, "Failed to import configuration on MikroTik\n");
            return 1;
        }
    } else {
        perror("fork failed (ssh import)");
        return 1;
    }
}