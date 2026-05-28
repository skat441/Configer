#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>

// Значения для CCCOPY-PROTOCOL
#define PROTOCOL_TFTP  1
#define PROTOCOL_RCP   3

// Значения для CCCOPY-SOURCE-FILE-TYPE
#define SOURCE_RUNNING  4   // running-config

// Значения для CCCOPY-DEST-FILE-TYPE
#define DEST_NETWORK    1   // network file (copy to server)

// Значения для CCCOPY-ENTRY-ROW-STATUS
#define STATUS_DESTROY  6   // destroy the table entry
#define STATUS_CREATE   4   // create and go

/**
 * Проверяет, настроен ли TFTP сервер на устройстве
 * и какие протоколы поддерживаются
 */
int check_device_capabilities(const char *device_ip, const char *community) {
    char command[512];
    char output[256];
    FILE *fp;
    
    printf("\n[DIAG] Checking device capabilities...\n");
    
    // Проверяем доступные протоколы
    snprintf(command, sizeof(command),
             "snmpget -v2c -c %s %s .1.3.6.1.4.1.9.9.96.1.1.1.1.1.0 2>&1",
             community, device_ip);
    
    fp = popen(command, "r");
    if (fp) {
        while (fgets(output, sizeof(output), fp)) {
            printf("  %s", output);
        }
        pclose(fp);
    }
    
    printf("\n");
    return 0;
}

/**
 * Проверяет возможность копирования конфигурации (важно!)
 */
int check_copy_capability(const char *device_ip, const char *community) {
    char command[512];
    char output[256];
    FILE *fp;
    
    printf("[DIAG] Checking copy capability...\n");
    
    // Проверяем поддерживаемые протоколы
    snprintf(command, sizeof(command),
             "snmptable -v2c -c %s %s CISCO-CONFIG-COPY-MIB 2>&1 | head -20",
             community, device_ip);
    
    system(command);
    
    return 0;
}

