#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <errno.h>
#include <ncurses.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include "../network_ops.h"
#include "../store/credential_store.h"

#define STORE_FILE_PATH "./devices.store.enc"
#define MENU_MAX_ITEMS 128
#define TEMP_YAML_PATH "/tmp/configer_devices_edit.yaml"

typedef enum {
    SCREEN_TOP_LEVEL = 0,
    SCREEN_GET_MENU,
    SCREEN_SEND_MENU,
    SCREEN_MANAGE_MENU
} ScreenState;

typedef enum {
    ITEM_NAV_GET = 0,
    ITEM_NAV_SEND,
    ITEM_NAV_MANAGE,
    ITEM_RUN_GET_DEVICE,
    ITEM_RUN_SEND_DEVICE,
    ITEM_RUN_GET_ALL,
    ITEM_RUN_SEND_ALL,
    ITEM_ADD_DEVICE,
    ITEM_EDIT_DEVICE,
    ITEM_DELETE_DEVICE,
    ITEM_BULK_EDIT,
    ITEM_SHOW_HISTORY,
    ITEM_BACK
} ItemAction;

typedef struct {
    char label[512];
    ItemAction action;
    size_t device_index;
} MenuItem;

typedef struct {
    const DeviceRecord *device;
    int is_send;
} DeviceActionContext;

static CredentialStore g_store;
static char g_master_password[128];

static int resolve_config_path(const char *link_path, char *real_path, size_t size) {
    ssize_t len;
    
    len = readlink(link_path, real_path, size - 1);
    if (len == -1) {
        return -1;
    }
    
    real_path[len] = '\0';
    return 0;
}

static void sanitize_device_name(char *dest, size_t size, const char *src) {
    if (!dest || !src || size == 0) return;
    
    strncpy(dest, src, size - 1);
    dest[size - 1] = '\0';
    
    for (char *p = dest; *p; p++) {
        if (*p == '/' || *p == ' ' || *p == '|' || *p == ':' || 
            *p == '*' || *p == '?' || *p == '"' || *p == '<' || 
            *p == '>' || *p == '\\') {
            *p = '_';
        }
    }
}

static int show_device_history(const DeviceRecord *device) {
    char dir_path[512];
    char command[2400];
    
    snprintf(dir_path, sizeof(dir_path), "./../../configs/%s", device->name);
    
    printf("\n=== Configuration history for: %s ===\n", device->name);
    printf("Directory: %s\n\n", dir_path);
    
    snprintf(command, sizeof(command), 
             "ls -lah %s/*.cfg %s/*.conf %s/*.rsc %s/*.config 2>/dev/null | tail -20", 
             dir_path, dir_path, dir_path, dir_path);
    system(command);
    
    printf("\nPress Enter to continue...");
    getchar();
    
    return 0;
}

// Создание директории, если её нет
static void ensure_directory(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) {
        mkdir(path, 0755);
    }
}

static void normalize_single_line(char *text) {
    size_t i;
    for (i = 0; text[i] != '\0'; ++i) {
        if (text[i] == '\n' || text[i] == '\r' || text[i] == '\t') {
            text[i] = ' ';
        }
    }
}

static int run_action_capture_stderr(int (*action)(void *), void *ctx, char *status, size_t status_size) {
    char temp_path[] = "/tmp/configer_stderr_XXXXXX";
    char errbuf[256];
    int fd;
    int saved_stderr;
    int rc;
    FILE *fp;
    size_t n;

    fd = mkstemp(temp_path);
    if (fd < 0) {
        snprintf(status, status_size, "Failed: cannot create stderr capture");
        return -1;
    }

    saved_stderr = dup(STDERR_FILENO);
    if (saved_stderr < 0 || dup2(fd, STDERR_FILENO) < 0) {
        if (saved_stderr >= 0) {
            close(saved_stderr);
        }
        close(fd);
        unlink(temp_path);
        snprintf(status, status_size, "Failed: cannot redirect stderr");
        return -1;
    }

    close(fd);
    rc = action(ctx);
    fflush(stderr);

    dup2(saved_stderr, STDERR_FILENO);
    close(saved_stderr);

    if (rc == 0) {
        snprintf(status, status_size, "Done successfully");
    } else {
        fp = fopen(temp_path, "r");
        if (fp) {
            n = fread(errbuf, 1, sizeof(errbuf) - 1, fp);
            errbuf[n] = '\0';
            fclose(fp);
        } else {
            errbuf[0] = '\0';
        }

        normalize_single_line(errbuf);
        if (errbuf[0] != '\0') {
            snprintf(status, status_size, "Failed: %s", errbuf);
        } else {
            snprintf(status, status_size, "Operation failed (no stderr output)");
        }
    }

    unlink(temp_path);
    return rc;
}

static void draw_menu(const char *title,
                      const char *subtitle,
                      const MenuItem *items,
                      size_t count,
                      size_t selected,
                      const char *status) {
    size_t i;

    clear();
    mvprintw(1, 2, "%s", title);
    mvprintw(2, 2, "%s", subtitle);
    for (i = 0; i < count; ++i) {
        if (i == selected) {
            attron(A_REVERSE);
        }
        mvprintw((int)i + 4, 4, "%s", items[i].label);
        if (i == selected) {
            attroff(A_REVERSE);
        }
    }
    mvhline(LINES - 3, 1, ACS_HLINE, COLS - 2);
    mvprintw(LINES - 2, 2, "Status: %s", status);
    refresh();
}

