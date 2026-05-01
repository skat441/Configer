#define _POSIX_C_SOURCE 200809L
#include "credential_store.h"
#include <errno.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define STORE_MAGIC "CFGS1"
#define STORE_MAGIC_SIZE 5
#define STORE_VERSION 1
#define STORE_SALT_SIZE 16
#define STORE_NONCE_SIZE 12
#define STORE_TAG_SIZE 16
#define STORE_KEY_SIZE 32
#define STORE_PBKDF2_ITERATIONS 200000

typedef struct {
    char magic[STORE_MAGIC_SIZE];
    uint8_t version;
    uint8_t salt[STORE_SALT_SIZE];
    uint8_t nonce[STORE_NONCE_SIZE];
    uint32_t iterations;
    uint32_t ciphertext_len;
} StoreHeader;

static uint32_t to_be32(uint32_t value) {
    return ((value & 0x000000FFu) << 24) |
           ((value & 0x0000FF00u) << 8) |
           ((value & 0x00FF0000u) >> 8) |
           ((value & 0xFF000000u) >> 24);
}

static uint32_t from_be32(uint32_t value) {
    return to_be32(value);
}

const char *device_type_to_string(DeviceType type) {
    switch (type) {
        case DEVICE_TYPE_CISCO: return "cisco";
        case DEVICE_TYPE_MIKROTIK: return "mikrotik";
        case DEVICE_TYPE_JUNIPER: return "juniper";
        default: return "unknown";
    }
}

int device_type_from_string(const char *value, DeviceType *out_type) {
    if (strcmp(value, "cisco") == 0) {
        *out_type = DEVICE_TYPE_CISCO;
        return 0;
    }
    if (strcmp(value, "mikrotik") == 0) {
        *out_type = DEVICE_TYPE_MIKROTIK;
        return 0;
    }
    if (strcmp(value, "juniper") == 0) {
        *out_type = DEVICE_TYPE_JUNIPER;
        return 0;
    }
    return -1;
}

static void secure_bzero(void *ptr, size_t len) {
    volatile unsigned char *p = (volatile unsigned char *)ptr;
    while (len--) {
        *p++ = 0;
    }
}

static void json_escape_append(char *dst, size_t dst_size, size_t *used, const char *src) {
    size_t i;
    for (i = 0; src[i] != '\0' && *used + 2 < dst_size; ++i) {
        if (src[i] == '"' || src[i] == '\\') {
            dst[(*used)++] = '\\';
            dst[(*used)++] = src[i];
        } else {
            dst[(*used)++] = src[i];
        }
    }
    dst[*used] = '\0';
}

static int serialize_store_json(const CredentialStore *store, char **out, size_t *out_len) {
    size_t i;
    size_t used = 0;
    size_t cap = 32768;
    char *buf = (char *)calloc(1, cap);
    if (!buf) {
        return -1;
    }

    used += (size_t)snprintf(buf + used, cap - used, "{\"devices\":[");
    for (i = 0; i < store->count; ++i) {
        const DeviceRecord *d = &store->devices[i];
        char name[256] = {0};
        char ip[256] = {0};
        char user[256] = {0};
        char pass[512] = {0};
        size_t n_used = 0;
        size_t ip_used = 0;
        size_t u_used = 0;
        size_t p_used = 0;
        json_escape_append(name, sizeof(name), &n_used, d->name);
        json_escape_append(ip, sizeof(ip), &ip_used, d->ip);
        json_escape_append(user, sizeof(user), &u_used, d->username);
        json_escape_append(pass, sizeof(pass), &p_used, d->password);
        used += (size_t)snprintf(buf + used, cap - used,
                                 "%s{\"name\":\"%s\",\"type\":\"%s\",\"ip\":\"%s\",\"username\":\"%s\",\"password\":\"%s\"}",
                                 (i == 0) ? "" : ",",
                                 name,
                                 device_type_to_string(d->type),
                                 ip,
                                 user,
                                 pass);
        if (used >= cap - 64) {
            free(buf);
            return -1;
        }
    }
    used += (size_t)snprintf(buf + used, cap - used, "]}");
    *out = buf;
    *out_len = used;
    return 0;
}

