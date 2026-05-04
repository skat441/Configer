#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "../network_ops.h"

int send_cisco_csr_config(const char *host, const char *user, const char *pass, const char *config_file) {
    FILE *fp;
    char script_path[] = "/tmp/ssh_config_XXXXXX";
    char filtered_config[] = "/tmp/filtered_config_XXXXXX";
    char command[2048];
    int result;
    struct stat st;
    char log_file[] = "/tmp/cisco_apply.log";
    
    // Проверяем существование файла конфигурации
    if (stat(config_file, &st) != 0) {
        fprintf(stderr, "Error: Config file %s does not exist\n", config_file);
        return -1;
    }
    
    // Создаем отфильтрованную версию конфигурации (только команды)
    int fd_filter = mkstemp(filtered_config);
    if (fd_filter == -1) {
        perror("mkstemp filtered_config");
        return -1;
    }
    close(fd_filter);
    
    // Фильтруем конфигурацию - оставляем только валидные команды (тихий режим)
    snprintf(command, sizeof(command),
        "grep -v '^!' %s | "           
        "grep -v '^Building configuration' | "
        "grep -v '^Current configuration' | "
        "grep -v '^end$' | "
        "grep -v '^\\s*$' | "           
        "grep -v '^service call-home' | "
        "grep -v '^platform' | "
        "grep -v '^boot-' | "
        "grep -v '^crypto pki certificate' | "
        "grep -v '^certificate ' | "
        "grep -v '^\\s*[0-9A-F]' | "    
        "grep -v '^diagnostic bootup' | "
        "grep -v '^memory free' | "
        "grep -v '^spanning-tree' | "
        "grep -v '^redundancy' | "
        "grep -v '^subscriber templating' | "
        "grep -v '^multilink bundle-name' | "
        "grep -v '^license udi' | "
        "grep -v '^snmp-server community' | "
        "grep -v '^control-plane' | "
        "grep -v '^line con' | "
        "grep -v ' stopbits' | "
        "grep -v '^call-home' | "
        "grep -v '^profile ' | "
        "grep -v ' contact-email-addr' | "
        "grep -v ' active' | "
        "grep -v ' destination transport-method' | "
        "sed 's/^Router(config-if)# //' | "
        "sed 's/^Router(config)# //' | "
        "sed 's/^Router(ca-trustpoint)# //' | "
        "sed 's/^Router(config-cert-chain)# //' | "
        "sed 's/^Router(config-line)# //' | "
        "sed 's/^Router(cfg-call-home)# //' | "
        "sed 's/^Router(cfg-call-home-profile)# //' "
        "> %s 2>/dev/null", config_file, filtered_config);
    
    system(command);
    
    // Создаем expect скрипт для применения конфигурации
    int fd = mkstemp(script_path);
    if (fd == -1) {
        perror("mkstemp");
        unlink(filtered_config);
        return -1;
    }
    
    fp = fdopen(fd, "w");
    if (!fp) {
        perror("fdopen");
        close(fd);
        unlink(filtered_config);
        return -1;
    }
    
    fprintf(fp, "#!/usr/bin/expect -f\n");
    fprintf(fp, "set timeout 10\n");
    fprintf(fp, "log_user 0\n\n");  // Отключаем вывод на экран
    
    fprintf(fp, "spawn ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null %s@%s\n", user, host);
    fprintf(fp, "\n");
    
    // Аутентификация
    fprintf(fp, "expect {\n");
    fprintf(fp, "    \"password:\" { send \"%s\\r\" }\n", pass);
    fprintf(fp, "    \"Password:\" { send \"%s\\r\" }\n", pass);
    fprintf(fp, "    \"yes/no\" { send \"yes\\r\"; exp_continue }\n");
    fprintf(fp, "    timeout { puts \"Connection timeout\"; exit 1 }\n");
    fprintf(fp, "}\n");
    fprintf(fp, "\n");
    
    // Вход в enable режим
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
    fprintf(fp, "expect \"#\"\n");
    fprintf(fp, "\n");
    
    // Вход в режим конфигурации
    fprintf(fp, "send \"configure terminal\\r\"\n");
    fprintf(fp, "expect \"(config)#\"\n");
    fprintf(fp, "\n");
    
    // Применяем команды из отфильтрованного файла
    fprintf(fp, "set cfg [open %s r]\n", filtered_config);
    fprintf(fp, "set cmd_count 0\n");
    fprintf(fp, "set errors 0\n");
    fprintf(fp, "\n");
    
    fprintf(fp, "while {[gets $cfg line] != -1} {\n");
    fprintf(fp, "    set line [string trim $line]\n");
    fprintf(fp, "    if {$line == \"\"} continue\n");
    fprintf(fp, "    \n");
    fprintf(fp, "    # Пропускаем строки, которые не являются командами\n");
    fprintf(fp, "    if {[string match \"!*\" $line]} continue\n");
    fprintf(fp, "    if {[string match \"*Certificate*\" $line]} continue\n");
    fprintf(fp, "    if {[string match \"*self-signed*\" $line]} continue\n");
    fprintf(fp, "    if {[string match \"*hex*\" $line]} continue\n");
    fprintf(fp, "    \n");
    fprintf(fp, "    # Отправляем команду\n");
    fprintf(fp, "    send \"$line\\r\"\n");
    fprintf(fp, "    incr cmd_count\n");
    fprintf(fp, "    \n");
    fprintf(fp, "    # Ждем ответ\n");
    fprintf(fp, "    expect {\n");
    fprintf(fp, "        \"#\" {\n");
    fprintf(fp, "            # Команда выполнена\n");
    fprintf(fp, "        }\n");
    fprintf(fp, "        \"%% \" {\n");
    fprintf(fp, "            incr errors\n");
    fprintf(fp, "            expect \"#\"\n");
    fprintf(fp, "        }\n");
    fprintf(fp, "        timeout {\n");
    fprintf(fp, "            incr errors\n");
    fprintf(fp, "        }\n");
    fprintf(fp, "    }\n");
    fprintf(fp, "}\n");
    fprintf(fp, "close $cfg\n");
    fprintf(fp, "\n");
    
    // Завершаем конфигурацию
    fprintf(fp, "send \"end\\r\"\n");
    fprintf(fp, "expect \"#\"\n");
    fprintf(fp, "\n");
    
    // Сохраняем конфигурацию
    fprintf(fp, "send \"write memory\\r\"\n");
    fprintf(fp, "expect \"#\"\n");
    fprintf(fp, "\n");
    
    // Выход
    fprintf(fp, "send \"exit\\r\"\n");
    fprintf(fp, "expect eof\n");
    
    fclose(fp);
    
    // Делаем скрипт исполняемым
    chmod(script_path, 0755);
    
    // Запускаем expect скрипт и перенаправляем вывод в лог-файл
    snprintf(command, sizeof(command), "%s > %s 2>&1", script_path, log_file);
    result = system(command);
    
    // Удаляем временные файлы
    unlink(script_path);
    unlink(filtered_config);
    
    // Опционально: удаляем лог-файл, если не нужен
    // unlink(log_file);
    
    return result == 0 ? 0 : -1;
}