static void restore_curses_input_state(void) {
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);
    flushinp();
}

static int normalize_input_key(int ch) {
    static int utf8_lead_d0 = 0;
    if (utf8_lead_d0) {
        utf8_lead_d0 = 0;
        switch (ch) {
            case 0xB9: return 'q';
            case 0x99: return 'Q';
            case 0xBE: return 'j';
            case 0x9E: return 'J';
            case 0xBB: return 'k';
            case 0x9B: return 'K';
            case 0xB8: return 'b';
            case 0x98: return 'B';
            default: return ch;
        }
    }
    if (ch == 0xD0) {
        utf8_lead_d0 = 1;
        return 0;
    }
    return ch;
}

static int prompt_line(const char *prompt, char *buf, size_t buf_size) {
    if (buf_size == 0) {
        return -1;
    }
    printf("%s", prompt);
    fflush(stdout);
    if (!fgets(buf, (int)buf_size, stdin)) {
        return -1;
    }
    buf[strcspn(buf, "\r\n")] = '\0';
    return 0;
}

static int prompt_password(const char *prompt, char *buf, size_t buf_size) {
    struct termios oldt;
    struct termios newt;

    if (buf_size == 0 || tcgetattr(STDIN_FILENO, &oldt) != 0) {
        return -1;
    }

    newt = oldt;
    newt.c_lflag &= (tcflag_t)~ECHO;
    if (tcsetattr(STDIN_FILENO, TCSANOW, &newt) != 0) {
        return -1;
    }

    printf("%s", prompt);
    fflush(stdout);
    if (!fgets(buf, (int)buf_size, stdin)) {
        tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
        return -1;
    }
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    printf("\n");
    buf[strcspn(buf, "\r\n")] = '\0';
    return 0;
}

static int is_valid_ip(const char *ip) {
    int dots = 0;
    size_t i;
    if (ip[0] == '\0') {
        return 0;
    }
    for (i = 0; ip[i] != '\0'; ++i) {
        if (ip[i] == '.') {
            dots++;
        } else if (!isdigit((unsigned char)ip[i])) {
            return 0;
        }
    }
    return dots == 3;
}


