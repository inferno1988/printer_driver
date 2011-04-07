#include <microhttpd.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/select.h>
#include <sys/syslog.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include "http_server.h"
#include "const_defs.h"
#include "port_operations.h"

struct connection_info_struct {
    int connectiontype;
    char *answerstring;
    struct MHD_PostProcessor *postprocessor;
};
//enctype=\"multipart/form-data\"
const char *askpage = "<html><META http-equiv=\"Content-Type\" content=\"text/html; charset=\"UTF-8\"><body>\
<h1>Priter test page</h1><br>\
<form action=\"/print\" method=\"post\" >\
<textarea rows=\"40\" cols=\"100\" name=\"toprint\">\
</textarea>\
<input type=\"submit\" value=\" Send \"></form>\
<form action=\"/getpaperstatus\" method=\"post\" >\
<input type=\"submit\" value=\" Get Paper Status\"></form>\
</body></html>";
const char *errorpage =
        "<html><body>This doesn’t seem to be right.</body></html>";
const char *paper_pattern =
        "{ \"paperEnd\": \"%i\", \"nearPaperEnd\": \"%i\" }";
char *post_msg;
char *answerstring;
char *paper_page;

static int answer_to_connection(void *cls, struct MHD_Connection *connection, const char *url, const char *method,
        const char *version, const char *upload_data, size_t *upload_data_size, void **con_cls);
static int
send_page(struct MHD_Connection *connection, const char *page);
static int
iterate_post(void *coninfo_cls, enum MHD_ValueKind kind, const char *key,
        const char *filename, const char *content_type,
        const char *transfer_encoding, const char *data, uint64_t off,
        size_t size);
static void
request_completed (void *cls, struct MHD_Connection *connection,
void **con_cls, enum MHD_RequestTerminationCode toe);

struct MHD_Daemon * http_start(unsigned short port, char *host_name) {
    struct MHD_Daemon * daemon;
    struct sockaddr_in daemon_ip_addr;

    memset(&daemon_ip_addr, 0, sizeof (struct sockaddr_in));
    daemon_ip_addr.sin_family = AF_INET;
    daemon_ip_addr.sin_port = htons(port);
    inet_pton(AF_INET, host_name, &daemon_ip_addr.sin_addr);

    daemon = MHD_start_daemon(MHD_USE_SELECT_INTERNALLY, 
    port,
    NULL,
    NULL,
    &answer_to_connection,
    NULL,
    MHD_OPTION_SOCK_ADDR,
    &daemon_ip_addr,
    MHD_OPTION_NOTIFY_COMPLETED,
    request_completed,
    NULL,
    MHD_OPTION_END);

    if (NULL == daemon) {
        syslog(LOG_ERR, "Error in starting HTTP server");
        closelog();
        exit(0);
    }
    return daemon;
}

void http_stop(struct MHD_Daemon * daemon) {
    MHD_stop_daemon(daemon);
}

