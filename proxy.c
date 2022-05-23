#include <stdio.h>
#include "csapp.h"
#include "cache.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400
#define PROTLEN 7 /* Length of http:// */
#define VERLEN 8 /* Length of HTTP/1.0 and HTTP/1.0 */

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
static const char *host_key = "Host";
static const char *connection_hdr = "Connection: close\r\n";
static const char *proxy_conn_hdr = "Proxy-Connection: close\r\n";

/* Routines for the web proxy */
void process_request(int connfd);
void parse_url(char *url, char *hostname, char *port, char *uri);
void build_requesthdrs(rio_t *rp, char *hostname, char *request_hdrs);
void *thread(void *vargp);
void sig_int_handler(int signal);

/* Global variables for synchronizing accesses to cache */
int readcount;  /* Number of threads reading cache */
sem_t mutex;    /* Semaphore protecting readcount */
sem_t mutex_w;  /* Semaphore protecting writing to cache */

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[1]);
        exit(1);
    }

    /* Install signal handler */
    Signal(SIGPIPE, SIG_IGN); /* Ignore SIGPIPE (broken pipe) */
    Signal(SIGINT, sig_int_handler);

    init_cache();
    readcount = 0;
    Sem_init(&mutex, 0, 1);
    Sem_init(&mutex_w, 0, 1);

    int listenfd, *connfdp;
    listenfd = Open_listenfd(argv[1]);

    struct sockaddr_storage clientaddr;
    socklen_t clientlen;
    char hostname[MAXLINE], port[MAXLINE];
    pthread_t tid;

    while (1) {
        clientlen = sizeof(struct sockaddr_storage);
        connfdp = Malloc(sizeof(int));
        *connfdp = Accept(listenfd, (SA *) &clientaddr, &clientlen);
        Getnameinfo((SA *) &clientaddr, clientlen, hostname, MAXLINE, 
                    port, MAXLINE, 0);
        printf("Accepted connection from (%s, %s)\n", hostname, port);
        Pthread_create(&tid, NULL, thread, connfdp);
    }

    return 0;
}

void *thread(void *vargp)
{
    int connfd = *((int *)vargp);
    Pthread_detach(pthread_self());
    Free(vargp);
    process_request(connfd);
    Close(connfd);
    return NULL;
}

void process_request(int connfd)
{
    char buf[MAXLINE];
    rio_t rio;
    Rio_readinitb(&rio, connfd);
    if (!rio_readlineb(&rio, buf, MAXLINE)) /* Read request line */
        return; /* Nothing to read */

    /* Parse the request line */
    char method[MAXLINE], url[MAXLINE], version[MAXLINE];
    if (sscanf(buf, "%s %s %s", method, url, version) != 3) {
        printf("Invalid HTTP request\n");
        return;
    }

    /* Check for valid request line */
    if (strcmp(method, "GET")) {
        printf("Method not implemented\n");
        return;
    }
    if (strncasecmp(url, "http://", PROTLEN)) {
        printf("Invalid URL\n");
        return;
    }
    if (strncasecmp(version, "HTTP/1.0", VERLEN) &&
        strncasecmp(version, "HTTP/1.1", VERLEN)) 
    {
        printf("Invalid request version\n");
        return;
    }

    char hostname[MAXLINE], port[MAXLINE], uri[MAXLINE];
    parse_url(url, hostname, port, uri);

    /* Create HTTP request to server */
    char http_request[MAXLINE], request_hdrs[MAXLINE];
    
    sprintf(http_request, "GET %s HTTP/1.0\r\n", uri);
    build_requesthdrs(&rio, hostname, request_hdrs);
    strcat(http_request, request_hdrs);

    P(&mutex);
    readcount++;
    if (readcount == 1)
        P(&mutex_w);
    V(&mutex);
    cache_object *cache_obj = find_cache_object(url);
    P(&mutex);
    readcount--;
    if (readcount == 0)
        V(&mutex_w);
    V(&mutex);
    
    if (cache_obj != NULL) {
        Rio_writen(connfd, cache_obj->buf, cache_obj->size);
        printf("Retrieved web object from proxy cache\n");
        return;
    }

    /* Connect to the server */
    /* FD that proxy uses to communicate with server */
    int proxyfd = Open_clientfd(hostname, port);
    Rio_writen(proxyfd, http_request, strlen(http_request));

    /* Forward response from server to client */
    rio_t rioServer;
    Rio_readinitb(&rioServer, proxyfd);
    size_t n, total_byte = 0;

    /* For caching */
    char cache_object[MAX_OBJECT_SIZE];
    char *p = cache_object;
    while ((n = Rio_readlineb(&rioServer, buf, MAXLINE)) != 0) {
        Rio_writen(connfd, buf, n);
        total_byte += n;
        if (total_byte < MAX_OBJECT_SIZE) {
            strcpy(p, buf);
            p += n;
        }
    }
    if (total_byte < MAX_OBJECT_SIZE) {
        P(&mutex_w);
        insert_obj(url, cache_object, total_byte);
        V(&mutex_w);
    }
    Close(proxyfd);
}

void parse_url(char *url, char *hostname, char *port, char *uri)
{
    url += PROTLEN; /* Skip http:// */
    strcpy(hostname, url);

    char *p = index(hostname, '/'); 
    if (p) {
        strcpy(uri, p);
        *p = '\0';  /* Remove uri from hostname */
    } else {
        strcpy(uri, "/");
    }

    /* Get port */
    if (strstr(hostname, ":")) {
        p = index(hostname, ':');
        strcpy(port, p + 1);
        *p = '\0';  /* Remove port from hostname */
    } else {
        strcpy(port, "80");/* Default port */
    }
}

void build_requesthdrs(rio_t *rp, char *hostname, char *request_hdrs)
{
    char buf[MAXLINE], has_host = 0;
    char host_hdr[MAXLINE], other_hdrs[MAXLINE];

    while (1) {
        Rio_readlineb(rp, buf, MAXLINE);
        if (!strcmp(buf, "\r\n"))
            break;
        if (!strncasecmp(buf, host_key, strlen(host_key))) {
            has_host = 1;
            strcpy(host_hdr, buf);
        } else
            strcat(other_hdrs, buf);
    }
    
    if (!has_host) {
        sprintf(host_hdr, "Host: %s\r\n", hostname);
    }

    /* Combine all headers */
    sprintf(request_hdrs, "%s%s%s%s%s%s", host_hdr, user_agent_hdr,
    connection_hdr, proxy_conn_hdr, other_hdrs, "\r\n");

    return;
}

/* Handle interrupt signal */
void sig_int_handler(int signal)
{   
    printf("\n");
    free_cache();
    exit(0);
}