// Обновленный execute_device_action
// Обновленный execute_device_action
static int execute_device_action(void *ctx_ptr) {
    DeviceActionContext *ctx = (DeviceActionContext *)ctx_ptr;
    const DeviceRecord *d = ctx->device;
    char config_path[1024];
    char safe_name[256];
    char real_path[1024];
    int result;
    
    // Очищаем имя устройства для использования в пути
    sanitize_device_name(safe_name, sizeof(safe_name), d->name);
    
    if (!ctx->is_send) {
        // GET операция - получаем конфигурацию с устройства
        switch (d->type) {
            case DEVICE_TYPE_ELTEX_ME:
                {
                    char timestamp[32];
                    time_t now = time(NULL);
                    struct tm *t = localtime(&now);
                    strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S", t);
                    
                    snprintf(config_path, sizeof(config_path), "./../../configs/%s/%s_%s.config", 
                             safe_name, safe_name, timestamp);
                    
                    char dir_path[512];
                    snprintf(dir_path, sizeof(dir_path), "./../../configs/%s", safe_name);
                    ensure_directory(dir_path);
                    //TO_DO:
                    //Получить пароль и ip от текущего пользователя для выполнения scp команды, если этого пароля и ip ещё нету
                    result = eltex_me_scp_get(safe_name, d->username, d->ip, d->password, getlogin(), "192.168.192.53", "0512", config_path);
                    
                    if (result == 0) {
                        char latest_path[512];
                        snprintf(latest_path, sizeof(latest_path), "./../../configs/%s/latest.config", safe_name);
                        unlink(latest_path);
                        symlink(config_path, latest_path);
                    }
                }
                break;
            case DEVICE_TYPE_JUNIPER:
                {
                    char timestamp[32];
                    time_t now = time(NULL);
                    struct tm *t = localtime(&now);
                    strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S", t);
                    
                    snprintf(config_path, sizeof(config_path), "./../../configs/%s/%s_%s.conf", 
                             safe_name, safe_name, timestamp);
                    
                    char dir_path[512];
                    snprintf(dir_path, sizeof(dir_path), "./../../configs/%s", safe_name);
                    ensure_directory(dir_path);
                    
                    result = juniper_vmx_backup_get(d->username, d->ip, d->password, config_path);
                    
                    if (result == 0) {
                        char latest_path[512];
                        snprintf(latest_path, sizeof(latest_path), "./../../configs/%s/latest.conf", safe_name);
                        unlink(latest_path);
                        symlink(config_path, latest_path);
                    }
                }
                break;
            case DEVICE_TYPE_CISCO_XRV:
                result = get_cisco_config(d->ip, d->username, d->password);
                if (result == 0) {
                    // Генерируем путь с временной меткой
                    char timestamp[32];
                    time_t now = time(NULL);
                    struct tm *t = localtime(&now);
                    strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S", t);
                    
                    snprintf(config_path, sizeof(config_path), "./../../configs/%s/%s_%s.cfg", 
                             safe_name, safe_name, timestamp);
                    
                    // Создаем директорию если её нет
                    char dir_path[512];
                    snprintf(dir_path, sizeof(dir_path), "./../../configs/%s", safe_name);
                    ensure_directory(dir_path);
                    
                    // Перемещаем файл
                    char cmd[1060];
                    snprintf(cmd, sizeof(cmd), "mv cisco.cfg \"%s\" 2>/dev/null", config_path);
                    system(cmd);
                    
                    // Обновляем symlink на последнюю версию
                    char latest_path[512];
                    snprintf(latest_path, sizeof(latest_path), "./../../configs/%s/latest.cfg", safe_name);
                    unlink(latest_path);
                    symlink(config_path, latest_path);
                }
                break;
                
            case DEVICE_TYPE_CISCO_CSR:
                result = get_cisco_csr_config(d->ip, d->username, d->password);
                if (result == 0) {
                    char timestamp[32];
                    time_t now = time(NULL);
                    struct tm *t = localtime(&now);
                    strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S", t);
                    
                    snprintf(config_path, sizeof(config_path), "./../../configs/%s/%s_%s.cfg", 
                             safe_name, safe_name, timestamp);
                    
                    char dir_path[512];
                    snprintf(dir_path, sizeof(dir_path), "./../../configs/%s", safe_name);
                    ensure_directory(dir_path);
                    
                    char cmd[1060];
                    snprintf(cmd, sizeof(cmd), "mv cisco.cfg \"%s\" 2>/dev/null", config_path);
                    system(cmd);
                    
                    char latest_path[512];
                    snprintf(latest_path, sizeof(latest_path), "./../../configs/%s/latest.cfg", safe_name);
                    unlink(latest_path);
                    symlink(config_path, latest_path);
                }
                break;
                
            case DEVICE_TYPE_MIKROTIK:
                {
                    // Генерируем путь с временной меткой
                    char timestamp[32];
                    time_t now = time(NULL);
                    struct tm *t = localtime(&now);
                    strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S", t);
                    
                    snprintf(config_path, sizeof(config_path), "./../../configs/%s/%s_%s.rsc", 
                             safe_name, safe_name, timestamp);
                    
                    // Создаем директорию
                    char dir_path[512];
                    snprintf(dir_path, sizeof(dir_path), "./../../configs/%s", safe_name);
                    ensure_directory(dir_path);
                    
                    // Получаем конфигурацию
                    result = mikrotik_scp_get(d->username, d->ip, d->password, "", config_path);
                    
                    if (result == 0) {
                        // Обновляем symlink
                        char latest_path[512];
                        snprintf(latest_path, sizeof(latest_path), "./../../configs/%s/latest.rsc", safe_name);
                        unlink(latest_path);
                        symlink(config_path, latest_path);
                    }
                }
                break;
            default:
                return 1;
        }
        return result;
    }

    // SEND операции - берем последний конфиг из директории устройства
    char link_path[512];
    char find_cmd[1024];
    FILE *fp;


    switch (d->type) {
        case DEVICE_TYPE_ELTEX_ME:
            snprintf(link_path, sizeof(link_path), "./../../configs/%s/latest.config", safe_name);

            // Проверяем существование symlink или файла
            if (resolve_config_path(link_path, real_path, sizeof(real_path)) != 0) {
                // Пробуем найти любой .config файл в директории
                snprintf(find_cmd, sizeof(find_cmd), 
                         "ls -t ./../../configs/%s/*.config 2>/dev/null | head -1", safe_name);
                fp = popen(find_cmd, "r");
                if (fp && fgets(real_path, sizeof(real_path), fp)) {
                    real_path[strcspn(real_path, "\n")] = '\0';
                    pclose(fp);
                } else {
                    if (fp) pclose(fp);
                    fprintf(stderr, "Error: No config file found for device %s\n", d->name);
                    return 1;
                }
            }
            char cp_cmd[2048];
            char rm_cmd[2048];
            snprintf(cp_cmd, sizeof(cp_cmd), "cp %s ~/", real_path);
            system(cp_cmd);

            char config_name[1024];
            char buffer[256];
            sscanf(real_path, "./../../configs/%[^/]/%s", buffer, config_name);


            int result = eltex_me_scp_send(d->username, d->ip, d->password, getlogin(), "192.168.192.53", "0512", config_name);

            snprintf(rm_cmd, sizeof(rm_cmd), "rm ~/%s*.config", safe_name);
            system(rm_cmd);

            return result;
        case DEVICE_TYPE_CISCO_XRV:
            snprintf(link_path, sizeof(link_path), "./../../configs/%s/latest.cfg", safe_name);
            
            // Проверяем существование symlink или файла
            if (resolve_config_path(link_path, real_path, sizeof(real_path)) != 0) {
                // Пробуем найти любой .cfg файл в директории
                snprintf(find_cmd, sizeof(find_cmd), 
                         "ls -t ./../../configs/%s/*.cfg 2>/dev/null | head -1", safe_name);
                fp = popen(find_cmd, "r");
                if (fp && fgets(real_path, sizeof(real_path), fp)) {
                    real_path[strcspn(real_path, "\n")] = '\0';
                    pclose(fp);
                } else {
                    if (fp) pclose(fp);
                    fprintf(stderr, "Error: No config file found for device %s\n", d->name);
                    return 1;
                }
            }
            return put_cisco_config(d->ip, d->username, d->password, real_path);
        case DEVICE_TYPE_CISCO_CSR:
            snprintf(link_path, sizeof(link_path), "./../../configs/%s/latest.cfg", safe_name);
            
            // Проверяем существование symlink или файла
            if (resolve_config_path(link_path, real_path, sizeof(real_path)) != 0) {
                // Пробуем найти любой .cfg файл в директории
                snprintf(find_cmd, sizeof(find_cmd), 
                         "ls -t ./../../configs/%s/*.cfg 2>/dev/null | head -1", safe_name);
                fp = popen(find_cmd, "r");
                if (fp && fgets(real_path, sizeof(real_path), fp)) {
                    real_path[strcspn(real_path, "\n")] = '\0';
                    pclose(fp);
                } else {
                    if (fp) pclose(fp);
                    fprintf(stderr, "Error: No config file found for device %s\n", d->name);
                    return 1;
                }
            }
            return send_cisco_csr_config(d->ip, d->username, d->password, real_path);
        case DEVICE_TYPE_MIKROTIK:
            snprintf(link_path, sizeof(link_path), "./../../configs/%s/latest.rsc", safe_name);
            
            if (resolve_config_path(link_path, real_path, sizeof(real_path)) != 0) {
                // Пробуем найти любой .rsc файл
                snprintf(find_cmd, sizeof(find_cmd), 
                         "ls -t ./../../configs/%s/*.rsc 2>/dev/null | head -1", safe_name);
                fp = popen(find_cmd, "r");
                if (fp && fgets(real_path, sizeof(real_path), fp)) {
                    real_path[strcspn(real_path, "\n")] = '\0';
                    pclose(fp);
                } else {
                    if (fp) pclose(fp);
                    fprintf(stderr, "Error: No config file found for device %s\n", d->name);
                    return 1;
                }
            } else {
            }
            return mikrotik_scp_send(d->username, d->ip, d->password, real_path, "config.rsc");
        case DEVICE_TYPE_JUNIPER:
            // Сначала пробуем latest.conf
            snprintf(link_path, sizeof(link_path), "./../../configs/%s/latest.conf", safe_name);
            
            if (resolve_config_path(link_path, real_path, sizeof(real_path)) != 0) {
                // Пробуем latest.cfg как fallback
                snprintf(link_path, sizeof(link_path), "./../../configs/%s/latest.cfg", safe_name);
                
                if (resolve_config_path(link_path, real_path, sizeof(real_path)) != 0) {
                    // Пробуем найти любой .conf или .cfg файл
                    snprintf(find_cmd, sizeof(find_cmd), 
                             "ls -t ./../../configs/%s/*.conf ./../../configs/%s/*.cfg 2>/dev/null | head -1", 
                             safe_name, safe_name);
                    fp = popen(find_cmd, "r");
                    if (fp && fgets(real_path, sizeof(real_path), fp)) {
                        real_path[strcspn(real_path, "\n")] = '\0';
                        pclose(fp);
                    } else {
                        if (fp) pclose(fp);
                        fprintf(stderr, "Error: No config file found for device %s\n", d->name);
                        return 1;
                    }
                }
            }
            return juniper_vmx_backup_send(d->username, d->ip, d->password, real_path);
        default:
            return 1;
    }
}

