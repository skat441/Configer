#ifndef CREDENTIAL_STORE_H
#define CREDENTIAL_STORE_H

#include <stddef.h>
#include <stdint.h>

#define STORE_MAX_DEVICES 256
#define STORE_MAX_NAME_LEN 128
#define STORE_MAX_IP_LEN 64
#define STORE_MAX_USERNAME_LEN 128
#define STORE_MAX_PASSWORD_LEN 256

typedef enum {
    DEVICE_TYPE_CISCO,
    DEVICE_TYPE_MIKROTIK,
    DEVICE_TYPE_JUNIPER
} DeviceType;

typedef struct {
    char name[STORE_MAX_NAME_LEN];
    DeviceType type;
    char ip[STORE_MAX_IP_LEN];
    char username[STORE_MAX_USERNAME_LEN];
    char password[STORE_MAX_PASSWORD_LEN];
} DeviceRecord;

typedef struct {
    DeviceRecord devices[STORE_MAX_DEVICES];
    size_t count;
} CredentialStore;

// Основные функции
const char *device_type_to_string(DeviceType type);
int device_type_from_string(const char *value, DeviceType *out_type);

// Сохранение и загрузка (зашифрованные)
int credential_store_save(const CredentialStore *store, const char *path, const char *master_password);
int credential_store_open_or_create(CredentialStore *store, const char *path, const char *master_password);

// Управление устройствами
int credential_store_add(CredentialStore *store, const DeviceRecord *record);
int credential_store_update(CredentialStore *store, size_t index, const DeviceRecord *record);
int credential_store_delete(CredentialStore *store, size_t index);

// YAML экспорт/импорт
int credential_store_export_yaml(const CredentialStore *store, char **out_yaml, size_t *out_len);
int credential_store_import_yaml(CredentialStore *store, const char *yaml, char *error_buf, size_t error_buf_size);

#endif