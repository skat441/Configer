#ifndef CREDENTIAL_STORE_H
#define CREDENTIAL_STORE_H

#include <stddef.h>

#define STORE_MAX_DEVICES 64

typedef enum {
    DEVICE_TYPE_CISCO = 0,
    DEVICE_TYPE_MIKROTIK,
    DEVICE_TYPE_JUNIPER
} DeviceType;

typedef struct {
    char name[64];
    DeviceType type;
    char ip[64];
    char username[64];
    char password[128];
} DeviceRecord;

typedef struct {
    DeviceRecord devices[STORE_MAX_DEVICES];
    size_t count;
} CredentialStore;

int credential_store_open_or_create(CredentialStore *store,
                                    const char *path,
                                    const char *master_password);
int credential_store_save(const CredentialStore *store,
                          const char *path,
                          const char *master_password);
int credential_store_add(CredentialStore *store, const DeviceRecord *record);
int credential_store_update(CredentialStore *store, size_t index, const DeviceRecord *record);
int credential_store_delete(CredentialStore *store, size_t index);
int credential_store_export_json(const CredentialStore *store, char **out_json, size_t *out_len);
int credential_store_import_json(CredentialStore *store,
                                 const char *json,
                                 char *error_buf,
                                 size_t error_buf_size);

const char *device_type_to_string(DeviceType type);
int device_type_from_string(const char *value, DeviceType *out_type);

#endif