static size_t build_menu(ScreenState screen, MenuItem *items, size_t capacity) {
    size_t count = 0;
    size_t i;
    
    if (screen == SCREEN_TOP_LEVEL) {
        snprintf(items[count].label, sizeof(items[count].label), "GET operations");
        items[count++].action = ITEM_NAV_GET;
        snprintf(items[count].label, sizeof(items[count].label), "SEND operations");
        items[count++].action = ITEM_NAV_SEND;
        snprintf(items[count].label, sizeof(items[count].label), "Manage devices");
        items[count++].action = ITEM_NAV_MANAGE;
        return count;
    }

    if (screen == SCREEN_GET_MENU) {
        // Добавляем "Get all devices" пункт
        snprintf(items[count].label, sizeof(items[count].label), ">>> GET ALL DEVICES <<<");
        items[count].device_index = 0;
        items[count++].action = ITEM_RUN_GET_ALL;
        
        // Разделитель (визуальный)
        snprintf(items[count].label, sizeof(items[count].label), "---------------------");
        items[count].device_index = 0;
        items[count++].action = ITEM_BACK;  // Неактивный пункт, просто разделитель
        
        // Список отдельных устройств
        for (i = 0; i < g_store.count && count + 1 < capacity; ++i) {
            snprintf(items[count].label,
                     sizeof(items[count].label),
                     "%s | %s | %s@%s",
                     g_store.devices[i].name,
                     device_type_to_string(g_store.devices[i].type),
                     g_store.devices[i].username,
                     g_store.devices[i].ip);
            items[count].device_index = i;
            items[count++].action = ITEM_RUN_GET_DEVICE;
        }
        
        snprintf(items[count].label, sizeof(items[count].label), "<- Back");
        items[count++].action = ITEM_BACK;
        return count;
    }
    
    if (screen == SCREEN_SEND_MENU) {
        // Добавляем "Send all devices" пункт
        snprintf(items[count].label, sizeof(items[count].label), ">>> SEND ALL DEVICES <<<");
        items[count].device_index = 0;
        items[count++].action = ITEM_RUN_SEND_ALL;
        
        // Разделитель
        snprintf(items[count].label, sizeof(items[count].label), "---------------------");
        items[count].device_index = 0;
        items[count++].action = ITEM_BACK;
        
        // Список отдельных устройств
        for (i = 0; i < g_store.count && count + 1 < capacity; ++i) {
            snprintf(items[count].label,
                     sizeof(items[count].label),
                     "%s | %s | %s@%s",
                     g_store.devices[i].name,
                     device_type_to_string(g_store.devices[i].type),
                     g_store.devices[i].username,
                     g_store.devices[i].ip);
            items[count].device_index = i;
            items[count++].action = ITEM_RUN_SEND_DEVICE;
        }
        
        snprintf(items[count].label, sizeof(items[count].label), "<- Back");
        items[count++].action = ITEM_BACK;
        return count;
    }

    if (screen == SCREEN_MANAGE_MENU) {
        snprintf(items[count].label, sizeof(items[count].label), "Add device");
        items[count++].action = ITEM_ADD_DEVICE;
        snprintf(items[count].label, sizeof(items[count].label), "Edit device");
        items[count++].action = ITEM_EDIT_DEVICE;
        snprintf(items[count].label, sizeof(items[count].label), "Delete device");
        items[count++].action = ITEM_DELETE_DEVICE;
        snprintf(items[count].label, sizeof(items[count].label), "Bulk edit in editor (YAML)");
        items[count++].action = ITEM_BULK_EDIT;
        snprintf(items[count].label, sizeof(items[count].label), "Show device history");
        items[count++].action = ITEM_SHOW_HISTORY;
        snprintf(items[count].label, sizeof(items[count].label), "<- Back");
        items[count++].action = ITEM_BACK;
    }

    return count;
}

