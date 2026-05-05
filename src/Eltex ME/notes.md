# ##ELTEX ME

#Скачать конфигу

Зайти по ssh, сделать команду: copy fs://running-config scp://user@host/ vrf mgmt-intf - скачать текующую конфигу с устройства

#Залить конфигу

copy scp://kirill@192.168.192.53/L4-AR1-20260504_053548-running_config fs://candidate-config vrf mgmt-intf - отправить конфигу на устройство в качестве временной конфиги

commit replace - подменить running-config на candidate-config
