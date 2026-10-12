/**
 * @file   app_protocol.h
 * @brief  协议配置与解析会话管理
 */

#ifndef APP_PROTOCOL_H
#define APP_PROTOCOL_H

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================== */

void App_Protocol_Init(void);
void App_Protocol_Process(void);
void App_Protocol_Request_Parse(void);
void App_Protocol_Cancel(void);

/* ================================================== */

#ifdef __cplusplus
}
#endif

#endif /* APP_PROTOCOL_H */