static void
request_completed(void *cls, struct MHD_Connection *connection,
        void **con_cls, enum MHD_RequestTerminationCode toe) {
    struct connection_info_struct *con_info = *con_cls;
    if (NULL == con_info)
        return;
    if ((con_info->connectiontype == POST)) {
        int fd = 0;
        fd = init_port(dev_name);
        write_data(fd, answerstring);
        close_port(fd);
        bzero(answerstring, MAXANSWERSIZE);
    }
}
static int
answer_to_connection(void *cls, struct MHD_Connection *connection,
        const char *url, const char *method,
        const char *version, const char *upload_data,
        size_t *upload_data_size, void **con_cls) {
    if (0 == strcmp(url, "/getpaperstatus")) {
        int fd = 0, paper_code = 0;
        fd = init_port(dev_name);
        paper_code = getPaperStatus(fd);
        close_port(fd);
        switch (paper_code){
            case 0: 
                sprintf(paper_page, paper_pattern, 0, 0);
                syslog(LOG_ALERT,"Printer daemon: paper is ok!");
                break;
            case 3: 
                sprintf(paper_page, paper_pattern, 0, 1);
                syslog(LOG_ALERT,"Printer daemon: near paper end!");
                break;
            case 12: 
                sprintf(paper_page, paper_pattern, 1, 0);
                syslog(LOG_ALERT,"Printer daemon: paper is end!");
                break;
            case 15: 
                sprintf(paper_page, paper_pattern, 1, 1);
                syslog(LOG_ALERT,"Printer daemon: paper is end && near paper end!");
                break;
        }
        return send_page(connection, paper_page);
    } else {
    if (NULL == *con_cls) {
        struct connection_info_struct *con_info;
        con_info = malloc(sizeof(struct connection_info_struct));
        if (NULL == con_info)
            return MHD_NO;
        con_info->answerstring = NULL;
        if (0 == strcmp(method, "POST")) {
            con_info->postprocessor =
                    MHD_create_post_processor(connection, POSTBUFFERSIZE, iterate_post, (void *) con_info);
            if (NULL == con_info->postprocessor) {
                free(con_info);
                return MHD_NO;
            }
            con_info->connectiontype = POST;
        } else
            con_info->connectiontype = GET;
        *con_cls = (void *) con_info;
        return MHD_YES;
    }
    if (0 == strcmp(method, "GET")) {
        return send_page(connection, askpage);
    }
    if (0 == strcmp(method, "POST")) {
        struct connection_info_struct *con_info = *con_cls;
        if (*upload_data_size != 0) {
            MHD_post_process(con_info->postprocessor, upload_data, *upload_data_size);
            *upload_data_size = 0;
            return MHD_YES;
        } else if (NULL != con_info->answerstring)
            return send_page(connection, con_info->answerstring);
    }
    return send_page(connection, errorpage);
    }
}

static int
iterate_post(void *coninfo_cls, enum MHD_ValueKind kind, const char *key,
        const char *filename, const char *content_type,
        const char *transfer_encoding, const char *data, uint64_t off,
        size_t size) {
    struct connection_info_struct *con_info = coninfo_cls;
    if (0 == strcmp(key, "toprint")) {
        if ((size > 0) && (size <= MAXNAMESIZE)) {
            strcat(answerstring, data);
            //strcat(post_msg, answerstring);
            con_info->answerstring = answerstring;
        } else
            con_info->answerstring = NULL;
    }
}

static int
send_page(struct MHD_Connection *connection, const char *page)
{
    int ret;
    struct MHD_Response *response;
    response =
            MHD_create_response_from_data(strlen(page), (void *) page, MHD_NO,
            MHD_NO);
    if (!response)
        return MHD_NO;
    ret = MHD_queue_response(connection, MHD_HTTP_OK, response);
    MHD_destroy_response(response);
    return ret;
}

void init_post_mem() {
    post_msg = (char *) malloc(MAXANSWERSIZE);
    if (!post_msg){
        syslog(LOG_ERR, "Error in malloc post_msg");
        closelog();
    }
    dev_name = (char *) malloc(100);
    if (!dev_name){
        syslog(LOG_ERR, "Error in malloc dev_name");
        closelog();
    }
    answerstring = (char *) malloc(MAXANSWERSIZE);
    if (!answerstring){
        syslog(LOG_ERR, "Error in malloc answerstring");
        closelog();
    }
    param_host_name = (char *) malloc(15);
    if (!param_host_name) {
        syslog(LOG_ERR, "Error in malloc param_host_name");
        closelog();
    }
    paper_page = (char *) malloc(MAXANSWERSIZE);
    if (!paper_page) {
        syslog(LOG_ERR, "Error in malloc paper_page");
        closelog();
    }
}

void free_post_mem(){
    free(post_msg);
    free(dev_name);
    free(answerstring);
    free(param_host_name);
    free(paper_page);
}

char *get_post_msg() {
    return post_msg;
}