static void print_devices_list(void) {
    size_t i;
    printf("\nDevices in store:\n");
    if (g_store.count == 0) {
        printf("  (empty)\n");
    }
    for (i = 0; i < g_store.count; ++i) {
        printf("  %zu) %s | %s | %s@%s\n",
               i + 1,
               g_store.devices[i].name,
               device_type_to_string(g_store.devices[i].type),
               g_store.devices[i].username,
               g_store.devices[i].ip);
    }
}

static int manage_add_device(void) {
    DeviceRecord d;
    char type_buf[32];
    memset(&d, 0, sizeof(d));

    print_devices_list();
    if (prompt_line("Name: ", d.name, sizeof(d.name)) != 0 ||
        prompt_line("Type (cisco-xrv|cisco-csr|mikrotik|juniper): ", type_buf, sizeof(type_buf)) != 0 ||
        device_type_from_string(type_buf, &d.type) != 0 ||
        prompt_line("IP: ", d.ip, sizeof(d.ip)) != 0 ||
        !is_valid_ip(d.ip) ||
        prompt_line("Username: ", d.username, sizeof(d.username)) != 0 ||
        prompt_password("Password: ", d.password, sizeof(d.password)) != 0) {
        return -1;
    }

    if (credential_store_add(&g_store, &d) != 0 ||
        credential_store_save(&g_store, STORE_FILE_PATH, g_master_password) != 0) {
        return -1;
    }
    return 0;
}

static int manage_edit_device(void) {
    char input[32];
    size_t idx;
    DeviceRecord d;

    if (g_store.count == 0) {
        return -1;
    }

    print_devices_list();
    if (prompt_line("Device index to edit: ", input, sizeof(input)) != 0) {
        return -1;
    }
    idx = (size_t)strtoul(input, NULL, 10);
    if (idx == 0 || idx > g_store.count) {
        return -1;
    }
    d = g_store.devices[idx - 1];

    if (prompt_line("New name (blank keep): ", input, sizeof(input)) == 0 && input[0] != '\0') {
        snprintf(d.name, sizeof(d.name), "%s", input);
    }
    if (prompt_line("New ip (blank keep): ", input, sizeof(input)) == 0 && input[0] != '\0') {
        if (!is_valid_ip(input)) {
            return -1;
        }
        snprintf(d.ip, sizeof(d.ip), "%s", input);
    }
    if (prompt_line("New username (blank keep): ", input, sizeof(input)) == 0 && input[0] != '\0') {
        snprintf(d.username, sizeof(d.username), "%s", input);
    }
    if (prompt_password("New password (blank keep): ", input, sizeof(input)) == 0 && input[0] != '\0') {
        snprintf(d.password, sizeof(d.password), "%s", input);
    }
    if (credential_store_update(&g_store, idx - 1, &d) != 0 ||
        credential_store_save(&g_store, STORE_FILE_PATH, g_master_password) != 0) {
        return -1;
    }
    return 0;
}

