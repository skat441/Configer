#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

// Конфигурация параметров
#define TFTP_FILE_PATH     "../../configs/router-config.cfg"
#define TFTP_FILE_NAME     "router-config.cfg"
#define AGENT_IP           "11.11.11.11"
#define TFTP_SERVER_IP     "192.168.192.53"
#define SNMP_USER          "user"
#define SNMP_PASSWORD      "password"
#define SESSION_ID         "929"

// Функция для создания пустого файла с полными правами на запись
int create_tftp_stub_file() {
    printf("[1/3] Создание локального файла-заглушки: %s\n", TFTP_FILE_PATH);
    
    // Удаляем старый файл, если он был
    unlink(TFTP_FILE_PATH);

    // Создаем новый файл с правами 0666 (чтение/запись всем)
    int fd = open(TFTP_FILE_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) {
        perror("Ошибка создания файла на TFTP-сервере");
        return 0;
    }
    close(fd);
    printf("Файл успешно создан и открыт для записи.\n");
    return 1;
}

int main(int argc, char **argv) {
    char cmd_buffer[2048];

    // 1. Подготавливаем файл на TFTP
    if (!create_tftp_stub_file()) {
        exit(1);
    }
    sleep(1);
    // 2. Предварительно очищаем старую сессию на Cisco (на случай, если она зависла)
    printf("[2/3] Очистка старой SNMP сессии (ID: %s)...\n", SESSION_ID);
    snprintf(cmd_buffer, sizeof(cmd_buffer),
             "snmpset -v 3 -l authNoPriv -u %s -a SHA -A %s %s .1.3.6.1.4.1.9.9.96.1.1.1.1.14.%s i 6 > /dev/null 2>&1",
             SNMP_USER, SNMP_PASSWORD, AGENT_IP, SESSION_ID);
    system(cmd_buffer);
    sleep(1); // Небольшая пауза, чтобы Cisco успела обработать сброс

    // 3. Формируем и выполняем единую команду snmpset для копирования конфигурации
    printf("[3/3] Вызов snmpset для копирования конфигурации через TFTP...\n");
    snprintf(cmd_buffer, sizeof(cmd_buffer),
             "snmpset -v 3 -l authNoPriv -u %s -a SHA -A %s %s "
             ".1.3.6.1.4.1.9.9.96.1.1.1.1.2.%s i 1 "
             ".1.3.6.1.4.1.9.9.96.1.1.1.1.3.%s i 4 "
             ".1.3.6.1.4.1.9.9.96.1.1.1.1.4.%s i 1 "
             ".1.3.6.1.4.1.9.9.96.1.1.1.1.5.%s a %s "
             ".1.3.6.1.4.1.9.9.96.1.1.1.1.6.%s s %s "
             ".1.3.6.1.4.1.9.9.96.1.1.1.1.14.%s i 4",
             SNMP_USER, SNMP_PASSWORD, AGENT_IP, 
             SESSION_ID, SESSION_ID, SESSION_ID, 
             SESSION_ID, TFTP_SERVER_IP, 
             SESSION_ID, TFTP_FILE_NAME, 
             SESSION_ID);

    // Выводим команду в консоль для наглядности дебага
    printf("\nВыполняется системная команда:\n%s\n\n", cmd_buffer);

    // Запуск через shell
    int exit_status = system(cmd_buffer);

    if (exit_status == 0) {
        printf("Команда отправлена успешно! Проверьте файл конфигурации.\n");
    } else {
        fprintf(stderr, "Системный вызов snmpset вернул код ошибки: %d\n", exit_status);
    }

    return 0;
}
