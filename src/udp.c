#include "udp.h"
#include <coreinit/memdefaultheap.h>
#include <coreinit/thread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

static int udp_socket = -1;
static volatile bool udp_lock = false;

/**
 * Initialize the UDP socket.
 * @param ipString The IP address to connect to.
 * @param ipport The port to connect to.
 */
void udp_init(const char * ipString, unsigned short ipport)
{
    udp_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if(udp_socket < 0) {
        return;
    }

    struct sockaddr_in connect_addr;
    memset(&connect_addr, 0, sizeof(connect_addr));
    connect_addr.sin_family = AF_INET;
    connect_addr.sin_port = htons(ipport);  // Asegúrate de convertir el puerto correctamente
    inet_aton(ipString, &connect_addr.sin_addr);

    if(connect(udp_socket, (struct sockaddr*)&connect_addr, sizeof(connect_addr)) < 0)
    {
        close(udp_socket);
        udp_socket = -1;
    }
}

/**
 * Deinitialize the UDP socket.
 */
void udp_deinit(void)
{
    if(udp_socket >= 0)
    {
        close(udp_socket);
        udp_socket = -1;
    }
}

/**
 * Print a string to the UDP socket.
 * @param str The string to send.
 */
void udp_print(const char *str)
{
    if(udp_socket < 0) {
        return;
    }

    while(udp_lock == true) {
        OSSleepTicks(OSMillisecondsToTicks(1));
    }
    udp_lock = true;

    int len = strlen(str);
    while (len > 0) {
        const int block = len < 1400 ? len : 1400; // take max 1400 bytes per UDP packet
        const int ret = send(udp_socket, str, block, 0);
        if(ret < 0) {
            break;
        }

        len -= ret;
        str += ret;
    }

    udp_lock = false;
}

/**
 * Receive a string from the UDP socket.
 * @param buffer The buffer to store the received string.
 * @param max_len The maximum length of the buffer.
 * @return The number of bytes received, or -1 if an error occurred.
 */
int udp_receive(char *buffer, int max_len)
{
    if (udp_socket < 0) {
        return -1;
    }

    struct sockaddr_in sender_addr;
    socklen_t addr_len = sizeof(sender_addr);
    int received_bytes = recvfrom(udp_socket, buffer, max_len, 0, (struct sockaddr*)&sender_addr, &addr_len);

    if (received_bytes < 0) {
        // Error receiving data
        perror("Error receiving UDP packet");
        return -1;
    }

    // Ensure the received data is null-terminated to prevent buffer overflows
    if (received_bytes < max_len) {
        buffer[received_bytes] = '\0';
    } else {
        buffer[max_len - 1] = '\0';
    }

    return received_bytes;
}