static int manage_delete_device(void) {
    char input[32];
    size_t idx;
    if (g_store.count == 0) {
        return -1;
    }
    print_devices_list();
    if (prompt_line("Device index to delete: ", input, sizeof(input)) != 0) {
        return -1;
    }
    idx = (size_t)strtoul(input, NULL, 10);
    if (idx == 0 || idx > g_store.count) {
        return -1;
    }
    if (credential_store_delete(&g_store, idx - 1) != 0 ||
        credential_store_save(&g_store, STORE_FILE_PATH, g_master_password) != 0) {
        return -1;
    }
    return 0;
}

static int run_editor_for_file(const char *path) {
    const char *editor = getenv("EDITOR");
    pid_t pid;
    int status;

    if (!editor || editor[0] == '\0') {
        editor = "nano";
    }

    pid = fork();
    if (pid == 0) {
        execlp(editor, editor, path, (char *)0);
        execlp("nano", "nano", path, (char *)0);
        _exit(127);
    }
    if (pid < 0) {
        return -1;
    }
    if (waitpid(pid, &status, 0) < 0) {
        return -1;
    }
    return (WIFEXITED(status) && WEXITSTATUS(status) == 0) ? 0 : -1;
}

static int manage_bulk_edit_devices(char *status, size_t status_size) {
    char *yaml = NULL;
    size_t yaml_len = 0;
    char temp_path[] = "/tmp/configer_devices_edit_XXXXXX";
    int fd;
    FILE *fp;
    long file_len;
    char *edited = NULL;
    CredentialStore updated;
    char errbuf[256];

    if (credential_store_export_yaml(&g_store, &yaml, &yaml_len) != 0) {
        snprintf(status, status_size, "Failed to export devices to YAML");
        return -1;
    }

    fd = mkstemp(temp_path);
    if (fd < 0) {
        free(yaml);
        snprintf(status, status_size, "Failed to create temp editor file: %s", strerror(errno));
        return -1;
    }
    
    fp = fdopen(fd, "w");
    if (!fp) {
        close(fd);
        unlink(temp_path);
        free(yaml);
        snprintf(status, status_size, "Failed to open temp editor file");
        return -1;
    }
    
    fprintf(fp, "# Edit devices in YAML format\n");
    fprintf(fp, "# Format:\n");
    fprintf(fp, "# devices:\n");
    fprintf(fp, "#   - name: \"device-name\"\n");
    fprintf(fp, "#     type: cisco-xrv|cisco-csr|mikrotik|juniper\n");
    fprintf(fp, "#     ip: \"192.168.1.1\"\n");
    fprintf(fp, "#     username: \"user\"\n");
    fprintf(fp, "#     password: \"pass\"\n");
    fprintf(fp, "#\n");
    fwrite(yaml, 1, yaml_len, fp);
    fclose(fp);
    free(yaml);

    if (run_editor_for_file(temp_path) != 0) {
        unlink(temp_path);
        snprintf(status, status_size, "Editor cancelled or failed");
        return -1;
    }

    fp = fopen(temp_path, "rb");
    if (!fp) {
        unlink(temp_path);
        snprintf(status, status_size, "Failed to read edited file");
        return -1;
    }
    
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        unlink(temp_path);
        snprintf(status, status_size, "Failed to read edited file");
        return -1;
    }
    
    file_len = ftell(fp);
    if (file_len < 0 || fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        unlink(temp_path);
        snprintf(status, status_size, "Failed to read edited file");
        return -1;
    }

    edited = (char *)calloc(1, (size_t)file_len + 1);
    if (!edited) {
        fclose(fp);
        unlink(temp_path);
        snprintf(status, status_size, "Out of memory");
        return -1;
    }
    
    if (fread(edited, 1, (size_t)file_len, fp) != (size_t)file_len) {
        free(edited);
        fclose(fp);
        unlink(temp_path);
        snprintf(status, status_size, "Failed to read edited file");
        return -1;
    }
    fclose(fp);
    unlink(temp_path);

    if (credential_store_import_yaml(&updated, edited, errbuf, sizeof(errbuf)) != 0) {
        free(edited);
        snprintf(status, status_size, "Invalid YAML format: %s", errbuf);
        return -1;
    }
    free(edited);

    g_store = updated;
    if (credential_store_save(&g_store, STORE_FILE_PATH, g_master_password) != 0) {
        snprintf(status, status_size, "Failed to save encrypted store");
        return -1;
    }

    snprintf(status, status_size, "Bulk edit applied, devices: %zu", g_store.count);
    return 0;
}

static int prompt_yes_no(const char *prompt) {
    char buf[8];
    if (prompt_line(prompt, buf, sizeof(buf)) != 0) {
        return 0;
    }
    return buf[0] == 'y' || buf[0] == 'Y';
}

static int execute_get_all(void) {
    size_t i;
    int failed_count = 0;
    DeviceActionContext ctx;
    
    printf("\n=== Getting configuration from ALL devices ===\n");
    printf("Total devices: %zu\n\n", g_store.count);
    printf("Configs will be saved to: ./../../configs/<device_name>/\n\n");
    
    for (i = 0; i < g_store.count; ++i) {
        printf("[%zu/%zu] Getting config from: %s (%s@%s) ... ",
               i + 1, g_store.count, 
               g_store.devices[i].name,
               g_store.devices[i].username, 
               g_store.devices[i].ip);
        fflush(stdout);
        
        // Используем execute_device_action для GET операции
        ctx.device = &g_store.devices[i];
        ctx.is_send = 0;  // 0 = GET операция
        
        int result = execute_device_action(&ctx);
        
        if (result == 0) {
            printf("✓ OK\n");
        } else {
            // Получаем детали ошибки из последнего вызова
            printf("✗ FAILED\n");
            failed_count++;
        }
    }
    
    printf("\n=== Summary ===\n");
    printf("Successful: %zu\n", g_store.count - failed_count);
    printf("Failed: %d\n", failed_count);
    printf("\nConfigs stored in: ./../../configs/<device_name>/\n");
    printf("Latest config link: ./../../configs/<device_name>/latest.*\n");
    
    return (failed_count == 0) ? 0 : -1;
}


