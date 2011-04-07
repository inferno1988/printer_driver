
/*
serial printer daemon ( Hwasung system co ltd HMK-080 s/n 08060467/n )
(c) Alexander Kubrack, 2009, 2010
Ì¦ÃÅÎÚ¦Ñ âúä

Date:        $Date: 2010/04/27 15:05:56 $
Revision:    $Revision: 1.11 $
Id:          $Id: sprnd.c,v 1.11 2010/04/27 15:05:56 kubrack Exp $

*/


/*
stty < /dev/cuau1.init 19200
cat h.txt | iconv -f koi8-u -t cp866 > /dev/cuau1
*/

#include <sys/wait.h>
#include <sys/resource.h>
#include <iconv.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <strings.h>
#include <string.h>
#include <syslog.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "ak_httpd.h"

/* exit status */
#define FATAL_STATUS		33	/* for no restart main daemon */
#define PRE_FORK_FATAL_STATUS	34
#define FDOPEN_FAIL_STATUS	35	/* retrieble */
#define LISTEN_FAIL_STATUS	35	/* retrieble */

#define COMFD int
#define NETFD int
#define FD_SET_T fd_set
#define FROM_COM_RCVD_LEN 16

/* ÚÍ¦ÎÎ¦/ÍÁËÒÏÓÉ, ËÏÔÒ¦ ÈÏÞÅÔØÓÑ ÐÏÔ¦Í ÐÒÉÊÍÁÔÉ ÑË ÏÐÃ¦§ */
#undef WITHOUT_DEVICE
int 	comspeed = B19200;
char *iconv_from="UTF-8";
//char *iconv_from="KOI8-RU";
char *iconv_to="CP1251//IGNORE";
char *printtag="toprint";
char *functag="callback";
int 	BACKLOG = 2;				/* netstat -Lan ; sysctl kern.ipc.somaxconn*/
/* global for getopt */
int	isdaemon	= 0;			/* [-D ] run as daemon */
int 	loglevel	=LOG_NOTICE;		/* [-d 0..7] default LOG_NOTICE = 5 */
char    addr[16]	= "127.0.0.1";	/* [-l] addr to listen */
char    comport[32] 	= "/dev/cuau0";		/* COM	*/
int     port		= 8886;		       	/* [-p] port to listen, default 27001 */
int	LOG_PERROR_	= 0;			/* [-v] all syslog to terminal too, default 0 (disable) */
struct timeval select_timeout = {0,4E5};		/* [-t] select timeout, {seconds,microseconds}, 400 ms */
char	*optstr	 = "d:l:p:c:t:DvVh";

void usage (char *prog)
{
 fprintf(stderr,"usage: %s [options]\n\
  -d <debug level (%u)>\n\
  -l <addr to listen (%s)>\n\
  -p <port to listen (%u)>\n\
  -c <com port (%s)>\n\
  -t <select timeout, microseconds (%u)>\n\
  -D - run as daemon;\n\
  -v - all syslog to console too;\n\
  -V - version info.\n",prog,loglevel,addr,port,comport,select_timeout.tv_usec);
}

int 	estatus = 0;			/* for restart-on-fail loop */
int 	cpid, status;			/* timadvdctrl */
struct rusage crusage;			/* tmp for rusage */
COMFD fdcom; 			/* COM FD */
NETFD fdnet;			/* socket FD */
FD_SET_T	*fdset_r;
iconv_t iconv_vector;
char code_cut_full[2] = {0x1b,0x69}; 	/* ESC + 'i' OR GS + 'V' + 0 */
char code_cut_parital[2] = {0x1b,0x6d};	/* ESC + 'm' OR GS + 'V' + 1 */
/* ÔÁË ¦ ÎÅ ×ÉÛÌÏ ÏÞÉÝÁÔÉ ÂÕÆÅÒ ×¦Ä ÐÏÐÅÒÅÄÎØÏÇÏ ÞÅËÕ. ÐÏËÁ ÄÏÂÉ×Á¤ÍÏ ÐÒÏÂ¦ÌÁÍÉ */
char code_clear_grid[2] = {0x1a,0x43};		/* SUB + 'C' */
char code_clear_buffer[2] = {0x10,0x5};		/* DLE + ENQ */
char code_reset[2] = {0x1b,0x40};		/* ESC + '@' */
char code_set_lmargin[4] = {0x1d,0x4c,32,0};		/* GS + 'L' + nL + nH  - The left margin (nL + nH ? 256) ? 0.125mm as set at */
char code_set_standard_mode[2] = {0x1b,0x53};		/* ESC + 'S' */
int 	from_com_rcvd_flag	= 0;
char 	from_com_rcvd_text[FROM_COM_RCVD_LEN];

