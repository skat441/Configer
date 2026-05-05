#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <time.h>
#include <sys/stat.h>
#include <libgen.h>
#include <string.h>
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
    char local_dir[1024];
    char mkdir_cmd[2048];
    time_t now;
    struct tm *tm_now;
    pid_t pid;
    int status;

    (void)remote_path;

    // Создаем директорию для сохранения файла, если её нет
    strncpy(local_dir, local_path, sizeof(local_dir) - 1);
    local_dir[sizeof(local_dir) - 1] = '\0';
    
    // Извлекаем путь директории (dirname)
    char *last_slash = strrchr(local_dir, '/');
    if (last_slash != NULL) {
        *last_slash = '\0';
        snprintf(mkdir_cmd, sizeof(mkdir_cmd), "mkdir -p %s", local_dir);
        system(mkdir_cmd);
        *last_slash = '/'; // Восстанавливаем для дальнейшего использования
    }

    now = time(NULL);
    tm_now = localtime(&now);
    if (tm_now == NULL || strftime(export_base, sizeof(export_base), "backup-%Y%m%d-%H%M%S", tm_now) == 0) {
        fprintf(stderr, "Failed to generate backup filename\n");
        return 1;
    }

    snprintf(export_file, sizeof(export_file), "%s.rsc", export_base);
    snprintf(export_command, sizeof(export_command), "export file=%s", export_base);
    snprintf(host_spec, sizeof(host_spec), "%s@%s", mikrotik_user, mikrotik_host);
    snprintf(source_spec, sizeof(source_spec), "%s:%s", host_spec, export_file);

    printf("Exporting configuration from MikroTik device %s...\n", mikrotik_host);
    
    // Fork для выполнения export команды на MikroTik
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

        perror("execl failed (ssh)");
        _exit(1);
    } else if (pid > 0) {
        waitpid(pid, &status, 0);
        if (!(WIFEXITED(status) && WEXITSTATUS(status) == 0)) {
            fprintf(stderr, "Failed to export configuration from MikroTik\n");
            return 1;
        }
    } else {
        perror("fork failed (ssh)");
        return 1;
    }

    printf("Copying backup file to %s...\n", local_path);
    
    // Fork для выполнения scp
    pid = fork();
    if (pid == 0) {
        execl("/usr/bin/sshpass", 
              "sshpass",
              "-p", mikrotik_password,
              "/usr/bin/scp",
              "-o", "StrictHostKeyChecking=no",
              "-o", "UserKnownHostsFile=/dev/null",
              source_spec,
              local_path,
              (char *)0);
        
        perror("execl failed (scp)");
        _exit(1);
    } else if (pid > 0) {
        waitpid(pid, &status, 0);
        
        if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
            printf("Successfully copied to: %s\n", local_path);
            
            // Необязательно: удаляем временный файл на MikroTik
            char remove_cmd[800];
            snprintf(remove_cmd, sizeof(remove_cmd), 
                     "sshpass -p '%s' ssh -o StrictHostKeyChecking=no %s \"rm %s\" 2>/dev/null",
                     mikrotik_password, host_spec, export_file);
            system(remove_cmd);
            
            return 0;
        } else {
            fprintf(stderr, "Failed to copy backup file\n");
            return 1;
        }
    } else {
        perror("fork failed (scp)");
        return 1;
    }
}