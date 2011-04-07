/* 
 * File:   port_operations.h
 * Author: atom
 *
 * Created on 11 Январь 2011 г., 20:09
 */
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <syslog.h>
#include <fcntl.h>
#include <iconv.h>
#include <termios.h>
#include <signal.h>

#ifndef _PORT_OPERATIONS_H
#define	_PORT_OPERATIONS_H

#ifdef	__cplusplus
extern "C" {
#endif

int init_port(char * devname);
int write_data(int port, char *buf);
unsigned int getPaperStatus(int port);
int close_port(int port);


#ifdef	__cplusplus
}
#endif

#endif	/* _PORT_OPERATIONS_H */