/*****************************************************************************************/
/*
Open & Init COMs
Usage: if ( (iofd = init_com("/dev/ucom0"))<0 ) { exit(-1); }
*/
int init_com (char *port) {
	struct termios newtio;			// for tcsetattr()
	int iofd;

	syslog(LOG_NOTICE,"Try to open %s...",port);
	iofd = open(port, O_RDWR, O_NONBLOCK);
//	iofd = open(port, O_RDWR, O_NONBLOCK, O_NOCTTY, O_EXLOCK, O_DIRECT);
	if (iofd <0) {
		perror("getport");
		return iofd;
	};
	syslog(LOG_NOTICE,"%s opened as %d",port,iofd);
	if (-1 == fcntl(iofd, F_SETFL, FNDELAY)) {
		perror("fcntl");
		return -1;
	};
	bzero(&newtio, sizeof(newtio));
	newtio.c_cflag = CS8 | CLOCAL ;
	newtio.c_iflag = IGNPAR;
	newtio.c_oflag = 0 ;
	newtio.c_lflag = 0;
	newtio.c_cc[VTIME]    = 1;	/*  in .1 s, man 4 termios  */
	newtio.c_cc[VMIN]     = 1;
	if (-1 == tcflush(iofd, TCIOFLUSH)) {
		syslog(LOG_CRIT,"init_com(): tcflush error");
                perror("tcflush");
                return -1;
        };
	if (-1 == cfsetspeed(&newtio,comspeed)) {
                syslog(LOG_CRIT,"init_com(): tcflush error");
                perror("cfsetspeed");
                return -1;
	};
       	if (-1 == tcsetattr(iofd,TCSANOW,&newtio)) {
		syslog(LOG_CRIT,"init_com(): tcsetattr error");
		perror("tcsetattr");
		return -1;
	};
	return iofd;
};

int get_comstr(COMFD fdcom)
{
	bzero(from_com_rcvd_text,sizeof(from_com_rcvd_text));
	from_com_rcvd_flag = read(fdcom,from_com_rcvd_text,FROM_COM_RCVD_LEN);
//	syslog(LOG_INFO,"<-- %-10.10s: read  %3.3d bytes: (first 47): [%-47.47s]","get_comstr()",from_com_rcvd_flag,from_com_rcvd_text);
	syslog(LOG_INFO,"<-- %-10.10s: read  %u bytes.","get_comstr()",from_com_rcvd_flag);
}