static int execute_send_all(void) {
    size_t i;
    int failed_count = 0;
    DeviceActionContext ctx;
    
    printf("\n=== Sending configuration to ALL devices ===\n");
    printf("Total devices: %zu\n\n", g_store.count);
    
    for (i = 0; i < g_store.count; ++i) {
        printf("[%zu/%zu] Sending config to: %s (%s@%s) ... ",
               i + 1, g_store.count,
               g_store.devices[i].name,
               g_store.devices[i].username, 
               g_store.devices[i].ip);
        fflush(stdout);
        
        // Используем execute_device_action для SEND операции
        ctx.device = &g_store.devices[i];
        ctx.is_send = 1;  // 1 = SEND операция
        
        int result = execute_device_action(&ctx);
        
        if (result == 0) {
            printf("✓ OK\n");
        } else {
            printf("✗ FAILED\n");
            failed_count++;
        }
    }
    
    printf("\n=== Summary ===\n");
    printf("Successful: %zu\n", g_store.count - failed_count);
    printf("Failed: %d\n", failed_count);
    
    return (failed_count == 0) ? 0 : -1;
}

int main(void) {
    MenuItem items[MENU_MAX_ITEMS];
    size_t item_count;
    size_t selected = 0;
    ScreenState screen = SCREEN_TOP_LEVEL;
    char status[512];
    int ch;

    if (access(STORE_FILE_PATH, F_OK) != 0) {
        char confirm[128];
        if (errno != ENOENT) {
            fprintf(stderr, "Cannot access store file path: %s\n", STORE_FILE_PATH);
            return 1;
        }
        if (prompt_password("Create master password: ", g_master_password, sizeof(g_master_password)) != 0 ||
            prompt_password("Confirm master password: ", confirm, sizeof(confirm)) != 0) {
            fprintf(stderr, "Failed to read master password\n");
            return 1;
        }
        if (strcmp(g_master_password, confirm) != 0) {
            fprintf(stderr, "Master password confirmation mismatch\n");
            return 1;
        }
        memset(confirm, 0, sizeof(confirm));
        memset(&g_store, 0, sizeof(g_store));
        if (credential_store_save(&g_store, STORE_FILE_PATH, g_master_password) != 0) {
            fprintf(stderr, "Failed to create encrypted store file\n");
            return 1;
        }
    } else {
        int attempts;
        int opened = 0;
        for (attempts = 0; attempts < 3; ++attempts) {
            if (prompt_password("Master password: ", g_master_password, sizeof(g_master_password)) != 0) {
                fprintf(stderr, "Failed to read master password\n");
                return 1;
            }
            if (credential_store_open_or_create(&g_store, STORE_FILE_PATH, g_master_password) == 0) {
                opened = 1;
                break;
            }
            fprintf(stderr, "Wrong password or corrupted store (%d/3)\n", attempts + 1);
        }
        if (!opened) {
            if (prompt_yes_no("Reset encrypted store file and create new one? (y/N): ")) {
                char confirm[128];
                if (prompt_password("New master password: ", g_master_password, sizeof(g_master_password)) != 0 ||
                    prompt_password("Confirm new password: ", confirm, sizeof(confirm)) != 0 ||
                    strcmp(g_master_password, confirm) != 0) {
                    fprintf(stderr, "Password setup failed\n");
                    return 1;
                }
                memset(confirm, 0, sizeof(confirm));
                memset(&g_store, 0, sizeof(g_store));
                if (credential_store_save(&g_store, STORE_FILE_PATH, g_master_password) != 0) {
                    fprintf(stderr, "Failed to recreate encrypted store\n");
                    return 1;
                }
            } else {
                fprintf(stderr, "Failed to open encrypted store\n");
                return 1;
            }
        }
    }

    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);

    snprintf(status, sizeof(status), "Store unlocked, devices: %zu", g_store.count);
    item_count = build_menu(screen, items, MENU_MAX_ITEMS);

    while (1) {
        const char *title = "Configer TUI";
        const char *subtitle = "Use arrows/jk, Enter select, b back, q quit";
        DeviceActionContext action_ctx;
        if (screen == SCREEN_GET_MENU) {
            title = "GET operations";
        } else if (screen == SCREEN_SEND_MENU) {
            title = "SEND operations";
        } else if (screen == SCREEN_MANAGE_MENU) {
            title = "Manage devices";
        }
        draw_menu(title, subtitle, items, item_count, selected, status);

        ch = normalize_input_key(getch());
        if (ch == 0) {
            continue;
        }
        if (ch == 'q' || ch == 'Q') {
            break;
        } else if ((ch == KEY_UP || ch == 'k' || ch == 'K') && selected > 0) {
            selected--;
            continue;
        } else if ((ch == KEY_DOWN || ch == 'j' || ch == 'J') && selected + 1 < item_count) {
            selected++;
            continue;
        } else if (ch == 'b' || ch == 'B') {
            if (screen != SCREEN_TOP_LEVEL) {
                screen = SCREEN_TOP_LEVEL;
                item_count = build_menu(screen, items, MENU_MAX_ITEMS);
                selected = 0;
                snprintf(status, sizeof(status), "Back to main menu");
            }
            continue;
        } else if (!(ch == '\n' || ch == KEY_ENTER || ch == 10 || ch == 13)) {
            continue;
        }

        switch (items[selected].action) {
            case ITEM_SHOW_HISTORY:
                if (g_store.count == 0) {
                    snprintf(status, sizeof(status), "No devices to show history");
                    break;
                }
                // Показываем список устройств и выбираем
                endwin();
                print_devices_list();
                printf("\nEnter device index to show history: ");
                char input[32];
                if (prompt_line("", input, sizeof(input)) == 0) {
                    size_t idx = strtoul(input, NULL, 10);
                    if (idx > 0 && idx <= g_store.count) {
                        show_device_history(&g_store.devices[idx - 1]);
                    }
                }
                initscr();
                restore_curses_input_state();
                snprintf(status, sizeof(status), "History viewer closed");
                break;
            case ITEM_RUN_GET_ALL:
                endwin();
                execute_get_all();
                printf("\nPress Enter to continue...");
                getchar();
                initscr();
                restore_curses_input_state();
                snprintf(status, sizeof(status), "Get all devices completed");
                break;
                
            case ITEM_RUN_SEND_ALL:
                endwin();
                execute_send_all();
                printf("\nPress Enter to continue...");
                getchar();
                initscr();
                restore_curses_input_state();
                snprintf(status, sizeof(status), "Send all devices completed");
                break;
            case ITEM_NAV_GET:
                screen = SCREEN_GET_MENU;
                item_count = build_menu(screen, items, MENU_MAX_ITEMS);
                selected = 0;
                snprintf(status, sizeof(status), "Choose device for GET");
                break;
            case ITEM_NAV_SEND:
                screen = SCREEN_SEND_MENU;
                item_count = build_menu(screen, items, MENU_MAX_ITEMS);
                selected = 0;
                snprintf(status, sizeof(status), "Choose device for SEND");
                break;
            case ITEM_NAV_MANAGE:
                screen = SCREEN_MANAGE_MENU;
                item_count = build_menu(screen, items, MENU_MAX_ITEMS);
                selected = 0;
                snprintf(status, sizeof(status), "Manage encrypted device list");
                break;
            case ITEM_BACK:
                screen = SCREEN_TOP_LEVEL;
                item_count = build_menu(screen, items, MENU_MAX_ITEMS);
                selected = 0;
                snprintf(status, sizeof(status), "Back to main menu");
                break;
            case ITEM_RUN_GET_DEVICE:
            case ITEM_RUN_SEND_DEVICE:
                if (items[selected].device_index >= g_store.count) {
                    snprintf(status, sizeof(status), "Invalid device index");
                    break;
                }
                action_ctx.device = &g_store.devices[items[selected].device_index];
                action_ctx.is_send = (items[selected].action == ITEM_RUN_SEND_DEVICE);
                snprintf(status, sizeof(status), "Running...");
                draw_menu(title, subtitle, items, item_count, selected, status);
                run_action_capture_stderr(execute_device_action, &action_ctx, status, sizeof(status));
                restore_curses_input_state();
                break;
            case ITEM_ADD_DEVICE:
                endwin();
                if (manage_add_device() == 0) {
                    snprintf(status, sizeof(status), "Device added");
                } else {
                    snprintf(status, sizeof(status), "Failed to add device");
                }
                initscr();
                restore_curses_input_state();
                item_count = build_menu(screen, items, MENU_MAX_ITEMS);
                selected = 0;
                break;
            case ITEM_EDIT_DEVICE:
                endwin();
                if (manage_edit_device() == 0) {
                    snprintf(status, sizeof(status), "Device updated");
                } else {
                    snprintf(status, sizeof(status), "Failed to edit device");
                }
                initscr();
                restore_curses_input_state();
                item_count = build_menu(screen, items, MENU_MAX_ITEMS);
                selected = 0;
                break;
            case ITEM_DELETE_DEVICE:
                endwin();
                if (manage_delete_device() == 0) {
                    snprintf(status, sizeof(status), "Device deleted");
                } else {
                    snprintf(status, sizeof(status), "Failed to delete device");
                }
                initscr();
                restore_curses_input_state();
                item_count = build_menu(screen, items, MENU_MAX_ITEMS);
                selected = 0;
                break;
            case ITEM_BULK_EDIT:
                endwin();
                manage_bulk_edit_devices(status, sizeof(status));
                initscr();
                restore_curses_input_state();
                item_count = build_menu(screen, items, MENU_MAX_ITEMS);
                selected = 0;
                break;
        }
    }

    endwin();
    memset(g_master_password, 0, sizeof(g_master_password));
    return 0;
}