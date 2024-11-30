#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* A ripoff of logger.h */

/**
 * Initialize the UDP socket.
 * @param ip The IP address to connect to.
 * @param ipport The port to connect to.
 */
void udp_init(const char * ip, unsigned short ipport);

/**
 * Deinitialize the UDP socket.
 */
void udp_deinit(void);

/**
 * Print a string to the UDP socket.
 * @param str The string to send.
 */
void udp_print(const char *str);

/**
 * Receive a string from the UDP socket.
 * @param buffer The buffer to store the received string.
 * @param max_len The maximum length of the buffer.
 * @return The number of bytes received, or -1 if an error occurred.
 */
int udp_receive(char *buffer, int max_len);

#ifdef __cplusplus
}
#endif