static int json_extract_value(const char *start, const char *key, char *out, size_t out_size) {
    const char *p = strstr(start, key);
    size_t idx = 0;
    if (!p) {
        return -1;
    }
    p += strlen(key);
    while (*p && *p != '"') {
        if (*p == '\\' && p[1] != '\0') {
            p++;
        }
        if (idx + 1 >= out_size) {
            return -1;
        }
        out[idx++] = *p++;
    }
    out[idx] = '\0';
    return (*p == '"') ? 0 : -1;
}

static int deserialize_store_json(CredentialStore *store, const char *json) {
    const char *p = json;
    store->count = 0;

    while ((p = strstr(p, "{\"name\":\"")) != NULL) {
        DeviceRecord d;
        char type[32];
        memset(&d, 0, sizeof(d));
        memset(type, 0, sizeof(type));

        if (json_extract_value(p, "{\"name\":\"", d.name, sizeof(d.name)) != 0 ||
            json_extract_value(p, "\"type\":\"", type, sizeof(type)) != 0 ||
            json_extract_value(p, "\"ip\":\"", d.ip, sizeof(d.ip)) != 0 ||
            json_extract_value(p, "\"username\":\"", d.username, sizeof(d.username)) != 0 ||
            json_extract_value(p, "\"password\":\"", d.password, sizeof(d.password)) != 0 ||
            device_type_from_string(type, &d.type) != 0) {
            return -1;
        }

        if (store->count >= STORE_MAX_DEVICES) {
            return -1;
        }
        store->devices[store->count++] = d;
        p += 9;
    }
    return 0;
}

static int is_valid_ipv4(const char *ip) {
    int dots = 0;
    int value = 0;
    int digit_count = 0;
    size_t i;

    if (!ip || ip[0] == '\0') {
        return 0;
    }

    for (i = 0; ; ++i) {
        char c = ip[i];
        if (c >= '0' && c <= '9') {
            value = value * 10 + (c - '0');
            digit_count++;
            if (value > 255 || digit_count > 3) {
                return 0;
            }
        } else if (c == '.' || c == '\0') {
            if (digit_count == 0) {
                return 0;
            }
            if (c == '.') {
                dots++;
                if (dots > 3) {
                    return 0;
                }
            } else {
                break;
            }
            value = 0;
            digit_count = 0;
        } else {
            return 0;
        }
    }

    return dots == 3;
}

static int validate_store(const CredentialStore *store, char *error_buf, size_t error_buf_size) {
    size_t i;
    size_t j;

    if (!error_buf || error_buf_size == 0) {
        static char sink[8];
        error_buf = sink;
        error_buf_size = sizeof(sink);
    }

    if (store->count > STORE_MAX_DEVICES) {
        snprintf(error_buf, error_buf_size, "too many devices");
        return -1;
    }

    for (i = 0; i < store->count; ++i) {
        const DeviceRecord *d = &store->devices[i];
        if (d->name[0] == '\0' || d->ip[0] == '\0' || d->username[0] == '\0' || d->password[0] == '\0') {
            snprintf(error_buf, error_buf_size, "device %zu has empty required fields", i + 1);
            return -1;
        }
        if (!is_valid_ipv4(d->ip)) {
            snprintf(error_buf, error_buf_size, "device %zu has invalid IPv4 address", i + 1);
            return -1;
        }
        for (j = i + 1; j < store->count; ++j) {
            if (strcmp(store->devices[i].name, store->devices[j].name) == 0) {
                snprintf(error_buf, error_buf_size, "duplicate device name: %s", store->devices[i].name);
                return -1;
            }
            if (strcmp(store->devices[i].ip, store->devices[j].ip) == 0) {
                snprintf(error_buf, error_buf_size, "duplicate device ip: %s", store->devices[i].ip);
                return -1;
            }
        }
    }

    return 0;
}

static int derive_key(const char *password,
                      const uint8_t salt[STORE_SALT_SIZE],
                      uint32_t iterations,
                      uint8_t key[STORE_KEY_SIZE]) {
    if (!PKCS5_PBKDF2_HMAC(password,
                           (int)strlen(password),
                           salt,
                           STORE_SALT_SIZE,
                           (int)iterations,
                           EVP_sha256(),
                           STORE_KEY_SIZE,
                           key)) {
        return -1;
    }
    return 0;
}