int get_net(NETFD fd)
/*
	accept();
	ÞÉÔÁ¤ÍÏ Ú ÓÏËÅÔÁ, ¦ÄÅÎÔÉÆ¦ËÕ¤ÍÏ ÚÁÐÉÔ ÄÏ ÎÁÓ, ÁÂÏ return -1 ÑËÝÏ Î¦
	×¦ÄÄÁ¤ÍÏ <form><input name="lastcode" value="lastcode"></form>
	lastcode = "";
	ÚÁËÒÉ×Á¤ÍÏ ×ÓÅ ÝÏ ×¦ÄËÒÉÌÉ ÔÕÔ;
*/
{
	FILE *fsock;			/* socket file */
	char method[16],url[MAX_HTTP_DATA_LENGTH],postdata[MAX_HTTP_DATA_LENGTH];
	struct header_t headers[1];	/* ÚÁÔÉÞËÁ - ÚÁÒÁÚ ÎÅ ÚÁÐÏ×ÎÀ¤ÔØÓÑ ÒÅÁÌØÎÏ*/
	char *printmsg_in, *pin;
	char *printmsg_out, *pout;
	int i;

	if (0>AH_accept(fd,&fsock))
	{
		syslog(LOG_ERR,"get_net: cant accept()");
		return -1;
	}
	if (0>AH_get_request(&fsock,method,url,headers,postdata))
	{
		syslog(LOG_ERR,"get_net: cant get_request()");
		return -1;
	}

	struct form_data_t *form_data;
	int ss=AH_parse_form_data(&form_data,postdata);
	size_t printmsg_len_in, printmsg_len_out, printmsg_len, printmsg_leno;
	int lconv = 0;
	for (i=0;i<ss;i++)
	{
		syslog(LOG_DEBUG,"method: [%s], url:[%s]\nform_data[%d]: [%s]=[%s]",method,url,i,form_data[i].name,form_data[i].val);

		if (!strcmp(functag,form_data[i].name)) {
			/*	ÐÏ×ÅÒÔÁ¤ÍÏ ÔÅ ÝÏ ÐÒÉÛÌÏ Ú ÓÏÍ, ÑËÝÏ ×ÄÒÕÇ	*/
			fprintf(fsock,"%s({ \n ",form_data[i].val);
			fprintf(fsock,"'%s': '%u',\n","from_com_rcvd_length",from_com_rcvd_flag);
			if ( from_com_rcvd_flag ) {
				fprintf(fsock,"'%s': '","from_com_rcvd_data");
			        for (i=0;i<from_com_rcvd_flag;i++) {
					syslog(LOG_NOTICE," DEBUG: from_com_rcvd_data, [%u] from [%u]: [%#2.2x]",i,from_com_rcvd_flag,from_com_rcvd_text[i]);
			                fprintf(fsock," %#2.2x",from_com_rcvd_text[i]);
				}
				fprintf(fsock,"',\n");
			};
			fprintf(fsock,"})\n");
			from_com_rcvd_flag = 0;
		}

		if (!strcmp(printtag,form_data[i].name)) {
			printmsg_len = printmsg_len_in = printmsg_len_out = strlen(form_data[i].val);
			printmsg_leno = ( printmsg_len_out *= 2 ); 					/* ÎÁ ÓÌÕÞÁÊ UTF */
			printmsg_in = pin =  malloc(printmsg_len_in+1);
			printmsg_out = pout = malloc(printmsg_len_out+1);
			bzero(printmsg_in,printmsg_len_in+1);
			bzero(printmsg_out,printmsg_len_out+1);
			strncpy(printmsg_in,form_data[i].val,printmsg_len_in);
			syslog(LOG_DEBUG,"before iconv: in:[%s][%d] out:[%s][%d]",printmsg_in,printmsg_len_in,printmsg_out,printmsg_len_out);
			iconv(iconv_vector,NULL,NULL,NULL,NULL);
			if ((size_t)(-1) == iconv(iconv_vector, (const char**)&printmsg_in, &printmsg_len_in, &printmsg_out, &printmsg_len_out))
				{ perror("iconv"); }
			printmsg_in = printmsg_in-printmsg_len+printmsg_len_in;
			printmsg_out = printmsg_out-printmsg_leno+printmsg_len_out;
/* ÔÁË ¦ ÎÅ ×ÉÛÌÏ ÏÞÉÝÁÔÉ ÂÕÆÅÒ ×¦Ä ÐÏÐÅÒÅÄÎØÏÇÏ ÞÅËÕ. ÐÏËÁ ÄÏÂÉ×Á¤ÍÏ ÐÒÏÂ¦ÌÁÍÉ */
//			write(fdcom,code_reset,strlen(code_reset));
//			write(fdcom,code_set_lmargin,strlen(code_set_lmargin));
			write(fdcom,printmsg_out,strlen(printmsg_out));
			write(fdcom,code_cut_parital,sizeof(code_cut_parital));
			syslog(LOG_DEBUG,"after  iconv: in:[%s][%d] out:[%s][%d]",printmsg_in,printmsg_len_in,printmsg_out,printmsg_len_out);
			free(pin);
			free(pout);
		}
	}
	AH_free_parse_form_data(form_data,ss);
	fclose(fsock);			/* ×¦ÄËÒÉÔÏ × AH_accept */
	syslog(LOG_INFO,"Disconnect.");
	return 0;
}

