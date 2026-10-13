#include <stdio.h>
#include <string.h>
#include "osi_api.h"
#include "httpclient.h"
#include "at_api.h"

#define BUF_SIZE        (1024 * 5)
#define HTTP_TEST_URL    "http://pay.chinainfosafe.com:6003"
#define HTTPC_LOG  RTI_LOG

static int vatResName(const char *rsp, const char *name)
{
    if(rsp == NULL || name == NULL){
        return -1;
    }

    // int ret = strncmp(rsp, name, strlen(name));
    if(strncmp(rsp, name, strlen(name)) == 0)
    {
        return strlen(name);
    }
    return -1;
}

static bool prvUpdateCREG2(){
    char resp[128] = {0};
    char *cmd = "AT+CREG?\r\n";
    int n;
    int stat;
    char lac[5] = {0};
    char ci[9] = {0};
    int act;
    int ret = atCmdSendWaitResp(cmd, 1000, "+CREG", 1, "ERROR", resp, sizeof(resp));
    if(ret == 0){
        int pos = vatResName(resp, "+CREG");
        RTI_LOG("pos:%d",pos);
        if(pos >= 0)
        {
            ret = sscanf(resp + pos, ": %d,%d,\"%4s\",\"%8s\",%d\r\n", &n, &stat, lac, ci, &act);
            if(ret == 5)
            {
                RTI_LOG("n:%d state:%d lac:%s,ci:%s",n,stat,lac,ci);
                
                if(stat == 1 || stat == 5){
                    atCmdSendWaitResp("AT+CGACT=1\r\n", 1000, NULL, 1, "ERROR", NULL, 0);
                    return true;
                }else{
                    return false;
                }
            }
        }
        return false;
    }else{
        RTI_LOG("AT CREG Error");
    }
    return false;
}

static void activeNetwork(void){
    atCmdSendWaitResp("AT+CREG=2\r\n", 1000, NULL, 1, NULL, NULL, 0);
    osiThreadSleep(10);
    atCmdSendWaitResp("AT*DIALMODE=0\r\n", 1000, NULL, 1, NULL, NULL, 0);
    osiThreadSleep(10);
    while (1)
    {
        if(prvUpdateCREG2()){
            break;
        }else{
            osiThreadSleep(500);
        }
    }
}

static void httpclient_test_post( char *post_data, int post_data_len)
{
    char *get_url = HTTP_TEST_URL;    

    httpclient_t client = {0};
    httpclient_data_t client_data = {0};
    char *buf, *header;
    HTTPCLIENT_RESULT ret;
    int val_pos, val_len;
    
    HTTPC_LOG( "\nhttpclient_test_get()\n");
    buf = malloc(BUF_SIZE);    
    header = malloc(BUF_SIZE);
    if (buf == NULL || header == NULL) {        
        HTTPC_LOG("\nmemory malloc failed\n");
        return;
    }
    memset(buf, 0, BUF_SIZE);
    memset(header, 0, BUF_SIZE);
    
    // Http "get"
    client_data.header_buf = header;    
    client_data.header_buf_len = BUF_SIZE;
    client_data.response_buf = buf;
    client_data.response_buf_len = BUF_SIZE;    
    client_data.response_buf[0] = '\0';
    client_data.post_buf = post_data;
    client_data.post_buf_len = post_data_len;
    ret = httpclient_post(&client, get_url, &client_data);
    if (ret < 0)
        goto fail;   
    HTTPC_LOG("\ndata received: %s\n", client_data.response_buf);

    // get response header
    if(0 == httpclient_get_response_header_value(client_data.header_buf, "Content-length", &val_pos, &val_len))
        HTTPC_LOG("Content-length: %.*s", val_len, client_data.header_buf + val_pos);

fail:
    free(buf);
    free(header);

    // Print final log
    if (ret >= 0)    
        HTTPC_LOG("httpclient_test_post success.");
    else        
        HTTPC_LOG("httpclient_test_post fail, reason:%d.", ret);
}


static void httpclient_test_get(void)
{
    char *get_url = HTTP_TEST_URL;    

    httpclient_t client = {0};
    httpclient_data_t client_data = {0};
    char *buf, *header;
    HTTPCLIENT_RESULT ret;
    int val_pos, val_len;
    
    HTTPC_LOG( "\nhttpclient_test_get()\n");
    buf = malloc(BUF_SIZE);    
    header = malloc(BUF_SIZE);
    if (buf == NULL || header == NULL) {        
        HTTPC_LOG("\nmemory malloc failed\n");
        return;
    }
    memset(buf, 0, BUF_SIZE);
    memset(header, 0, BUF_SIZE);
    
    // Http "get"
    client_data.header_buf = header;    
    client_data.header_buf_len = BUF_SIZE;
    client_data.response_buf = buf;
    client_data.response_buf_len = BUF_SIZE;    
    client_data.response_buf[0] = '\0';
    ret = httpclient_get(&client, get_url, &client_data);
    if (ret < 0)
        goto fail;   
    HTTPC_LOG("\ndata received: %s\n", client_data.response_buf);

    // get response header
    if(0 == httpclient_get_response_header_value(client_data.header_buf, "Content-length", &val_pos, &val_len))
        HTTPC_LOG("Content-length: %.*s", val_len, client_data.header_buf + val_pos);

fail:
    free(buf);
    free(header);

    // Print final log
    if (ret >= 0)    
        HTTPC_LOG("httpclient_test_get success.");
    else        
        HTTPC_LOG("httpclient_test_get fail, reason:%d.", ret);
}

static void prvHttpClientExample(void *param){
    activeNetwork();
    char *post_data = "Hello chinainfosafe";
    while (1)
    {
        httpclient_test_get();
        osiThreadSleep(2000/5);

        httpclient_test_post(post_data, strlen(post_data));
        osiThreadSleep(2000/5);
    }
    
}

int appimg_enter(void *param)
{
    RTI_LOG("application image enter, param 0x%x", param);
    osiThreadCreate("http_example", prvHttpClientExample, NULL, 18, 10*1024);
    return 0;
}

void appimg_exit(void)
{
    RTI_LOG("application image exit");
}