static int encrypt_payload(const uint8_t *plaintext,
                           int plaintext_len,
                           const uint8_t key[STORE_KEY_SIZE],
                           const uint8_t nonce[STORE_NONCE_SIZE],
                           uint8_t **out_ciphertext,
                           int *out_ciphertext_len,
                           uint8_t out_tag[STORE_TAG_SIZE]) {
    EVP_CIPHER_CTX *ctx;
    uint8_t *ciphertext;
    int out_len = 0;
    int final_len = 0;

    ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        return -1;
    }
    ciphertext = (uint8_t *)malloc((size_t)plaintext_len + STORE_TAG_SIZE);
    if (!ciphertext) {
        EVP_CIPHER_CTX_free(ctx);
        return -1;
    }

    if (EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, NULL, NULL) != 1 ||
        EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, STORE_NONCE_SIZE, NULL) != 1 ||
        EVP_EncryptInit_ex(ctx, NULL, NULL, key, nonce) != 1 ||
        EVP_EncryptUpdate(ctx, ciphertext, &out_len, plaintext, plaintext_len) != 1 ||
        EVP_EncryptFinal_ex(ctx, ciphertext + out_len, &final_len) != 1 ||
        EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, STORE_TAG_SIZE, out_tag) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        free(ciphertext);
        return -1;
    }

    *out_ciphertext = ciphertext;
    *out_ciphertext_len = out_len + final_len;
    EVP_CIPHER_CTX_free(ctx);
    return 0;
}

static int decrypt_payload(const uint8_t *ciphertext,
                           int ciphertext_len,
                           const uint8_t key[STORE_KEY_SIZE],
                           const uint8_t nonce[STORE_NONCE_SIZE],
                           const uint8_t tag[STORE_TAG_SIZE],
                           uint8_t **out_plaintext,
                           int *out_plaintext_len) {
    EVP_CIPHER_CTX *ctx;
    uint8_t *plaintext;
    int out_len = 0;
    int final_len = 0;

    ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        return -1;
    }
    plaintext = (uint8_t *)calloc(1, (size_t)ciphertext_len + 1);
    if (!plaintext) {
        EVP_CIPHER_CTX_free(ctx);
        return -1;
    }

    if (EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, NULL, NULL) != 1 ||
        EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, STORE_NONCE_SIZE, NULL) != 1 ||
        EVP_DecryptInit_ex(ctx, NULL, NULL, key, nonce) != 1 ||
        EVP_DecryptUpdate(ctx, plaintext, &out_len, ciphertext, ciphertext_len) != 1 ||
        EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, STORE_TAG_SIZE, (void *)tag) != 1 ||
        EVP_DecryptFinal_ex(ctx, plaintext + out_len, &final_len) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        free(plaintext);
        return -1;
    }

    *out_plaintext = plaintext;
    *out_plaintext_len = out_len + final_len;
    EVP_CIPHER_CTX_free(ctx);
    return 0;
}

int credential_store_save(const CredentialStore *store,
                          const char *path,
                          const char *master_password) {
    StoreHeader header;
    uint8_t key[STORE_KEY_SIZE];
    uint8_t *ciphertext = NULL;
    int ciphertext_len = 0;
    uint8_t tag[STORE_TAG_SIZE];
    char *plaintext = NULL;
    size_t plaintext_len = 0;
    char temp_path[512];
    FILE *fp = NULL;

    memset(&header, 0, sizeof(header));
    memcpy(header.magic, STORE_MAGIC, STORE_MAGIC_SIZE);
    header.version = STORE_VERSION;
    header.iterations = to_be32(STORE_PBKDF2_ITERATIONS);

    if (RAND_bytes(header.salt, STORE_SALT_SIZE) != 1 ||
        RAND_bytes(header.nonce, STORE_NONCE_SIZE) != 1 ||
        serialize_store_json(store, &plaintext, &plaintext_len) != 0 ||
        derive_key(master_password, header.salt, STORE_PBKDF2_ITERATIONS, key) != 0 ||
        encrypt_payload((const uint8_t *)plaintext, (int)plaintext_len, key, header.nonce,
                        &ciphertext, &ciphertext_len, tag) != 0) {
        secure_bzero(key, sizeof(key));
        free(plaintext);
        free(ciphertext);
        return -1;
    }

    header.ciphertext_len = to_be32((uint32_t)ciphertext_len);
    snprintf(temp_path, sizeof(temp_path), "%s.tmp", path);
    fp = fopen(temp_path, "wb");
    if (!fp) {
        secure_bzero(key, sizeof(key));
        free(plaintext);
        free(ciphertext);
        return -1;
    }

    if (fwrite(&header, sizeof(header), 1, fp) != 1 ||
        fwrite(ciphertext, (size_t)ciphertext_len, 1, fp) != 1 ||
        fwrite(tag, STORE_TAG_SIZE, 1, fp) != 1 ||
        fflush(fp) != 0 ||
        fsync(fileno(fp)) != 0 ||
        fclose(fp) != 0 ||
        rename(temp_path, path) != 0) {
        fclose(fp);
        unlink(temp_path);
        secure_bzero(key, sizeof(key));
        free(plaintext);
        free(ciphertext);
        return -1;
    }

    secure_bzero(key, sizeof(key));
    free(plaintext);
    free(ciphertext);
    return 0;
}