/*****************************************************************************************/
int main (int argc, char **argv)
{

/* getopt */
char optch;
while ((optch = getopt(argc, argv, optstr)) != -1) {
	switch (optch) {
/*	   case 'b':
		if ( 1 != sscanf(optarg,"%u",&BACKLOG) ) fprintf(stderr,"Getopt: -b is not %%u, ignored");
		break;
*/	   case 'l':
	   	(void)strncpy(addr,optarg,sizeof(addr));
	   	break;
	   case 'c':
	   	(void)strncpy(comport,optarg,sizeof(comport));
	   	break;
	   case 'p':
	     	if ( 1 != sscanf(optarg,"%u",&port) ) fprintf(stderr,"Getopt: -p is not %%u, ignored.\n");
		break;
	   case 't':
	     	if ( 1 != sscanf(optarg,"%u",&(select_timeout.tv_usec)) ) fprintf(stderr,"Getopt: -t is not %%u, ignored.\n");
		break;
	   case 'd':
	     	if ( 1 != sscanf(optarg,"%d",&loglevel) ) fprintf(stderr,"Getopt: -d is not %%u, ignored.\n");
		if ( LOG_EMERG > loglevel ) loglevel = LOG_EMERG;
		if ( LOG_DEBUG < loglevel ) loglevel = LOG_DEBUG;
		break;
	   case 'v':
	     	LOG_PERROR_ = LOG_PERROR;
	   	break;
	   case 'D':
		isdaemon++;
		break;
	   case 'V':
		fprintf(stderr,"$Date: 2010/04/27 15:05:56 $\n$Revision: 1.11 $\n");
		exit(0);
	   case 'h':
	     	usage(argv[0]);
		exit(PRE_FORK_FATAL_STATUS);
	   case '?':
           default:
           	usage(argv[0]);
		exit(PRE_FORK_FATAL_STATUS);
	}
}

/* syslog */
openlog("sprnd",LOG_PERROR_ | LOG_PID | LOG_CONS,LOG_USER);
setlogmask(LOG_UPTO(loglevel));
syslog(LOG_NOTICE,"Log level set to [%u].",loglevel);

if (isdaemon) {
	if (fork()) exit(0);
	chdir("/");
	umask(0);
}
for (;;) {
  	if (cpid=fork()) {
		syslog(LOG_INFO,"fork, child pid= %i",cpid);
		wait4(cpid,&status,0,&crusage);
		if (WIFEXITED(status)) {
			estatus = WEXITSTATUS(status);
			syslog(LOG_ERR,"child [%i] exited with status %i",cpid,estatus);
		}
		if (WIFSIGNALED(status)) {
			syslog(LOG_ERR,"child [%i] killed with signal %i",cpid,WTERMSIG(status));
		}
		syslog(LOG_NOTICE,"child [%i] resource usage statustic:",cpid);
		syslog(LOG_NOTICE,"-----------------------------------------------");
		syslog(LOG_NOTICE,"%30s: %-d.%06d s","user time used",crusage.ru_utime.tv_sec,crusage.ru_utime.tv_usec);
		syslog(LOG_NOTICE,"%30s: %-d.%06d s","system time used",crusage.ru_stime.tv_sec,crusage.ru_stime.tv_usec);
		syslog(LOG_NOTICE,"%30s: %-d kilobytes","max resident set size",crusage.ru_maxrss);
		syslog(LOG_NOTICE,"%30s: %-d kilobytes * ticks-of-execution","integral shared memory size",crusage.ru_ixrss);
		syslog(LOG_NOTICE,"%30s: %-d kilobytes * ticks-of-execution","integral unshared data",crusage.ru_idrss);
		syslog(LOG_NOTICE,"%30s: %-d kilobytes * ticks-of-execution","integral unshared stack",crusage.ru_isrss);
		syslog(LOG_NOTICE,"%30s: %-d","page reclaims",crusage.ru_minflt);
		syslog(LOG_NOTICE,"%30s: %-d","page faults",crusage.ru_majflt);
		syslog(LOG_NOTICE,"%30s: %-d","swaps",crusage.ru_nswap);
		syslog(LOG_NOTICE,"%30s: %-d","block input operations",crusage.ru_inblock);
		syslog(LOG_NOTICE,"%30s: %-d","block output operations",crusage.ru_oublock);
		syslog(LOG_NOTICE,"%30s: %-d","messages sent",crusage.ru_msgsnd);
		syslog(LOG_NOTICE,"%30s: %-d","messages received",crusage.ru_msgrcv);
		syslog(LOG_NOTICE,"%30s: %-d","signals received",crusage.ru_nsignals);
		syslog(LOG_NOTICE,"%30s: %-d","voluntary context switches",crusage.ru_nvcsw);
		syslog(LOG_NOTICE,"%30s: %-d","involuntary context switches",crusage.ru_nivcsw);
		syslog(LOG_NOTICE,"-----------------------------------------------");
  	} else break;
	if (FATAL_STATUS == estatus) exit(estatus);
	else 	sleep(10);
}

/* Open & Init COMs */
#ifndef WITHOUT_DEVICE
if ((fdcom = init_com(comport))<0 )
{
	syslog(LOG_CRIT,"Cant get COM port [%s] - exiting.",comport);
	exit(FATAL_STATUS);
}
/* ÔÁË ¦ ÎÅ ×ÉÛÌÏ ÏÞÉÝÁÔÉ ÂÕÆÅÒ ×¦Ä ÐÏÐÅÒÅÄÎØÏÇÏ ÞÅËÕ. ÐÏËÁ ÄÏÂÉ×Á¤ÍÏ ÐÒÏÂ¦ÌÁÍÉ */
//write(fdcom,code_reset,strlen(code_reset));
//write(fdcom,code_set_lmargin,strlen(code_set_lmargin));
//write(fdcom,code_set_standard_mode,strlen(code_set_standard_mode));

#endif
#ifdef WITHOUT_DEVICE
fdcom = 2;
#endif
/* ÏÔÒÉÍÕ¤ÍÏ fdnet */
if ( 0 > (fdnet = AH_makeserver(addr,port,BACKLOG)))
{
	syslog(LOG_CRIT,"Cant create hhtpd - exiting.");
	exit(FATAL_STATUS);
}

/* iconv */
iconv_vector = iconv_open(iconv_to, iconv_from);
iconvctl(iconv_vector,ICONV_SET_DISCARD_ILSEQ ,&fdcom);

fdset_r = malloc(sizeof(FD_SET_T));
for (;;)
{
	FD_ZERO(fdset_r);
	FD_SET(fdcom,fdset_r);
	FD_SET(fdnet,fdset_r);
	if (select(FD_SETSIZE,fdset_r,NULL,NULL,&select_timeout) >0)
	{
		if (FD_ISSET(fdcom,fdset_r))	get_comstr( fdcom );	/* ÐÒÏ ×ÓÑË ×ÉÐÁÄÏË */
		if (FD_ISSET(fdnet,fdset_r))	get_net( fdnet );
	};

}
free(fdset_r);
iconv_close(iconv_vector);
/* close all */
}