int cisco_snmp_backup_simple(const char *device_ip, const char *community,
                              const char *tftp_server, const char *filename) {
    char command[1024];
    char result_buf[1024];
    int index;
    int result;
    FILE *fp;
    
    // Инициализация random
    srand(time(NULL));
    
    printf("\n========================================\n");
    printf("Cisco SNMP Backup Tool\n");
    printf("========================================\n");
    printf("[INFO] Device: %s\n", device_ip);
    printf("[INFO] Community: %s\n", community);
    printf("[INFO] TFTP Server: %s\n", tftp_server);
    printf("[INFO] Filename: %s\n", filename);
    printf("========================================\n\n");
    
    // Диагностика устройства
    check_device_capabilities(device_ip, community);
    
    // Проверка copy capability
    check_copy_capability(device_ip, community);
    
    // Генерируем уникальный индекс (от 1 до 65535 для совместимости)
    index = (rand() % 65535) + 1;
    printf("\n[INFO] Using transaction index: %d\n", index);
    
    // Вариант 1: SNMP SET с использованием snmpset (с правильным порядком)
    printf("\n[STEP 1] Sending SNMP SET request...\n");
    
    snprintf(command, sizeof(command),
        "snmpset -v2c -c %s -t 10 %s "
        ".1.3.6.1.4.1.9.9.96.1.1.1.1.2.%d i %d "      // ccCopyProtocol
        ".1.3.6.1.4.1.9.9.96.1.1.1.1.3.%d i %d "      // ccCopySourceFileType
        ".1.3.6.1.4.1.9.9.96.1.1.1.1.4.%d i %d "      // ccCopyDestFileType
        ".1.3.6.1.4.1.9.9.96.1.1.1.1.5.%d a %s "      // ccCopyServerAddress
        ".1.3.6.1.4.1.9.9.96.1.1.1.1.6.%d s %s "      // ccCopyFileName
        ".1.3.6.1.4.1.9.9.96.1.1.1.1.14.%d i %d 2>&1", // ccCopyEntryRowStatus
        community, device_ip,
        index, PROTOCOL_TFTP,
        index, SOURCE_RUNNING,
        index, DEST_NETWORK,
        index, tftp_server,
        index, filename,
        index, STATUS_CREATE);
    
    printf("[INFO] Executing: %s\n", command);
    result = system(command);
    
    if (result != 0) {
        fprintf(stderr, "\n[ERROR] SNMP SET failed\n");
        
        // Пробуем альтернативный вариант с RCP протоколом
        printf("\n[INFO] Trying alternative protocol (RCP)...\n");
        snprintf(command, sizeof(command),
            "snmpset -v2c -c %s -t 10 %s "
            ".1.3.6.1.4.1.9.9.96.1.1.1.1.2.%d i %d "      // ccCopyProtocol = RCP(3)
            ".1.3.6.1.4.1.9.9.96.1.1.1.1.3.%d i %d "      // ccCopySourceFileType
            ".1.3.6.1.4.1.9.9.96.1.1.1.1.4.%d i %d "      // ccCopyDestFileType
            ".1.3.6.1.4.1.9.9.96.1.1.1.1.5.%d a %s "      // ccCopyServerAddress
            ".1.3.6.1.4.1.9.9.96.1.1.1.1.6.%d s %s "      // ccCopyFileName
            ".1.3.6.1.4.1.9.9.96.1.1.1.1.14.%d i %d 2>&1", // ccCopyEntryRowStatus
            community, device_ip,
            index, 3,  // RCP protocol
            index, SOURCE_RUNNING,
            index, DEST_NETWORK,
            index, tftp_server,
            index, filename,
            index, STATUS_CREATE);
        
        printf("[INFO] Executing: %s\n", command);
        result = system(command);
        
        if (result != 0) {
            fprintf(stderr, "[ERROR] Both TFTP and RCP protocols failed\n");
            return -1;
        }
    }
    
    // Ожидание завершения
    printf("\n[STEP 2] Waiting for backup to complete...\n");
    
    for (int i = 0; i < 60; i++) {
        sleep(2);
        
        // Каждые 10 секунд выводим статус
        if (i % 5 == 0 && i > 0) {
            printf("[INFO] Still waiting... (%d/60 seconds)\n", i * 2);
        }
        
        // Проверяем статус операции
        snprintf(command, sizeof(command),
            "snmpget -v2c -c %s -t 3 %s .1.3.6.1.4.1.9.9.96.1.1.1.1.10.%d 2>&1",
            community, device_ip, index);
        
        fp = popen(command, "r");
        if (fp) {
            char output[512];
            while (fgets(output, sizeof(output), fp)) {
                // Ищем статус
                if (strstr(output, "INTEGER:")) {
                    int state = 0;
                    if (sscanf(output, "%*[^0-9]%d", &state) == 1) {
                        const char *state_str = "";
                        switch (state) {
                            case 2: state_str = "SUCCESSFUL ✓"; break;
                            case 3: state_str = "RUNNING..."; break;
                            case 4: state_str = "WAITING..."; break;
                            case 5: state_str = "FAILED ✗"; break;
                            default: state_str = "UNKNOWN"; break;
                        }
                        printf("[INFO] State: %s (%d)\n", state_str, state);
                        
                        if (state == 2) {
                            printf("\n[SUCCESS] Backup completed!\n");
                            printf("[INFO] File: %s\n", filename);
                            printf("[INFO] TFTP Server: %s\n", tftp_server);
                            pclose(fp);
                            
                            // Очищаем запись в таблице
                            printf("[INFO] Cleaning up...\n");
                            snprintf(command, sizeof(command),
                                "snmpset -v2c -c %s %s .1.3.6.1.4.1.9.9.96.1.1.1.1.14.%d i %d 2>&1",
                                community, device_ip, index, STATUS_DESTROY);
                            system(command);
                            
                            return 0;
                        } else if (state == 5) {
                            printf("\n[ERROR] Backup failed on device\n");
                            pclose(fp);
                            return -1;
                        }
                    }
                }
            }
            pclose(fp);
        }
    }
    
    fprintf(stderr, "\n[ERROR] Timeout waiting for backup completion (120 seconds)\n");
    return -1;
}

int main(int argc, char **argv) {
    const char *device_ip = "192.168.0.199";
    const char *community = "BACKUPCOMMUNITY";
    const char *tftp_server = "192.168.0.131";
    const char *filename = "config_backup.cfg";
    
    if (argc == 5) {
        device_ip = argv[1];
        community = argv[2];
        tftp_server = argv[3];
        filename = argv[4];
    } else if (argc != 1 && argc != 5) {
        fprintf(stderr, "Usage: %s <device_ip> <community> <tftp_server> <filename>\n", argv[0]);
        fprintf(stderr, "Example: %s 192.168.0.199 BACKUPCOMMUNITY 192.168.0.131 router.cfg\n", argv[0]);
        return 1;
    }
    
    return cisco_snmp_backup_simple(device_ip, community, tftp_server, filename);
}