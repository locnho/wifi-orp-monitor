#ifdef ARDUINO
  #include <Arduino.h>
#else
#include <iostream>
#include <sstream>
#include <iomanip>
#include <string>
#endif
#include "rs485_parser.h"

//
// RS485
#ifdef ARDUINO
HardwareSerial rs485(1);

// #define DBG_RS485 1
#if defined(DBG_RS485)
  #define RS485_PRINT   Serial.print
  #define RS485_PRINTLN Serial.println
#else
  #define RS485_PRINT(x)
  #define RS485_PRINTLN(x)
#endif

#endif

unsigned char my_rs485_id = 0x88;
unsigned char rs485_buffer[256];
int rs485_buffer_cnt = 0;
unsigned char rs485_pkt[256];
int rs485_pkt_cnt = 0;
bool rs485_ready = false;
unsigned long rs485_last_cmd_ts = 0;

void rs485_setup()
{
#ifdef ARDUINO
  rs485.begin(9600, SERIAL_8N1, 9, 10);
#endif
}

/* From https://github.com/aqualinkd/AqualinkD/blob/master/JANDY_RS485_PROTOCOL.md */
int rs485_generate_checksum(unsigned char* packet, int length)
{
  int i, sum, n;
  n = length - 3;  // Exclude DLE, ETX, and trailing NUL
  sum = 0;
  for (i = 0; i < n; i++)
    sum += (int) packet[i];
  return (sum & 0x0FF);
}

/* From https://github.com/aqualinkd/AqualinkD/blob/master/JANDY_RS485_PROTOCOL.md */
bool rs485_check_jandy_checksum(unsigned char* packet, int length)
{
  if (rs485_generate_checksum(packet, length) == packet[length-3])
    return true;
  
  // Known bug workaround
  if (packet[3] == 0x04 && packet[4] == 0x03 && packet[length-3] == 0x0a)
    return true;  // Forced valid with LOG warning
  
  return false;
}

void rs485_tx_frame(unsigned char *buffer, int len)
{
#ifdef ARDUINO
    rs485.write(buffer, len);
    RS485_PRINT("RS485 TX: ");
    for (int i = 0; i < len; i++) {
        char buf[10];
        sprintf(buf, "0x%02X ", buffer[i]);
        RS485_PRINT(buf);
    }
    RS485_PRINTLN("");
#else
    for (int i = 0; i < len; i++) {
        char buf[10];
        sprintf(buf, "0x%02X ", buffer[i]);
        std::cout << buf;
    }
    std::cout << std::endl;
#endif
}

void rs485_send_ack()
{
  unsigned char tx_buffer[20];

  tx_buffer[0] = 0x10;
  tx_buffer[1] = 0x02;
  tx_buffer[2] = 0x00;
  tx_buffer[3] = 0x01;
  tx_buffer[4] = 0x00;
  tx_buffer[5] = 0x00;
  tx_buffer[6] = 0x13;
  tx_buffer[7] = 0x10;
  tx_buffer[8] = 0x03;
  /* Send out the buffer */
  rs485_tx_frame(tx_buffer, 9);
}

void rs485_process_frame(unsigned char *pkt, int pkt_cnt)
{
  if (pkt[2] == my_rs485_id) {
    switch (pkt[3]) {
    case 0x0: /* Probe */
        rs485_send_ack();
        break;
    }
    return;
  }
}

