#ifndef NETWORK_OPS_H
#define NETWORK_OPS_H

typedef struct {
    const char *ip;
    const char *username;
    const char *password;
} DeviceConnection;

int get_cisco_config(const char *host, const char *user, const char *pass);
int put_cisco_config(const char *host, const char *user, const char *pass, const char *config_file);

int get_cisco_csr_config(const char *host, const char *user, const char *pass);
int send_cisco_csr_config(const char *host, const char *user, const char *pass, const char *config_file);

int mikrotik_scp_get(const char *mikrotik_user,
                     const char *mikrotik_host,
                     const char *mikrotik_password,
                     const char *remote_path,
                     const char *local_path);

int mikrotik_scp_send(const char *mikrotik_user,
                      const char *mikrotik_host,
                      const char *mikrotik_password,
                      const char *local_path,
                      const char *remote_path);

int juniper_vmx_backup_get(const char *juniper_user,
                           const char *juniper_host,
                           const char *juniper_password,
                           const char *local_path);

int juniper_vmx_backup_send(const char *juniper_user,
                            const char *juniper_host,
                            const char *juniper_password,
                            const char *local_path);

int eltex_me_scp_get(const char *hostname,
                     const char *user,
                     const char *ip,
                     const char *pass,
                     const char *local_user,
                     const char *local_ip,
                     const char *local_pass,
                     const char *config_path);

int eltex_me_scp_send(const char *user,
                      const char *ip,
                      const char *pass,
                      const char *local_user,
                      const char *local_ip,
                      const char *local_pass,
                      const char *config_path);

#endif
