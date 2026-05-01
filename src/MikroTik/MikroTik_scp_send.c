#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/wait.h>
#include "../network_ops.h"

int mikrotik_scp_send(const char *mikrotik_user,
                      const char *mikrotik_host,
                      const char *mikrotik_password,
                      const char *local_path,
                      const char *remote_path) {
    char destination_spec[512];
    snprintf(destination_spec, sizeof(destination_spec), "%s@%s:%s", mikrotik_user, mikrotik_host, remote_path);
    
    pid_t pid = fork();
    
    if (pid == 0) {
        // Дочерний процесс - выполняем scp через sshpass
        execl("/usr/bin/sshpass", 
              "sshpass",
              "-p", mikrotik_password,
              "/usr/bin/scp",
              "-o", "StrictHostKeyChecking=no",
              "-o", "UserKnownHostsFile=/dev/null",
              local_path,
              destination_spec,
              (char *)0);
        
        // Если дошли сюда - ошибка
        perror("execl failed");
        return 1;
    } else if (pid > 0) {
        // Родительский процесс - ждем завершения
        int status;
        waitpid(pid, &status, 0);
        
        if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
            printf("Файл успешно отправлен: %s -> %s\n", local_path, destination_spec);
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