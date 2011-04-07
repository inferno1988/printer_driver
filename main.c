/* 
 * File:   main.c
 * Author: atom
 *
 * Created on December 28, 2010, 9:59 AM
 */

#include <sys/syslog.h>


#include <stdio.h>

#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>

#include "http_server.h"
#include "const_defs.h"

int main(int argc, char *argv[]) {
    int rez = 0, is_daemon = 0, bind_port = 0;
    struct MHD_Daemon *daemon1;
    init_post_mem();
    dev_name = "/dev/usblp0";
    param_host_name = "127.0.0.1";
    chdir("/");
    fclose(stdin);
    fclose(stderr);

     while ((rez = getopt(argc, argv, "p:h:do:")) != -1) {
        switch (rez) {
            case 'p': bind_port = atoi(optarg);
                break;
            case 'h': param_host_name = optarg;
                break;
            case 'd': is_daemon = 1;
                break;
            case 'o': dev_name = optarg;
                break;
            case '?': exit(0);
                break;
        };
    };

    if (argc == 1) {
        printf("\
Custom printer daemon v0.2-rc1\n\
No options entered! \n\
Usage:\n\
printerd -d \"Start as daemon with default settings\"\n\
printerd -p 3426 -h 127.0.0.1 -o /dev/usblp0 -d \"Start as daemon with opts\"\n\
-p  Port\n\
-h Host\n\
-o Device\n\
-d Start as daemon\n\
Palamarchuk Maxim (gofl@meta.ua)   ҉ ©2011 \n");
        exit(0);
    }

    if (is_daemon) {
        if (fork()) {
            syslog(LOG_ALERT, "printerd can't fork. Err: %s", strerror(errno));
            closelog();
            exit(0);
        }
        fclose(stdout);
        setsid();
    }

    daemon1 = http_start(bind_port, param_host_name);

    while (TRUE) {
        sleep(60);
    }

    http_stop(daemon1);
}
