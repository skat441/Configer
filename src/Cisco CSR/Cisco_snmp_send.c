#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define AGENT_IP           "11.11.11.11"
#define TFTP_SERVER_IP     "192.168.192.53"
#define SNMP_USER          "user"
#define SNMP_PASSWORD      "password"
#define TFTP_FILE_NAME     "routerconfig.cfg"
#define SESSION_ID         "930"

int main(int argc, char **argv) {
    char cmd_buffer[2048];

    // 1. Принудительная очистка сессии на Cisco на всякий случай
    printf("[1/2] Сброс старой SNMP сессии (ID: %s)...\n", SESSION_ID);
    snprintf(cmd_buffer, sizeof(cmd_buffer),
             "snmpset -v 3 -l authNoPriv -u %s -a SHA -A %s %s .1.3.6.1.4.1.9.9.96.1.1.1.1.14.%s i 6 > /dev/null 2>&1",
             SNMP_USER, SNMP_PASSWORD, AGENT_IP, SESSION_ID);
    system(cmd_buffer);
    sleep(1);

    // 2. Формируем команду ЗАГРУЗКИ (Download) файла на маршрутизатор
    printf("[2/2] Вызов snmpset для загрузки конфигурации из TFTP в running-config...\n");
    snprintf(cmd_buffer, sizeof(cmd_buffer),
             "snmpset -v 3 -l authNoPriv -u %s -a SHA -A %s %s "
             ".1.3.6.1.4.1.9.9.96.1.1.1.1.2.%s i 1 "   // Протокол: TFTP (1)
             ".1.3.6.1.4.1.9.9.96.1.1.1.1.3.%s i 1 "   // Источник: networkFile (1) <-- ИЗМЕНЕНО
             ".1.3.6.1.4.1.9.9.96.1.1.1.1.4.%s i 4 "   // Цель: runningConfig (4)  <-- ИЗМЕНЕНО
             ".1.3.6.1.4.1.9.9.96.1.1.1.1.5.%s a %s " // IP TFTP-сервера
             ".1.3.6.1.4.1.9.9.96.1.1.1.1.6.%s s %s " // Имя читаемого файла
             ".1.3.6.1.4.1.9.9.96.1.1.1.1.14.%s i 4",  // Статус: createAndGo (4)
             SNMP_USER, SNMP_PASSWORD, AGENT_IP, 
             SESSION_ID, SESSION_ID, SESSION_ID, 
             SESSION_ID, TFTP_SERVER_IP, 
             SESSION_ID, TFTP_FILE_NAME, 
             SESSION_ID);

    int exit_status = system(cmd_buffer);

    if (exit_status == 0) {
        printf("\nКоманда на загрузку успешно отправлена!\n");
        printf("Маршрутизатор Cisco скачивает файл и применяет настройки...\n");
    } else {
        fprintf(stderr, "Системный вызов snmpset вернул ошибку: %d\n", exit_status);
    }

    return 0;
}
