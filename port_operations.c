#include "port_operations.h"
#include "const_defs.h"


int wait_flag = TRUE;

int init_port(char * devname){
    int fd;
    struct termios oldtio, newtio;
    struct sigaction saio; //definition of signal action
    void signal_handler_IO(int status);

    fd = open(devname, O_RDWR | O_NOCTTY | O_NONBLOCK);

    if (fd < 0) {
        syslog(LOG_ALERT, "Err in open(): %s", strerror(errno));
        closelog();
        exit(0);
    }

    saio.sa_handler = signal_handler_IO;
    sigemptyset(&saio.sa_mask); //saio.sa_mask = 0;
    saio.sa_flags = 0;
    saio.sa_restorer = NULL;
    sigaction(SIGIO, &saio, NULL);

    // allow the process to receive SIGIO
    fcntl(fd, F_SETOWN, getpid());
    // Make the file descriptor asynchronous (the manual page says only
    // O_APPEND and O_NONBLOCK, will work with F_SETFL...)
    fcntl(fd, F_SETFL, FASYNC);

    tcgetattr(fd, &oldtio);

    newtio.c_cflag = B9600 | CRTSCTS | CS8 | 0 | 0 | 0 | CLOCAL | CREAD;
    newtio.c_iflag = IGNPAR;
    newtio.c_oflag = 0;
    newtio.c_lflag = 0; //ICANON;
    newtio.c_cc[VMIN] = 1;
    newtio.c_cc[VTIME] = 0;
    tcflush(fd, TCIFLUSH);
    tcsetattr(fd, TCSANOW, &newtio);

    return fd;
}

int write_data(int port, char *buf) {
    iconv_t cd;
    size_t k, f, t;
    char *iconv_from = "CP1251//IGNORE";
    char *iconv_to = "UTF-8";
    char *test_msg; 
    char buf1[MAXANSWERSIZE];
    char init[2] = {0x1B, 0x40}; //ініціалізація принтеру
    char hiq[3] = {0x1B, 0x21, 0x01}; //вибір шрифту
    char total_cut[2] = {0x1b, 0x69}; //відріз паперу
    char* out = buf1;
    char* msg = buf;

    f = strlen(buf);
    t = sizeof(buf1);
    cd = iconv_open(iconv_from, iconv_to);
    k = iconv(cd, &msg, &f, &out, &t);
    write(port, init, sizeof(init));
    write(port, hiq, sizeof(hiq));
    write(port, buf1, strlen(buf1));
    write(port, total_cut, sizeof(total_cut));
    int i;
    for (i = 0; i < MAXANSWERSIZE; i++)
        buf1[i] = 0;
    iconv_close(cd);
}

unsigned int getPaperStatus(int port) {
    unsigned char getPrinterStatus[2] = {0x1B, 0x76}; //отримати стан принтера
    unsigned char init[2] = {0x1B, 0x40}; //ініціалізація
    unsigned char *printerStatus = '0';
    write(port, init, strlen(init));
    write(port, getPrinterStatus, strlen(getPrinterStatus));
    read(port, &printerStatus, 1);
    return (unsigned int) printerStatus;
}

int close_port(int port){
    close(port);
}

void signal_handler_IO(int status) {
    wait_flag = FALSE;
}