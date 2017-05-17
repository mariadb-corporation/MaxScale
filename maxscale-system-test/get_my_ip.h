#ifndef GET_MY_IP_H
#define GET_MY_IP_H

/**
 * @brief get_my_ip Get IP address of machine where this code is executed as it is visible from remote machine
 * Connects to DNS port 53 of remote machine and gets own IP from socket info
 * @param remote_ip IP of remote machine
 * @param my_ip Pointer to result (own IP string)
 * @return 0 in case of success
 */
int get_my_ip(char * remote_ip, char *my_ip );

#endif // GET_MY_IP_H
