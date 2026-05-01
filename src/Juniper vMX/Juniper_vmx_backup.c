#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>
#include "../network_ops.h"

static int wait_child_exit(pid_t pid) {
    int status;

    if (pid < 0) {
        perror("fork failed");
        return 1;
    }

    if (waitpid(pid, &status, 0) < 0) {
        perror("waitpid failed");
        return 1;
    }

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        return 1;
    }

    return 0;
}

int juniper_vmx_backup_get(const char *juniper_user,
                           const char *juniper_host,
                           const char *juniper_password,
                           const char *local_path) {
    char host_spec[512];
    char source_spec[1024];
    const char *remote_path = "/var/tmp/vmx-backup.conf";
    pid_t pid;

    snprintf(host_spec, sizeof(host_spec), "%s@%s", juniper_user, juniper_host);
    snprintf(source_spec, sizeof(source_spec), "%s:%s", host_spec, remote_path);

    pid = fork();
    if (pid == 0) {
        execl("/usr/bin/sshpass",
              "sshpass",
              "-p", juniper_password,
              "/usr/bin/ssh",
              "-o", "StrictHostKeyChecking=no",
              "-o", "UserKnownHostsFile=/dev/null",
              host_spec,
              "cli -c \"show configuration | save /var/tmp/vmx-backup.conf\"",
              (char *)0);
        perror("execl failed");
        _exit(1);
    }
    if (wait_child_exit(pid) != 0) {
        fprintf(stderr, "Failed to save backup on Juniper vMX\n");
        return 1;
    }

    pid = fork();
    if (pid == 0) {
        execl("/usr/bin/sshpass",
              "sshpass",
              "-p", juniper_password,
              "/usr/bin/scp",
              "-o", "StrictHostKeyChecking=no",
              "-o", "UserKnownHostsFile=/dev/null",
              source_spec,
              local_path,
              (char *)0);
        perror("execl failed");
        _exit(1);
    }
    if (wait_child_exit(pid) != 0) {
        fprintf(stderr, "Failed to copy backup file via scp\n");
        return 1;
    }

    printf("Juniper backup saved and downloaded to %s\n", local_path);
    return 0;
}

int juniper_vmx_backup_send(const char *juniper_user,
                            const char *juniper_host,
                            const char *juniper_password,
                            const char *local_path) {
    char host_spec[512];
    char destination_spec[1024];
    const char *remote_path = "/var/tmp/vmx-backup.conf";
    pid_t pid;

    snprintf(host_spec, sizeof(host_spec), "%s@%s", juniper_user, juniper_host);
    snprintf(destination_spec, sizeof(destination_spec), "%s:%s", host_spec, remote_path);

    pid = fork();
    if (pid == 0) {
        execl("/usr/bin/sshpass",
              "sshpass",
              "-p", juniper_password,
              "/usr/bin/scp",
              "-o", "StrictHostKeyChecking=no",
              "-o", "UserKnownHostsFile=/dev/null",
              local_path,
              destination_spec,
              (char *)0);
        perror("execl failed");
        _exit(1);
    }
    if (wait_child_exit(pid) != 0) {
        fprintf(stderr, "Failed to upload backup file via scp\n");
        return 1;
    }

    pid = fork();
    if (pid == 0) {
        execl("/usr/bin/sshpass",
              "sshpass",
              "-p", juniper_password,
              "/usr/bin/ssh",
              "-o", "StrictHostKeyChecking=no",
              "-o", "UserKnownHostsFile=/dev/null",
              host_spec,
              "cli -c \"configure; load override /var/tmp/vmx-backup.conf\"",
              (char *)0);
        perror("execl failed");
        _exit(1);
    }
    if (wait_child_exit(pid) != 0) {
        fprintf(stderr, "Failed to load config on Juniper vMX\n");
        return 1;
    }

    printf("Juniper config uploaded and loaded from %s\n", local_path);
    return 0;
}
