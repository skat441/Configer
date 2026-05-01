#include <stdio.h>
#include <stdlib.h>

int main() {
    // Отправка одной команды
    system("sshpass -p 'password' ssh -o HostKeyAlgorithms=+ssh-rsa user@192.168.0.111 sh run");

    return 0;
}