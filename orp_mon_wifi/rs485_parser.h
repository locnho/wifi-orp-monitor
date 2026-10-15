#ifndef RS485_PARSER_H
#define RS485_PARSER_H  1

void rs485_setup();
void rs485_inner_loop(unsigned char ch);
void rs485_loop();
void rs485_cmd_loop(int orp_mv);

#endif
