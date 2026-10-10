#ifndef __MODBUS_TCP_SERVER_H__
#define __MODBUS_TCP_SERVER_H__

/**
 * @brief Initialize Modbus TCP Server
 * 
 * This creates a FreeRTOS task that will listen on TCP port 502
 * and accept incoming SCADA connections, acting as a transparent
 * bridge to the RS485 RTU network.
 */
void modbus_tcp_server_init(void);

#endif /* __MODBUS_TCP_SERVER_H__ */