int credential_store_open_or_create(CredentialStore *store,
                                    const char *path,
                                    const char *master_password) {
    FILE *fp;
    StoreHeader header;
    uint8_t key[STORE_KEY_SIZE];
    uint8_t *ciphertext = NULL;
    uint8_t tag[STORE_TAG_SIZE];
    uint8_t *plaintext = NULL;
    int plaintext_len = 0;
    uint32_t ciphertext_len;
    uint32_t iterations;

    memset(store, 0, sizeof(*store));
    fp = fopen(path, "rb");
    if (!fp) {
        if (errno == ENOENT) {
            return credential_store_save(store, path, master_password);
        }
        return -1;
    }

    if (fread(&header, sizeof(header), 1, fp) != 1 ||
        memcmp(header.magic, STORE_MAGIC, STORE_MAGIC_SIZE) != 0 ||
        header.version != STORE_VERSION) {
        fclose(fp);
        return -1;
    }

    ciphertext_len = from_be32(header.ciphertext_len);
    iterations = from_be32(header.iterations);
    if (ciphertext_len == 0 || ciphertext_len > 1024 * 1024 || iterations < 50000) {
        fclose(fp);
        return -1;
    }

    ciphertext = (uint8_t *)malloc(ciphertext_len);
    if (!ciphertext) {
        fclose(fp);
        return -1;
    }

    if (fread(ciphertext, ciphertext_len, 1, fp) != 1 ||
        fread(tag, STORE_TAG_SIZE, 1, fp) != 1 ||
        fclose(fp) != 0 ||
        derive_key(master_password, header.salt, iterations, key) != 0 ||
        decrypt_payload(ciphertext, (int)ciphertext_len, key, header.nonce, tag, &plaintext, &plaintext_len) != 0) {
        secure_bzero(key, sizeof(key));
        free(ciphertext);
        free(plaintext);
        return -1;
    }

    plaintext[plaintext_len] = '\0';
    if (deserialize_store_json(store, (const char *)plaintext) != 0 ||
        validate_store(store, NULL, 0) != 0) {
        secure_bzero(key, sizeof(key));
        free(ciphertext);
        free(plaintext);
        return -1;
    }

    secure_bzero(key, sizeof(key));
    free(ciphertext);
    secure_bzero(plaintext, (size_t)plaintext_len);
    free(plaintext);
    return 0;
}

int credential_store_add(CredentialStore *store, const DeviceRecord *record) {
    if (store->count >= STORE_MAX_DEVICES) {
        return -1;
    }
    store->devices[store->count++] = *record;
    return 0;
}

int credential_store_update(CredentialStore *store, size_t index, const DeviceRecord *record) {
    if (index >= store->count) {
        return -1;
    }
    store->devices[index] = *record;
    return 0;
}

int credential_store_delete(CredentialStore *store, size_t index) {
    size_t i;
    if (index >= store->count) {
        return -1;
    }
    for (i = index; i + 1 < store->count; ++i) {
        store->devices[i] = store->devices[i + 1];
    }
    store->count--;
    return 0;
}

int credential_store_export_json(const CredentialStore *store, char **out_json, size_t *out_len) {
    return serialize_store_json(store, out_json, out_len);
}

int credential_store_import_json(CredentialStore *store,
                                 const char *json,
                                 char *error_buf,
                                 size_t error_buf_size) {
    CredentialStore parsed;
    if (deserialize_store_json(&parsed, json) != 0) {
        if (error_buf && error_buf_size > 0) {
            snprintf(error_buf, error_buf_size, "invalid JSON structure");
        }
        return -1;
    }
    if (validate_store(&parsed, error_buf, error_buf_size) != 0) {
        return -1;
    }
    *store = parsed;
    return 0;
}
