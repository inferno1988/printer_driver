/* 
 * File:   http_server.h
 * Author: atom
 *
 * Created on 11 Январь 2011 г., 20:10
 */

#ifndef _HTTP_SERVER_H
#define	_HTTP_SERVER_H

#ifdef	__cplusplus
extern "C" {
#endif

struct MHD_Daemon * http_start(unsigned short port, char *host_name);
    void http_stop(struct MHD_Daemon * daemon);
    char *get_post_msg();
    void free_post_mem();
    void init_post_mem();
#ifdef	__cplusplus
}
#endif

#endif	/* _HTTP_SERVER_H */

