#include "proxy.h"


void proxyStart(char *port_str) 
{
    /* Create Host Socket */
    __s32 host_sock;
    if ((host_sock = createSocket()) == 0) {
        showLog("Error: proxyStart -> createSocekt");
        return;
    }

    /* Bind Host Socket */
    struct sockaddr_in host_addr = createAddress(htons(atoi(port_str)));

    if (bind(host_sock, (struct sockaddr*)&host_addr, sizeof(host_addr)) == -1) {
        showLog("Error: proxyStart -> bind");
        return;
    }

    /* Listen Host Socket */
    if (listen(host_sock, 50) == -1) {
        showLog("Error: proxyStart -> listen");
        return;
    }

    /* Accept Client Socket */
    socklen_t addr_size = sizeof(struct sockaddr_in);
    struct sockaddr_in client_addr;

    while (1) {
        __s32 *client_sock = (__s32*)malloc(sizeof(__s32));

        if ((*client_sock = accept(host_sock, (struct sockaddr*)&client_addr, &addr_size)) != -1) {
            pthread_t thread_id = 0;
            pthread_create(&thread_id, 0, &proxyHandler, (void*)client_sock);
            pthread_detach(thread_id);
        } else {
            showLog("Error: proxyStart -> accept");
            return;
        }
    }
}


void *proxyHandler(void *arg) 
{
    /* Client data */
    __s32 *client_sock = (__s32*)arg;
    __u8 *recv_buf = NULL;
    __s32 recv_buf_size = 0;
    __s32 recv_len = 0;

    /* Receive from client */
    if ((recv_len = receiveHttp(*client_sock, &recv_buf, &recv_buf_size, TYPE_CLIENT)) == -1) {
        showLog("Error: proxyHandler -> receiveHttp");
        goto client_out;
    }

    if (recv_len + 20 > recv_buf_size) {
        if (expandBuffer(&recv_buf, &recv_buf_size) == -1) {
            showLog("Error: proxyHandler -> expandBuffer");
            goto client_out;
        }
    }

    /* Relay with server */
    __u8 *relay_buf = NULL;
    __s32 relay_size = 0;
    __s32 relay_len = 0;

    /* Send request to server & Receive response from server */
    if ((relay_len = relayWithServer(recv_buf, &recv_len, &relay_buf, &relay_size)) == -1) {
        showLog("Error: proxyHandler -> realyWithServer");
        goto server_out;
    }

    /* Send to client */
    if (sendHttp(*client_sock, relay_buf, relay_len) == -1) {
        showLog("Error: proxyHandler -> sendHttp");        
        goto server_out;
    }

server_out:
    free(relay_buf);
    relay_buf = NULL;

client_out:
    free(recv_buf);
    recv_buf = NULL;

    close(*client_sock);
    free(client_sock);
}


__s32 receiveHttp(__s32 sock, __u8 **buf, __s32 *buf_len, __s32 type) 
{
    __s32 tot_recv_size = 0;
    __u8 cont_len_buf[20] = {0};
    __s32 cont_len = -1;
    __s32 http_header_len = -1;

    __s32 chunked = 0;

    const __u8 end_symbol[] = { 0x0d, 0x0a, 0x0d, 0x0a, 0x00 };

    while (1) {
        __u8 recv_buf[BUF_SIZE] = {0};
        __s32 recv_size = recv(sock, recv_buf, BUF_SIZE, 0);

        /* Find Content-Length Field */
        if (cont_len == -1 && getHttpField("Content-Length", recv_buf, cont_len_buf) != -1) {
            cont_len = atoi(cont_len_buf);
        }

        /* Find Transfer-Encoding Field */
        if (chunked == 0 && find("Transfer-Encoding: chunked", 26, recv_buf, recv_size) != -1) {
            chunked = 1;
        }

        /* Find 0x0d 0x0a 0x0d 0x0a */
        if (http_header_len == -1) {
            __s32 index = find(end_symbol, END_SYMBOL_LEN, recv_buf, recv_size);
            if (index != -1) {
                http_header_len = tot_recv_size + index + 4;
            }
        }

        if (recv_size == -1) {
            showLog("Error: receiveHttp -> recv");
            return -1;
        } else if (recv_size == 0) {
            /* Received all data from server */
            // showLog("Info: receiveHttp -> recv_size = 0");
            break;
        }

        /* Expand Buffer */
        if (tot_recv_size + recv_size + 1 >= *buf_len) {
            if (expandBuffer(buf, buf_len) == -1) {
                showLog("Error: receiveHttp -> expandBuffer");
                return -1;
            } 
        }

        /* Add Packet to Buffer */
        appendPacket(recv_buf, recv_size, *buf, tot_recv_size);
        tot_recv_size += recv_size;


        /* From Client case */
        if (type == TYPE_CLIENT) {
            /* No chunked case & Foundd end_symbol */
            if (!chunked && http_header_len != -1) {

                /* No Content-Length */
                if (cont_len == -1) {
                    break;
                }

                /* Check Recv Length */
                if (tot_recv_size >= http_header_len + cont_len) {
                    break;
                }

            }

        }
    }

    return tot_recv_size;
}


__s32 sendHttp(__s32 sock, const __u8 *buf, __s32 buf_len)
{
    __s32 tot_send_size = 0;

    while (1) {
        __s32 send_size = 0;
        send_size = send(sock, buf + tot_send_size, buf_len - tot_send_size, 0);

        if (send_size == -1) {
            showLog("Error: sendHttp -> send");            
            return -1;
        } else if (send_size == 0) {
            break;
        }
        
        tot_send_size += send_size;
        if (tot_send_size >= buf_len) {
            break;
        }

    }

    return 0;
}


__s32 relayWithServer(__u8 *recv_buf, __s32 *recv_len, __u8 **relay_buf, __s32 *relay_size)
{
    /* Make packet to send to server*/
    __u8 domain[200] = {0};

    if (getHttpField("Host", recv_buf, domain) == -1) {
        showLog("Error: relayWithServer -> getHttpField");
        return -1;
    }

    /* Delete Domain */
    deleteUri(recv_buf, recv_len);

    /* Add Connection: close */
    addHttpHeader("Connection: close", recv_buf, recv_len);

    /* Send to server */
    __s32 server_sock = createSocket();
    struct sockaddr_in server_addr = createAddress(htons(80));
    server_addr.sin_addr.s_addr = getIpFromDomain(domain);

    if (connect(server_sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        showLog("Error: relayWithServer -> connect");
        return -1;
    }

    if (sendHttp(server_sock, recv_buf, *recv_len) == -1) {
        showLog("Error: relayWithServer -> sendHttp");
        return -1;
    }

    /* Receive from server */
    __s32 relay_len = 0;
    if ((relay_len = receiveHttp(server_sock, relay_buf, relay_size, TYPE_SERVER)) == -1) {
        showLog("Error: relayWithServer -> receiveHttp");
        return -1;
    }

    close(server_sock);

    return relay_len;
}