void rs485_inner_loop(unsigned char ch)
{
  bool frame_done = false;

  if (rs485_buffer_cnt == 0) {
    if (ch == 0x10 /* DLE */) {
      rs485_buffer[rs485_buffer_cnt++] = ch;
    }
    return;
  }
  if (rs485_buffer_cnt == 1) {
    if (ch == 0x02 /* STX */) {
      rs485_buffer[rs485_buffer_cnt++] = ch;
    } else {
      rs485_buffer_cnt = 0;
    }  
    return;
  }
  if (ch != 0x03 /* ETX */) {
    rs485_buffer[rs485_buffer_cnt++] = ch;
  } else {
    if (rs485_buffer[rs485_buffer_cnt-1] != 0x10 /* DLE */) {
      rs485_buffer[rs485_buffer_cnt++] = ch; 
    } else {
      rs485_buffer[rs485_buffer_cnt++] = ch; 
      frame_done = true;
    }
  }
  if (!frame_done) {
    if (rs485_buffer_cnt >= sizeof(rs485_buffer) - 10)
      rs485_buffer_cnt = 0;
    return;
  }

  // Complete frame
  if (!rs485_check_jandy_checksum(rs485_buffer, rs485_buffer_cnt)) {
    rs485_buffer_cnt = 0;
    return;
  }

  rs485_ready = true;

  if (rs485_buffer[0] == 0x10 && rs485_buffer[1] == 0x02 &&
      rs485_buffer[rs485_buffer_cnt-2] == 0x10 && rs485_buffer[rs485_buffer_cnt-1] == 0x03) {
      rs485_pkt_cnt = 0;
      for (int i = 0; i < rs485_buffer_cnt - 1; ) {
        rs485_pkt[rs485_pkt_cnt++] = rs485_buffer[i];
        if (rs485_buffer[i] == 0x10 && rs485_buffer[i+1] == 0)
          i += 2;
        else
          i++;
      }
      rs485_pkt[rs485_pkt_cnt++] = rs485_buffer[rs485_buffer_cnt - 1];
      // process the frame
      rs485_process_frame(rs485_pkt, rs485_pkt_cnt);
  }
  rs485_buffer_cnt = 0;
}

// #define RS485_SIM   1
int row_idx = 0;
int col_idx = 0;
int rs485_sim()
{
    unsigned char sim_data[2][30] = {
        { 7, 0x10, 0x02, 0x84, 0x00, 0x96, 0x10, 0x03 },
        { 0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, 
    };
    if (row_idx < 0)
        return -1;
    if (col_idx == 0) {
        if (sim_data[row_idx][0] == 0) {
            row_idx = -1;
            return -1;
        }
    }
    int ch = sim_data[row_idx][col_idx + 1];
    if (++col_idx >= sim_data[row_idx][0]) {
        row_idx++;
        col_idx = 0;
    }
    return ch;
}

void rs485_loop()
{
#ifdef ARDUINO
  int ch;
  #ifdef RS485_SIM
    ch = rs485_sim();
    if (ch < 0)
        return;
  #else
    if (!rs485.available())
        return;
    ch = rs485.read();
  #endif
  rs485_inner_loop(ch);
#endif
}

void rs485_send_orp(int orp_mv)
{
  unsigned char tx_buffer[20];

  tx_buffer[0] = 0x10;
  tx_buffer[1] = 0x02;
  tx_buffer[2] = 0x00;
  tx_buffer[3] = 0x29;
  tx_buffer[4] = (orp_mv >> 8) & 0xFF;
  tx_buffer[5] = orp_mv & 0xFF;
  tx_buffer[6] = 0x00;
  tx_buffer[7] = 0x10;
  tx_buffer[8] = 0x03;
  tx_buffer[6] = rs485_generate_checksum(tx_buffer, 9);
  /* Send out the buffer */
  rs485_tx_frame(tx_buffer, 9);
}

unsigned long rs485_millis()
{
#ifdef ARDUINO
  return millis();
#else
  return 0;
#endif  
}

void rs485_cmd_loop(int orp_mv)
{
  if (!rs485_ready)
    return;
  if (rs485_last_cmd_ts != 0 &&
     (rs485_millis() - rs485_last_cmd_ts) < 10000) {
    return;
  }
  rs485_last_cmd_ts = rs485_millis();
  // Send ORP status
  rs485_send_orp(orp_mv);  
}
