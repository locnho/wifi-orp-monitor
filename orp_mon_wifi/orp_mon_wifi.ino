/*
 * ORP Monitor
 *
 */
#define WIFI_SUPPORT 1
#define WIFI_EXTERNAL_ANTENNA 1

#if defined(WIFI_SUPPORT) && (defined(ARDUINO_SEEED_XIAO_ESP32C3) || defined(ARDUINO_ARCH_ESP32) || defined(ARDUINO_SEEED_XIAO_ESP32C6))
#else
  #undef WIFI_SUPPORT
#endif

#include "Button2.h"
#include "Rotary.h"
#include <U8g2lib.h>
#include <Wire.h>
#include <HardwareSerial.h>

#include <TimeLib.h>

#define DBG_SUPPORT 1     /* Print general debug information */
//#define DBG_STATUS 1    /* Print equivalent of menu on serail port */
//#define DBG_MQTT 1        /* Print MQTT messages */
#define DBG_ORP 1         /* Print ORP message */

#if defined(DBG_SUPPORT)
  #define DBG_PRINT   Serial.print
  #define DBG_PRINTLN Serial.println
#else
  #define DBG_PRINT(x)
  #define DBG_PRINTLN(x)
#endif

#if defined(DBG_STATUS)
  #define STATUS_PRINT   Serial.print
  #define STATUS_PRINTLN Serial.println
#else
  #define STATUS_PRINT(x)
  #define STATUS_PRINTLN(x)
#endif

#if defined(DBG_ORP)
  #define ORP_PRINT   Serial.print
  #define ORP_PRINTLN Serial.println
#else
  #define ORP_PRINT(x)
  #define ORP_PRINTLN(x)
#endif

#if defined(DBG_MQTT)
  #define MQTT_PRINT   Serial.print
  #define MQTT_PRINTLN Serial.println
#else
  #define MQTT_PRINT(x)
  #define MQTT_PRINTLN(x)
#endif

#define FW_VERSION    "0.2"

//
// Rotary and Button
#define ROTARY_PIN1	2
#define ROTARY_PIN2	1
#define BUTTON_PIN	A0
#define CLICKS_PER_STEP   4   // this number depends on your rotary encoder 
Rotary rotary;
Button2 button;

//
// OELD
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);
int oled_addr = 0x3C;
bool oled_detected = 0;
unsigned long oled_idle_ts = 0;
bool oled_idle = 0;
unsigned long oled_refresh_ts = 0;
unsigned long oled_alive = 0;
#define OLED_IDLE_TIMEOUT_MS      (2*60000)
bool oled_screen_refresh = 0;

//
// Status message
char status_msg[40];
unsigned long status_msg_ts = 0;

//
// ORP Info
char orp_line[80];
int orp_line_cnt = 0;
float orp_reading = 0.0;
unsigned long ord_ts = 0;
int orp_caled = -1;
unsigned long orp_cal_ts = 0;
unsigned long orp_query_ts = 0;
enum ORP_STATE {
  OP_ORP_READING = 1,
  OP_ORP_CAL_INIT,
  OP_ORP_CAL_RESPONSE,
};
enum ORP_STATE orp_state = OP_ORP_READING;
HardwareSerial orp_serial(0);

//
// Operation State
enum OP_STATE {
  OP_NORMAL = 0,
  OP_MENU,
  OP_ORP_CAL,
  OP_WIFI_SETUP,
  OP_MQTT_CONNECT,
  OP_SETTING_INFO,
};

enum OP_STATE op_state = OP_NORMAL;

//
// WiFi
#if defined(WIFI_SUPPORT)
#include "sdkconfig.h"
#include "WiFi.h"
#include <ArduinoMDNS.h>

typedef struct {
  uint32_t signature1;
  uint32_t signature2;
  char ssid[64];
  char password[64];
  char hostname[64];
  char mqtt_broker[64];
  char mqtt_topic[64];
  char mqtt_user[64];
  char mqtt_password[64];
  char mqtt_pump_topic[64];
  char mqtt_swg_topic[64];
  char mqtt_orp_alarm_topic[64];
  char mqtt_datetime_topic[64];
  int mqtt_port;
  int orp_cal_mV;
  int swg_enable;
  int swg_orp_target;
  int swg_orp_hysteresis;
  float swg_orp_std_dev;
  int swg_orp_interval;
  int swg_orp_guard;
  int swg_orp_pct[5];
  int swg_data_sample_time_sec;

  int start_schedule[7];
  int end_schedule[7];
} Setting_Info;

#define SIGNATURE1    0x57494649
#define SIGNATURE2    0x00000003
#define MQTT_BROKER_DEFAULT   ""
#define MQTT_SUGGEST_BROKER_DEFAULT "aqualink.local"
#define MQTT_TOPIC_DEFAULT          "aqualinkd/CHEM/ORP/set"
#define MQTT_TOPIC_PUMP_DEFAULT     "aqualinkd/Filter_Pump"
#define MQTT_TOPIC_SWG_DEFAULT      "aqualinkd/SWG/Percent"
#define MQTT_TOPIC_DATETIME_DEFAULT "datetime"
#define MQTT_TOPIC_ORP_ALARM_DEFAULT "homebridge"
#define MQTT_USER_DEFAULT     "pi"
#define MQTT_PORT_DEFAULT     1883
#define HOSTNAME_DEFAULT      "ORP"
#define ORP_CAL_MV_DEFAULT    225
#define SWG_ENABLE_DEFAULT    1
#define START_SCHEDULE_DEFAULT    (9*60*60)
#define END_SCHEDULE_DEFAULT      (15*60*60)
Setting_Info setting_info;

char mqtt_pump_topic_match[64];

long rssi = 0;
enum WIFI_STATE {
  OP_WIFI_IDLE = 1,
  OP_WIFI_SMARTCONFIG,
  OP_WIFI_WAIT_CONNECT,
};

#define ORP_PUBLISH_TIMEMS      3000
unsigned long mqtt_orp_publish_ts = 0;

enum WIFI_STATE wifi_state = OP_WIFI_IDLE;
unsigned long wifi_setup_ts = 0;
void mqtt_publish(float orp);
bool is_wifi_connected();
int mqtt_is_subscribed();

unsigned long setting_info_ts = 0;

bool mdns_ready = 0;
unsigned long mdns_ready_ts = 0;
WiFiUDP udp;
MDNS mdns(udp);

#endif /* WIFI_SUPPORT */

unsigned long mqtt_connect_ts = 0;
int mqtt_connect_first_time = 1;
int mqtt_pump_state = -1;
int mqtt_swg_pct = -1;

//
// Serial Setup
void serial_setup()
{
  Serial.begin(115200);
  orp_serial.begin(9600);
}

//
// Status support functions
void set_status_msg(char *msg)
{
  strcpy(status_msg, msg);
  status_msg_ts = millis();
  DBG_PRINTLN(msg);
  oled_screen_refresh = 1;
}

void clear_status_msg()
{
  status_msg[0] = 0;
  status_msg_ts = 0;
  oled_screen_refresh = 1;
}

void check_status_msg_expired()
{
  if (status_msg[0] != 0 && (millis() - status_msg_ts) > 10000) {
    clear_status_msg();
    oled_screen_refresh = 1;
  }
}

//
// Button/Rotary Functions
void button_click(Button2& btn);
void button_rotate(Rotary& r);

void rotary_button_setup()
{
  rotary.begin(ROTARY_PIN1, ROTARY_PIN2, CLICKS_PER_STEP);
  rotary.setChangedHandler(button_rotate);
  button.begin(BUTTON_PIN);
  button.setTapHandler(button_click);
}

///////////////////////////////////////////////////////////////////////////////
// Data Samples Statistic
///////////////////////////////////////////////////////////////////////////////
#include "swganalyzer.h"
unsigned long orp_swg_ctl_chk_ts;
SWGAnalyzer swg_anlyzer;

void orp_data_setup()
{
  swg_anlyzer.setup(setting_info.swg_data_sample_time_sec, setting_info.swg_orp_std_dev, setting_info.swg_orp_target,
                    setting_info.swg_orp_hysteresis, setting_info.swg_orp_interval, setting_info.swg_orp_guard,
                    setting_info.swg_orp_pct);

  for (int i = 0; i < 7; i++) {
    swg_anlyzer.set_schedule(i, setting_info.start_schedule[i], setting_info.end_schedule[i]);
  }
  if (strlen(setting_info.mqtt_datetime_topic) > 0)
    swg_anlyzer.enable_datetime_check(1);
  else
    swg_anlyzer.enable_datetime_check(0);
}

void orp_swg_ctrl_loop()
{
  int swg_pct;

  if ((millis() - orp_swg_ctl_chk_ts) <= 60000) {
    return;
  }

  if (!swg_anlyzer.is_scheduled()) {
    return;
  }

  swg_pct = swg_anlyzer.get_swg_pct();
  mqtt_orp_alarm_publish(swg_anlyzer.is_alarmed());
  if (swg_pct >= 0) {
    swg_set(swg_pct);
  }
  orp_swg_ctl_chk_ts = millis();
}

//
// OELD functions
void clear_idle_timer()
{
  oled_idle = 0;
  oled_idle_ts = millis();
}

void oled_setup()
{
  Wire.begin();
  Wire.beginTransmission(oled_addr);
  int error = Wire.endTransmission();
  Wire.end();
  if (error == 0) {
    DBG_PRINTLN("OLED detected");
    oled_detected = 1;
    clear_status_msg();
  } else {
    DBG_PRINTLN("OLED not detected");
  }

  if (oled_detected) {
    u8g2.begin();
  }
}

void oled_clear()
{
  if (!oled_detected)
    return;
  u8g2.clear();
}

void oled_idle_check()
{
  if (!oled_detected)
    return;

  if (op_state != OP_NORMAL)
    return;

  if (!oled_idle) {
    if ((millis() - oled_idle_ts) >= OLED_IDLE_TIMEOUT_MS) {
      oled_idle = 1;
      oled_clear();
      DBG_PRINTLN("OLED idle");
    }
  }
}

void oled_wifi_update(int show_ip = 1)
{
#if defined(WIFI_SUPPORT)
  if (!oled_detected)
    return;

  u8g2.setFont(u8g2_font_helvB08_tf);
  int offset = 21;
  u8g2.drawStr(offset, 12, "P");
  u8g2.drawCircle(offset + 2, 8, 7);
  if (!mqtt_is_pump_on()) {
     u8g2.drawLine(offset - 3, 2, offset + 7, 14);
     u8g2.drawLine(offset - 2, 2, offset + 8, 14);
     u8g2.drawLine(offset - 4, 2, offset + 6, 14);
     u8g2.drawLine(offset - 3, 14, offset + 7, 2);
     u8g2.drawLine(offset - 2, 14, offset + 8, 2);
     u8g2.drawLine(offset - 4, 14, offset + 6, 2);
  }

  if (!is_wifi_connected())
    return;

  if (rssi >= -55) { 
    u8g2.drawBox(102,7,4,1);
    u8g2.drawBox(107,6,4,2);
    u8g2.drawBox(112,4,4,4);
    u8g2.drawBox(117,2,4,6);
    u8g2.drawBox(122,0,4,8);
  } else if (rssi < -55 & rssi > -65) {
    u8g2.drawBox(102,7,4,1);
    u8g2.drawBox(107,6,4,2);
    u8g2.drawBox(112,4,4,4);
    u8g2.drawBox(117,2,4,6);
    u8g2.drawFrame(122,0,4,8);
  } else if (rssi < -65 & rssi > -75) {
    u8g2.drawBox(102,8,4,1);
    u8g2.drawBox(107,6,4,2);
    u8g2.drawBox(112,4,4,4);
    u8g2.drawFrame(117,2,2,6);
    u8g2.drawFrame(122,0,4,8);
  } else if (rssi < -75 & rssi > -85) {
    u8g2.drawBox(102,8,4,1);
    u8g2.drawBox(107,6,4,2);
    u8g2.drawFrame(112,4,4,4);
    u8g2.drawFrame(117,2,4,6);
    u8g2.drawFrame(122,0,4,8);
  } else if (rssi < -85 & rssi > -96) {
    u8g2.drawBox(102,8,4,1);
    u8g2.drawFrame(107,6,4,2);
    u8g2.drawFrame(112,4,4,4);
    u8g2.drawFrame(117,2,4,6);
    u8g2.drawFrame(122,0,4,8);
  } else {
    u8g2.drawFrame(102,8,4,1);
    u8g2.drawFrame(107,6,4,2);
    u8g2.drawFrame(112,4,4,4);
    u8g2.drawFrame(117,2,4,6);
    u8g2.drawFrame(122,0,4,8);
  }

  if (show_ip) {
    u8g2.setFont(u8g2_font_helvB08_tf);
    IPAddress ip_address = WiFi.localIP();
    String ip_string = ip_address.toString();
    u8g2.drawStr(0, 54, ip_string.c_str());
  }

  if (mqtt_is_subscribed()) {  
    u8g2.setFont(u8g2_font_courR08_tf);
    u8g2.drawStr(u8g2.getDisplayWidth() - u8g2.getStrWidth("MQTT"), 15, "MQTT");
  }
#endif
}

bool is_orp_reading_expired()
{
  if (ord_ts == 0)
    return 1;
  if ((millis() - ord_ts) > 3000)
    return 1;
  return 0;
}

void oled_orp_update()
{
  u8g2.setFont(u8g2_font_helvB08_tf);
  u8g2.drawStr(4, 12, "C");
  u8g2.drawCircle(7, 8, 7);
  if (orp_caled != 1) {
    int offset = 4;
    u8g2.drawLine(offset - 3, 2, offset + 7, 14);
    u8g2.drawLine(offset - 2, 2, offset + 8, 14);
    u8g2.drawLine(offset - 4, 2, offset + 6, 14);
    u8g2.drawLine(offset - 3, 14, offset + 7, 2);
    u8g2.drawLine(offset - 2, 14, offset + 8, 2);
    u8g2.drawLine(offset - 4, 14, offset + 6, 2);
  }
}

void oled_alive_update()
{
  if (oled_alive == 0) {
    u8g2.setFont(u8g2_font_crox5tb_tf);
    u8g2.drawStr(u8g2.getDisplayWidth() - u8g2.getStrWidth("."), u8g2.getDisplayHeight() - 1, ".");
    oled_alive = 1;
  } else {
    oled_alive = 0;
  }
}

void oled_title_update(int show_ip = 0)
{
  u8g2.setFont(u8g2_font_crox5tb_tf);
  u8g2.drawStr(8 * 5, 16, "ORP");

  oled_orp_update();
  oled_wifi_update(show_ip);
}

void oled_status_alive_update(int show_status = 1)
{
  oled_alive_update();

  if (!show_status) {
    return;
  }
  
  u8g2.setFont(u8g2_font_helvB08_tf);
  if (strlen(status_msg) > 0) {
    u8g2.drawStr(0, 63, status_msg);
  } else if (timeStatus() == timeSet) {
    char msg[40];
    sprintf(msg, "%02d/%02d/%02d %02d:%02d:%02d", month(), day(), year(), hour(), minute(), second());
    u8g2.drawStr(0, 63, msg);
  }
}

void oled_title_update_print()
{
  if (orp_caled == 1) {
    STATUS_PRINTLN("CAL");
  }

  if (mqtt_is_pump_on()) {
    STATUS_PRINTLN("PUMP ON");
  } else {
    STATUS_PRINTLN("PUMP OFF");
  }

#if defined(WIFI_SUPPORT)
  if (is_wifi_connected()) {
    STATUS_PRINT("RSSI ");
    STATUS_PRINTLN(rssi);
    if (mqtt_is_subscribed()) {
      STATUS_PRINTLN("MQTT");
    }
    STATUS_PRINTLN(WiFi.localIP());
  }
#endif
}

void oled_status_alive_update_print()
{
  if (strlen(status_msg) > 0) {
    STATUS_PRINTLN(status_msg);
  }
}

void oled_update_normal(bool now)
{
  char msg1[40];
  char msg2[40];

  if (now == 0 && (millis() - oled_refresh_ts) <= 1000)
    return;
  if (oled_idle) {
    return;
  }
  // if (!oled_screen_refresh) {
  //   return;
  // }

  oled_screen_refresh = 0;
  oled_refresh_ts = millis();

  oled_title_update_print();
  int swg_pct;
  if (mqtt_swg_pct < 0)
    swg_pct = 0;
  else
    swg_pct = mqtt_swg_pct;
  if (is_orp_reading_expired()) {
    sprintf(msg1, "----");
    sprintf(msg2, "----   %d%%", swg_pct);
  } else {
    sprintf(msg1, "%0.1f", orp_reading);
    sprintf(msg2, "%0.1f   %d%%", orp_reading, swg_pct);
  }
  STATUS_PRINTLN(msg1);
  STATUS_PRINTLN(msg2);
  STATUS_PRINTLN(swg_anlyzer.get_last_orp());
  STATUS_PRINTLN(swg_anlyzer.get_last_swg_pct());
  oled_status_alive_update_print();

  if (!oled_detected)
    return;

  u8g2.clearBuffer();
  oled_title_update();
  oled_status_alive_update();

  u8g2.setFont(u8g2_font_ncenR18_tf);
  if (u8g2.getStrWidth(msg2) >= 128)
    u8g2.setFont(u8g2_font_ncenR14_tf);
  int x = (u8g2.getDisplayWidth()-10 - u8g2.getStrWidth(msg2)) / 2;
  int x2 = u8g2.getStrWidth(msg1);
  if (setting_info.swg_enable)
    u8g2.drawButtonUTF8(5 + x, 34, U8G2_BTN_BW0, u8g2.getDisplayWidth()-5*2,  0,  0, msg2);
  else
    u8g2.drawButtonUTF8(5 + x, 34, U8G2_BTN_BW0, u8g2.getDisplayWidth()-5*2,  0,  0, msg1);
  u8g2.setFont(u8g2_font_ncenR08_tf);
  u8g2.drawStr(5 + x + x2, 25, "m");
  u8g2.drawStr(5 + x + x2, 34, "V");

  u8g2.setFont(u8g2_font_ncenR12_tf);
  float v1 = swg_anlyzer.get_last_orp();
  int v2 = swg_anlyzer.get_last_swg_pct();
  sprintf(msg1, "%0.1f", v1);
  if (swg_anlyzer.is_alarmed())
    sprintf(msg2, "%0.1f   Alarm", v1);
  else
    sprintf(msg2, "%0.1f   %d%%", v1, v2);
  x = (u8g2.getDisplayWidth()-10 - u8g2.getStrWidth(msg2)) / 2;
  x2 = u8g2.getStrWidth(msg1);
  u8g2.drawButtonUTF8(5 + x, 48 + 2, U8G2_BTN_BW0, u8g2.getDisplayWidth()-5*2,  0,  0, msg2);
  u8g2.setFont(u8g2_font_ncenR08_tf);
  u8g2.drawStr(5 + x + x2 + 1, 39 + 2, "m");
  u8g2.drawStr(5 + x + x2 + 1, 48 + 2, "V");

  u8g2.sendBuffer();
}

int menu_index = 0;
#if defined(WIFI_SUPPORT)
  #define MENU_TOTAL 4
  #define MENU_ORP_INDEX 0
  #define MENU_WIFI_INDEX 1
  #define MENU_SETTING_INFO_INDEX 2
  #define MENU_EXIT_INDEX 3
  char *menu_text[MENU_TOTAL] = {
    "CAL ORP",
    "WiFi Setup",
    "System Info",
    "Exit"
  };
#else
  #define MENU_TOTAL 2
  #define MENU_ORP_INDEX 0
  #define MENU_EXIT_INDEX 1
  #define MENU_WIF_INDEX  128
  #define MENU_SETTING_INFO_INDEX 127
  char *menu_text[MENU_TOTAL] = {
    "CAL ORP",
    "Exit"
  };
#endif

void oled_update_menu()
{
  char text[30];

  if ((millis() - oled_refresh_ts) <= 500)
    return;
  oled_refresh_ts = millis();

  oled_title_update_print();
  for (int i = 0; i < MENU_TOTAL; i++) {
    if (i == menu_index) {
      DBG_PRINT("[");
      DBG_PRINT(menu_text[i]);
      DBG_PRINTLN("]");
    } else {
      DBG_PRINTLN(menu_text[i]);
    }
  }
  oled_status_alive_update_print();

  if (!oled_detected)
    return;

  u8g2.clearBuffer();

  oled_title_update(0);

  u8g2.setFont(u8g2_font_helvB08_tf);
  for (int i = 0; i < MENU_TOTAL; i++) {
    sprintf(text, " %s", menu_text[i]);
    if (i == menu_index)
      u8g2.drawButtonUTF8(5, 25 + 12 * i, U8G2_BTN_INV, u8g2.getDisplayWidth()-5*2, 0, 1, text);
    else
      u8g2.drawButtonUTF8(5, 25 + 12 * i, U8G2_BTN_BW0, u8g2.getDisplayWidth()-5*2, 0, 1, text);
  }

  u8g2.sendBuffer();
}

int oled_update_setting_info()
{
#if defined(WIFI_SUPPORT)
  char msg[30];

  if ((millis() - oled_refresh_ts) <= 500)
    return 0;
  oled_refresh_ts = millis();

  if ((millis() - setting_info_ts) >= 30000) {
    return 1;
  }

  oled_title_update_print();
  oled_status_alive_update_print();
  DBG_PRINT("SSID");
  DBG_PRINTLN(setting_info.ssid);
  DBG_PRINTLN(setting_info.hostname);
  DBG_PRINTLN(WiFi.localIP());

  if (!oled_detected)
    return 0;

  u8g2.clearBuffer();

  oled_title_update(0);
  oled_status_alive_update(0);

  //u8g2.setFont(u8g2_font_helvB08_tf);
  u8g2.setFont(u8g2_font_courB08_tf);
  u8g2.drawStr(0, 24, setting_info.ssid);
  sprintf(msg, "FW %s RSSI %d", FW_VERSION, WiFi.RSSI());
  u8g2.drawStr(0, 32, msg);
  u8g2.drawStr(0, 40, setting_info.hostname);
  IPAddress ip_address = WiFi.localIP();
  String ip_string = ip_address.toString();
  u8g2.drawStr(0, 48, ip_string.c_str());
  sprintf(msg, "ORP CAL %dmV", setting_info.orp_cal_mV);
  u8g2.drawStr(0, 56, msg);
#if defined(WIFI_EXTERNAL_ANTENNA)
  u8g2.drawStr(0, 64, "Antenna External");
#else
  u8g2.drawStr(0, 64, "Antenna Internal");
#endif

  u8g2.sendBuffer();
  return 0;
#else
  return 1;
#endif
}

void oled_update_orp()
{
  if ((millis() - oled_refresh_ts) <= 500)
    return;
  oled_refresh_ts = millis();

  oled_title_update_print();
  switch (orp_state) {
  case OP_ORP_CAL_INIT:
  case OP_ORP_CAL_RESPONSE:
    DBG_PRINTLN("ORP CAL...");
    break;
  }
  oled_status_alive_update_print();

  if (!oled_detected)
    return;

  u8g2.clearBuffer();

  oled_title_update();
  oled_status_alive_update();

  u8g2.setFont(u8g2_font_helvB08_tf);
  u8g2.drawStr(0, 32, "ORP calibrate...");

  u8g2.sendBuffer();
}

void oled_update_wifi()
{
#if defined(WIFI_SUPPORT)
  if ((millis() - oled_refresh_ts) <= 500)
    return;
  oled_refresh_ts = millis();

  oled_title_update_print();
  switch (wifi_state) {
  case OP_WIFI_SMARTCONFIG:
  case OP_WIFI_WAIT_CONNECT:
    DBG_PRINTLN("WiFi...");
    break;
  case OP_WIFI_IDLE:
    break;
  }
  oled_status_alive_update_print();

  if (!oled_detected)
    return;

  u8g2.clearBuffer();

  oled_title_update();
  oled_status_alive_update();

  u8g2.setFont(u8g2_font_helvB08_tf);
  u8g2.drawStr(0, 32, "WiFi Setup...");

  u8g2.sendBuffer();
#endif /* WIF_SUPPORT */
}

void oled_update_mqtt_connect(int now)
{
#if defined(WIFI_SUPPORT)
  if (!now && (millis() - oled_refresh_ts) <= 500)
    return;
  oled_refresh_ts = millis();

  oled_title_update_print();
  switch (wifi_state) {
  case OP_WIFI_SMARTCONFIG:
  case OP_WIFI_WAIT_CONNECT:
    DBG_PRINTLN("WiFi...");
    break;
  case OP_WIFI_IDLE:
    break;
  }
  oled_status_alive_update_print();

  if (!oled_detected)
    return;

  u8g2.clearBuffer();

  oled_title_update();
  oled_status_alive_update();

  u8g2.setFont(u8g2_font_helvB08_tf);
  u8g2.drawStr(0, 32, "MQTT connect...");

  u8g2.sendBuffer();
#endif /* WIF_SUPPORT */
}

void button_rotate(Rotary& r)
{
  DBG_PRINTLN(r.getPosition());
  clear_idle_timer();
  oled_screen_refresh = 1;
  switch (op_state) {
  case OP_MENU:
    {
      int pos = r.getPosition();
      if (pos < 0)
        pos *= -1;
      menu_index = pos % MENU_TOTAL;
    }
    break;
  case OP_ORP_CAL:
  case OP_WIFI_SETUP:
  default:
    break;
  }
}

void menu_action()
{
  switch (menu_index) {
  case MENU_ORP_INDEX:
    op_state = OP_ORP_CAL;
    orp_state = OP_ORP_CAL_INIT;
    break;
#if defined(WIFI_SUPPORT)
  case MENU_WIFI_INDEX:
    op_state = OP_WIFI_SETUP;
    wifi_state = OP_WIFI_SMARTCONFIG;
    break;
  case MENU_SETTING_INFO_INDEX:
    op_state = OP_SETTING_INFO;
    setting_info_ts = millis();
    break;
#endif /* WIFI_SUPPORT */
  case MENU_EXIT_INDEX:
  default:
    op_state = OP_NORMAL;
    break;
  }
}

void button_click(Button2& btn)
{
  DBG_PRINTLN("Click");
  if (oled_idle) {
    clear_idle_timer();
    oled_screen_refresh = 1;
    return;
  }

  clear_idle_timer();
  oled_screen_refresh = 1;
  switch (op_state) {
  case OP_NORMAL:
    op_state = OP_MENU;
    break;
  case OP_MENU:
    menu_action();
    break;
  case OP_SETTING_INFO:
    op_state = OP_NORMAL;
    break;
  case OP_ORP_CAL:
  case OP_WIFI_SETUP:
  default:
    break;
  }
}

/////////////////
// SWG Control
/////////////////
void swg_set(int percent)
{
  ORP_PRINT("Set SWG ");
  ORP_PRINTLN(percent);

  if (mqtt_swg_pct != percent) {
    mqtt_swg_publish(percent);
  }
}

///////////////////////////////////////////////////////////////////////////////
// WiFi Function
///////////////////////////////////////////////////////////////////////////////
#if defined(WIFI_SUPPORT)

#if defined(ARDUINO_SEEED_XIAO_M0)
  #include <FlashStorage.h>
  FlashStorage(setting_info_storage, Setting_Info);
#else
  #include <Preferences.h>
  Preferences prefs;
#endif

void system_setting_clear()
{
  memset((void *) &setting_info, 0, sizeof(setting_info));
  strcpy(setting_info.mqtt_broker, MQTT_BROKER_DEFAULT);
  strcpy(setting_info.mqtt_topic, MQTT_TOPIC_DEFAULT);
  strcpy(setting_info.mqtt_user, MQTT_USER_DEFAULT);
  strcpy(setting_info.hostname, HOSTNAME_DEFAULT);
  strcpy(setting_info.mqtt_pump_topic, MQTT_TOPIC_PUMP_DEFAULT);
  setting_info.mqtt_port = MQTT_PORT_DEFAULT;
  setting_info.orp_cal_mV = ORP_CAL_MV_DEFAULT;
  setting_info.signature1 = SIGNATURE1;
  setting_info.signature2 = SIGNATURE2;
  setting_info.swg_orp_target = SWG_ORP_DEFAULT;
  setting_info.swg_orp_hysteresis = SWG_ORP_HYSTERESIS_DEFAULT;
  setting_info.swg_orp_std_dev = SWG_ORP_STD_DEV_DEFAULT;
  setting_info.swg_orp_interval = SWG_ORP_INTERVAL_DEFAULT;
  setting_info.swg_orp_guard = SWG_ORP_GUARD_DEFAULT;
  setting_info.swg_data_sample_time_sec = SWG_DATA_SAMPLE_TIME_SEC_DEFAULT;
  setting_info.swg_orp_pct[0] = SWG_ORP_PCT0_DEFAULT;
  setting_info.swg_orp_pct[1] = SWG_ORP_PCT1_DEFAULT;
  setting_info.swg_orp_pct[2] = SWG_ORP_PCT2_DEFAULT;
  setting_info.swg_orp_pct[3] = SWG_ORP_PCT3_DEFAULT;
  setting_info.swg_orp_pct[4] = SWG_ORP_PCT4_DEFAULT;
  setting_info.swg_enable = SWG_ENABLE_DEFAULT;
  strcpy(setting_info.mqtt_swg_topic, MQTT_TOPIC_SWG_DEFAULT);
  strcpy(setting_info.mqtt_datetime_topic, MQTT_TOPIC_DATETIME_DEFAULT);
  strcpy(setting_info.mqtt_orp_alarm_topic, MQTT_TOPIC_ORP_ALARM_DEFAULT);
  for (int i = 0; i < 7; i++){
    setting_info.start_schedule[i] = START_SCHEDULE_DEFAULT;
    setting_info.end_schedule[i] = END_SCHEDULE_DEFAULT;
  }
}

void system_setting_init()
{
#if defined(ARDUINO_SEEED_XIAO_M0)
  setting_info = setting_info_storage.read();
  if (setting_info.signature1 != SIGNATURE1 &&
      setting_info.signature2 != SIGNATURE2) {
    system_setting_clear();
  }
#else
  prefs.begin("WIFI-INFO", false);
  String str = prefs.getString("SSID", "");
  strcpy(setting_info.ssid, str.c_str());
  str = prefs.getString("PW", "");
  strcpy(setting_info.password, str.c_str());
  str = prefs.getString("MQTTBROKER", MQTT_BROKER_DEFAULT);
  strcpy(setting_info.mqtt_broker, str.c_str());
  str = prefs.getString("MQTTTOPIC", MQTT_TOPIC_DEFAULT);
  strcpy(setting_info.mqtt_topic, str.c_str());
  str = prefs.getString("MQTTPUMPTOPIC", MQTT_TOPIC_PUMP_DEFAULT);
  strcpy(setting_info.mqtt_pump_topic, str.c_str());
  str = prefs.getString("MQTTSWGTOPIC", MQTT_TOPIC_SWG_DEFAULT);
  strcpy(setting_info.mqtt_swg_topic, str.c_str());
  str = prefs.getString("MQTTUSER", MQTT_USER_DEFAULT);
  strcpy(setting_info.mqtt_user, str.c_str());
  str = prefs.getString("MQTTPW", "");
  strcpy(setting_info.mqtt_password, str.c_str());
  str = prefs.getString("HOSTNAME", HOSTNAME_DEFAULT);
  strcpy(setting_info.hostname, str.c_str());
  setting_info.mqtt_port = prefs.getInt("MQTTPORT", MQTT_PORT_DEFAULT);
  setting_info.orp_cal_mV = prefs.getInt("ORPCALMV", ORP_CAL_MV_DEFAULT);

  setting_info.swg_enable = prefs.getInt("SWGENABLE", SWG_ENABLE_DEFAULT);
  setting_info.swg_orp_target = prefs.getInt("SWGORPTARGET", SWG_ORP_DEFAULT);
  setting_info.swg_orp_std_dev = prefs.getFloat("SWGORSTDEVF", SWG_ORP_STD_DEV_DEFAULT);
  setting_info.swg_orp_hysteresis = prefs.getInt("SWGORPHYST", SWG_ORP_HYSTERESIS_DEFAULT);
  setting_info.swg_orp_interval = prefs.getInt("SWGORPINTERVAL", SWG_ORP_INTERVAL_DEFAULT);
  setting_info.swg_orp_guard = prefs.getInt("SWGORPGUARD", SWG_ORP_GUARD_DEFAULT);
  setting_info.swg_orp_pct[0] = prefs.getInt("SWGORPPCT0", SWG_ORP_PCT0_DEFAULT);
  setting_info.swg_orp_pct[1] = prefs.getInt("SWGORPPCT1", SWG_ORP_PCT1_DEFAULT);
  setting_info.swg_orp_pct[2] = prefs.getInt("SWGORPPCT2", SWG_ORP_PCT2_DEFAULT);
  setting_info.swg_orp_pct[3] = prefs.getInt("SWGORPPCT3", SWG_ORP_PCT3_DEFAULT);
  setting_info.swg_orp_pct[4] = prefs.getInt("SWGORPPCT4", SWG_ORP_PCT4_DEFAULT);
  setting_info.swg_data_sample_time_sec = prefs.getInt("SWGTIME", SWG_DATA_SAMPLE_TIME_SEC_DEFAULT);

  str = prefs.getString("MQTTDTTOPIC", MQTT_TOPIC_DATETIME_DEFAULT);
  strcpy(setting_info.mqtt_datetime_topic, str.c_str());

  str = prefs.getString("MQTTORPALARMTOPIC", MQTT_TOPIC_ORP_ALARM_DEFAULT);
  strcpy(setting_info.mqtt_orp_alarm_topic, str.c_str());

  for (int i = 0; i < 7; i++) {
    char key_name[40];
    sprintf(key_name, "STARTSCHEDULE%d", i);
    setting_info.start_schedule[i] = prefs.getInt(key_name, START_SCHEDULE_DEFAULT);
    sprintf(key_name, "ENDCHEDULE%d", i);
    setting_info.end_schedule[i] = prefs.getInt(key_name, END_SCHEDULE_DEFAULT);
  }

#endif
  DBG_PRINT("WiFi: ");
  DBG_PRINTLN(setting_info.ssid);
  //DBG_PRINT("/");
  //DBG_PRINTLN(setting_info.password);
  DBG_PRINTLN("MQTT:");
  DBG_PRINTLN(setting_info.mqtt_broker);
  DBG_PRINTLN(setting_info.mqtt_user);
  // DBG_PRINTLN(setting_info.mqtt_password);
  DBG_PRINTLN(setting_info.mqtt_port);
  DBG_PRINTLN(setting_info.mqtt_topic);
  DBG_PRINTLN(setting_info.mqtt_pump_topic);
  DBG_PRINTLN(setting_info.orp_cal_mV);
}

void system_setting_save()
{
#if defined(ARDUINO_SEEED_XIAO_M0)
  setting_info_storage.write(setting_info);
#else
  prefs.putString("SSID", setting_info.ssid);
  prefs.putString("PW", setting_info.password);
  prefs.putString("MQTTBROKER", setting_info.mqtt_broker);
  prefs.putString("MQTTTOPIC", setting_info.mqtt_topic);
  prefs.putString("MQTTPUMPTOPIC", setting_info.mqtt_pump_topic);
  prefs.putString("MQTTSWGTOPIC", setting_info.mqtt_swg_topic);
  prefs.putString("MQTTUSER", setting_info.mqtt_user);
  prefs.putString("MQTTPW", setting_info.mqtt_password);
  prefs.putInt("MQTTPORT", setting_info.mqtt_port);
  prefs.putString("HOSTNAME", setting_info.hostname);
  prefs.putInt("ORPCALMV", setting_info.orp_cal_mV);

  prefs.putInt("SWGENABLE", setting_info.swg_enable);
  prefs.putInt("SWGORPTARGET", setting_info.swg_orp_target);
  prefs.putFloat("SWGORSTDEVF", setting_info.swg_orp_std_dev);
  prefs.putInt("SWGORPHYST", setting_info.swg_orp_hysteresis);
  prefs.putInt("SWGORPINTERVAL", setting_info.swg_orp_interval);
  prefs.putInt("SWGORPGUARD", setting_info.swg_orp_guard);
  prefs.putInt("SWGORPPCT0", setting_info.swg_orp_pct[0]);
  prefs.putInt("SWGORPPCT1", setting_info.swg_orp_pct[1]);
  prefs.putInt("SWGORPPCT2", setting_info.swg_orp_pct[2]);
  prefs.putInt("SWGORPPCT3", setting_info.swg_orp_pct[3]);
  prefs.putInt("SWGORPPCT4", setting_info.swg_orp_pct[4]);
  prefs.putInt("SWGTIME", setting_info.swg_data_sample_time_sec);

  prefs.putString("MQTTDTTOPIC", setting_info.mqtt_datetime_topic);
  prefs.putString("MQTTORPALARMTOPIC", setting_info.mqtt_orp_alarm_topic);

  for (int i = 0; i < 7; i++) {
    char key_name[40];
    sprintf(key_name, "STARTSCHEDULE%d", i);
    prefs.putInt(key_name, setting_info.start_schedule[i]);
    sprintf(key_name, "ENDCHEDULE%d", i);
    prefs.putInt(key_name, setting_info.end_schedule[i]);
  }
#endif
}

void system_setting_update()
{
  int len = strlen(setting_info.mqtt_pump_topic);

  if (len > 0) {
    strcpy(mqtt_pump_topic_match, setting_info.mqtt_pump_topic);
    if (mqtt_pump_topic_match[len-1] == '#' &&
        mqtt_pump_topic_match[len-2] == '/') {
      mqtt_pump_topic_match[len-2] = '\0';
    }
  } else {
    mqtt_pump_topic_match[0] = '\0';
  }
}

bool is_wifi_connected()
{
#if defined(WIFI_SUPPORT)
  if (WiFi.status() == WL_CONNECTED)
    return 1;
#endif
  return 0;
}

void wifi_init()
{
#if defined(WIFI_SUPPORT)
#if defined(WIFI_EXTERNAL_ANTENNA)
  pinMode(WIFI_ENABLE, OUTPUT);   // pinMode(3, OUTPUT);
  digitalWrite(WIFI_ENABLE, LOW); // digitalWrite(3, LOW); // Activate RF switch control
  delay(100);
  pinMode(WIFI_ANT_CONFIG, OUTPUT);     // pinMode(14, OUTPUT);
  digitalWrite(WIFI_ANT_CONFIG, HIGH);  // digitalWrite(14, HIGH); // Use external antenna
#endif
  WiFi.setHostname(setting_info.hostname);
  WiFi.begin(setting_info.ssid, setting_info.password);
#endif
}

void wifi_setup()
{
  system_setting_init();
  system_setting_update();
  wifi_init();
}

void wifi_resetup()
{
  WiFi.begin(setting_info.ssid, setting_info.password);
}

#else
void wifi_setup() { }
void wifi_resetup() { }
bool is_wifi_connected() { return 0; }
#endif /* WIFI_SUPPORT */

///////////////////////////////////////////////////////////////////////////////
// ORP Function
///////////////////////////////////////////////////////////////////////////////
void orp_loop()
{
//#define SIM_ORP_TEST  1
#if defined(SIM_ORP_TEST)
  if ((millis() - ord_ts) >= 1000) {
    ord_ts = millis();
    orp_caled = 1;
    if (orp_reading == 620)
      orp_reading = 550;
    else
    orp_reading = 550;
#if defined(WIFI_SUPPORT)
    // Publish only if pump is ON.
    if ((millis() - mqtt_orp_publish_ts) >= ORP_PUBLISH_TIMEMS) {
      if (mqtt_is_pump_on() && swg_anlyzer.is_scheduled())
        mqtt_publish(orp_reading);
      else
        mqtt_publish(0.0);
      mqtt_orp_publish_ts = millis();
    }
#endif
    swg_anlyzer.orp_add(orp_reading);
  }
#else

  if (orp_state == OP_ORP_READING && orp_caled == -1 && (millis() - orp_query_ts) >= 3000) {
      orp_serial.write("Cal,?\r");
      orp_query_ts = millis();
      ORP_PRINTLN("ORP query");
  }

  if (!orp_serial.available())
    return;
 
  char ch = orp_serial.read();
  if (ch != 0xD) {
    if (orp_line_cnt < 50) {
      orp_line[orp_line_cnt++] = ch;
    }
    return;
  }
  
  if (orp_line_cnt <= 0)
     return;
  orp_line[orp_line_cnt] = '\0';
  // ORP_PRINTLN(orp_line);

  switch (orp_state) {
  case OP_ORP_CAL_RESPONSE:
    if (strncmp(orp_line, "*OK", 3) == 0) {
      orp_state = OP_ORP_READING;
      set_status_msg("CAL OK");
      orp_caled = -1;
      orp_query_ts = 0;
      oled_screen_refresh = 1;
    }
    orp_line_cnt = 0;
    break;
  default:
    if (orp_caled < 0) {
      if (strncmp(orp_line, "?CAL,0", 6) == 0) {
        orp_caled = 0;
        oled_screen_refresh = 1;
      }
      if (strncmp(orp_line, "?CAL,1", 6) == 0) {
        orp_caled = 1;
        oled_screen_refresh = 1;
      }
    }
    
    if (!isdigit(orp_line[0])) {
      orp_line_cnt = 0;
      break;
    }
    orp_line[orp_line_cnt] = 0;
    orp_reading = atof(orp_line);
    char msg[80];
    sprintf(msg, "ORP: %0.1f", orp_reading);
    ORP_PRINTLN(msg);
    ord_ts = millis();
    orp_line_cnt = 0;
    oled_screen_refresh = 1;
#if defined(WIFI_SUPPORT)
    if ((millis() - mqtt_orp_publish_ts) >= ORP_PUBLISH_TIMEMS) {
      if (mqtt_is_pump_on() && swg_anlyzer.is_scheduled())
        mqtt_publish(orp_reading);
      else
        mqtt_publish(0.0);
      mqtt_orp_publish_ts = millis();
    }
#endif
    if (mqtt_is_pump_on()) {
      swg_anlyzer.orp_add(orp_reading);
    }
    break;
  }
#endif
}

void orp_cal_init()
{
  char msg[40];

  sprintf(msg, "Cal,%d\r", setting_info.orp_cal_mV);
  orp_state = OP_ORP_CAL_INIT;
  ORP_PRINTLN(msg);
  orp_serial.write(msg);
  set_status_msg("ORP calibrate");
  orp_cal_ts = millis();
}

int orp_cal_loop()
{
  switch (orp_state) {
  case OP_ORP_CAL_INIT:
    orp_cal_init();
    orp_state = OP_ORP_CAL_RESPONSE;
    break;
  case OP_ORP_CAL_RESPONSE:
    if ((millis() - orp_cal_ts) >= 15000) {
      orp_state = OP_ORP_READING;
      set_status_msg("ORP calibrate failed");
      return 1;
    }
  case OP_ORP_READING:
    return 1;
    break;
  }
  return 0;
}

/////////////////////////////////////////////////////////////////////
// WiFi Functions
/////////////////////////////////////////////////////////////////////
#if defined(WIFI_SUPPORT)
void wifi_smartconfig()
{
  WiFi.mode(WIFI_AP_STA);
  WiFi.beginSmartConfig();
}

int wifi_loop()
{
  switch (wifi_state) {
  case OP_WIFI_SMARTCONFIG:
    system_setting_clear();
    system_setting_save();
    system_setting_update();
    wifi_smartconfig();
    set_status_msg("SmartConfig started");
    wifi_state = OP_WIFI_WAIT_CONNECT;
    wifi_setup_ts = millis();
    break;
  case OP_WIFI_WAIT_CONNECT:
    // Wait for SmartConfig packet from mobile
    if (WiFi.smartConfigDone()) {
      set_status_msg("Waiting WiFi...");
    }
    if (WiFi.status() == WL_CONNECTED) {
      set_status_msg("WiFi Connected");
      String ssid_str = WiFi.SSID();
      String passwd_str = WiFi.psk();
      strcpy(setting_info.ssid, ssid_str.c_str());
      strcpy(setting_info.password, passwd_str.c_str());
      system_setting_save();
      system_setting_update();
      wifi_state = OP_WIFI_IDLE;
      return 1;
    }
    if ((millis() - wifi_setup_ts) > 3 *60 * 1000) {
      set_status_msg("WiFi Failed");
      wifi_state = OP_WIFI_IDLE;
      return 1;
    }
    break;
  }
  return 0;
}

unsigned long wifi_connect_chk_ts = 0;
void wifi_link_chk_loop()
{
  if (is_wifi_connected()) {
    long val = WiFi.RSSI();

    if (val != rssi) {
      oled_screen_refresh = 1;
      rssi = val;
    }
    return;
  }

  if ((millis() - wifi_connect_chk_ts) <= 15*1000) {
    return;
  }
  
  wifi_resetup();
  wifi_connect_chk_ts = millis();
}

/////////////////////////////////////////////////////////////////////
// MQTT Functions
/////////////////////////////////////////////////////////////////////
#include <WebServer.h>
#include <ArduinoMqttClient.h>

WiFiClient wifi_client;
MqttClient mqtt_client(wifi_client);
bool mqtt_subscribed = 0;

#define HTTP_PORT   80
WebServer server(HTTP_PORT);
String receivedMessage = "";

const char* htmlFormStart = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>ORP Configuration</title>
    <style>
        body {
            font-family: Arial, sans-serif;
            background-color: #f4f4f9;
            margin: 0;
            padding: 0;
        }
        .form-container {
            width: 400px;
            margin: 50px auto;
            background-color: white;
            padding: 20px;
            border-radius: 8px;
            box-shadow: 0 4px 8px rgba(0, 0, 0, 0.1);
        }
        form {
            display: flex;
            flex-direction: column;
        }
        .form-group {
            display: flex;
            align-items: center;
            margin-bottom: 15px;
        }        
        label {
            font-size: 14px;
            color: #333;
            margin-bottom: 5px;
        }
        label2 {
            width: 200px;
            font-size: 14px;
            color: #333;
            margin-bottom: 5px;
        }
        input[type="text"],
        input[type="email"],
        input[type="password"] {
            width: 100%;
            padding: 10px;
            margin-bottom: 20px;
            border: 1px solid #ccc;
            border-radius: 4px;
            box-sizing: border-box;
            font-size: 16px;
        }
        input[type="side"] {
            flex-grow: 1;
            padding: 10px;
            margin-bottom: 1px;
            border: 1px solid #ccc;
            border-radius: 4px;
            box-sizing: border-box;
            font-size: 16px;
        }
        input[type="side2"] {
            flex-grow: 1;
            width: 125px;
            padding: 10px;
            margin-bottom: 1px;
            border: 1px solid #ccc;
            border-radius: 4px;
            box-sizing: border-box;
            font-size: 16px;
        }
        input[type="submit"] {
            width: 100%;
            padding: 10px;
            background-color: #007bff;
            color: white;
            border: none;
            border-radius: 4px;
            font-size: 16px;
            cursor: pointer;
            transition: background-color 0.3s ease;
        }
        input[type="submit"]:hover {
            background-color: #0056b3;
        }
        input[type="submit"]:active {
            background-color: #004494;
        }
        h1 {
           text-align: center;
        }
    </style>
</head>
<body>
    <script>
      function updateField() {
        const inputTarget = document.getElementById('swgtarget');
        const inputHysteresis = document.getElementById('swghysteresis');
        const inputInterval = document.getElementById('swginterval');
        const swg0 = document.getElementById('swgpcttxt0');
        const swg1 = document.getElementById('swgpcttxt1');
        const swg2 = document.getElementById('swgpcttxt2');
        const swg3 = document.getElementById('swgpcttxt3');
        const swg4 = document.getElementById('swgpcttxt4');

        const tgtValue = Number(inputTarget.value);
        const hystValue = Number(inputHysteresis.value);
        const intValue = Number(inputInterval.value);

        swg0.textContent = `ORP ${tgtValue - hystValue - (intValue*1) + 1} - ${tgtValue - hystValue - (intValue * 0)} mV (%):`;
        swg1.textContent = `ORP ${tgtValue - hystValue - (intValue*2) + 1} - ${tgtValue - hystValue - (intValue * 1)} mV (%):`;
        swg2.textContent = `ORP ${tgtValue - hystValue - (intValue*3) + 1} - ${tgtValue - hystValue - (intValue * 2)} mV (%):`;
        swg3.textContent = `ORP ${tgtValue - hystValue - (intValue*4) + 1} - ${tgtValue - hystValue - (intValue * 3)} mV (%):`;
        swg4.textContent = `ORP ${tgtValue - hystValue - (intValue*5) + 1} - ${tgtValue - hystValue - (intValue * 4)} mV (%):`;
      }
    </script>
    <div class="form-container">
        <h1 >ORP Configuration</h1>
        <form action="/submit" method="POST">
)rawliteral";

const char* htmlHostname = R"rawliteral(
            <label for="hostname">Host Name:</label>
            <input type="text" id="hostname" name="hostname" value="%s" title="Default is 'ORP'." required>
)rawliteral";

const char* htmlMqttBroker = R"rawliteral(
            <label for="broker">MQTT Server:</label>
            <input type="text" id="broker" name="broker" value="%s" title="Default is 'aqualinkd.local'" required>
)rawliteral";

const char* htmlMqttUser = R"rawliteral(
            <label for="username">MQTT User:</label>
            <input type="text" id="username" name="username" value="%s" title="Default is 'pi'" required>
)rawliteral";

const char* htmlMqttPassword = R"rawliteral(
            <label for="password">MQTT Password:</label>
            <input type="password" id="password" name="password" required>
)rawliteral";

const char* htmlMqttPort = R"rawliteral(
            <label for="port">MQTT Port:</label>
            <input type="text" id="port" name="port" value="%d" title="Default is 1883" required>
)rawliteral";

const char* htmlMqttTopic = R"rawliteral(
            <label for="topic">MQTT ORP Topic:</label>
            <input type="text" id="topic" name="topic" title="For AquaLinkD, set to 'aqualinkd/CHEM/ORP/set'" value="%s" required>
)rawliteral";

const char* htmlMqttPumpTopic = R"rawliteral(
            <label for="pumptopic">MQTT Pump Topic:</label>
            <input type="text" id="pumptopic" name="pumptopic" title="For AquaLinkD, set to 'aqualinkd/Filter_Pump'" value="%s" required>
)rawliteral";

const char* htmlMqttSWGTopic = R"rawliteral(
            <label for="swgtopic">MQTT SWG Topic:</label>
            <input type="text" id="swgtopic" name="swgtopic" title="For AquaLinkD, set to 'aqualinkd/SWG/Percent'" value="%s" required>
)rawliteral";

const char* htmlMqttORAlarmPTopic = R"rawliteral(
            <label for="orpalarmtopic">MQTT ORP Alarm Topic:</label>
            <input type="text" id="orpalarmtopic" name="orpalarmtopic" title="Set topic for ORP alarm (based on homebridge-mqtt). Set to blank to disable. Default is 'homebridge'" value="%s" require>
)rawliteral";

const char* htmlOrpCalmV = R"rawliteral(
            <div class="form-group">
            <label2 for="orpcal">ORP Calibration mV:</label2>
            <input type="side" id="orpcal" name="orpcal" value="%d" title="Default is 225" required>
            </div>
)rawliteral";

const char* htmlSWGTgt = R"rawliteral(
            <div class="form-group">
            <label2 for="swgtarget">SWG Target mV:</label2>
            <input type="side" id="swgtarget" name="swgtarget" onchange="updateField()" title="Default is 700" value="%d" required>
            </div>
)rawliteral";

const char* htmlSWGHyst = R"rawliteral(
            <div class="form-group">
            <label2 for="swghysteresis">SWG Hysteresis mV:</label2>
            <input type="side" id="swghysteresis" name="swghysteresis" onchange="updateField()" title="Default is 25" value="%d" required>
            </div>
)rawliteral";

const char* htmlSWGStdDev = R"rawliteral(
            <div class="form-group">
            <label2 for="swgstddev">SWG Std Deviation mV:</label2>
            <input type="side" id="swgstddev" name="swgstddev" value="%.1f" title="Default is 1.5" required>
            </div>
)rawliteral";

const char* htmlSWGInterval = R"rawliteral(
            <div class="form-group">
            <label2 for="swginterval">SWG Interval mV:</label2>
            <input type="side" id="swginterval" name="swginterval" onchange="updateField()" title="Default is 15" value="%d" required>
            </div>
)rawliteral";

const char* htmlSWGTime = R"rawliteral(
            <div class="form-group">
            <label2 for="swgtime">SWG Sample Time (sec):</label2>
            <input type="side" id="swgtime" name="swgtime" title="Default is 600" value="%d" required>
            </div>
)rawliteral";

const char* htmlSWGCtrl = R"rawliteral(
            <div class="form-group">
            <label2 for="swgctrl">Enable SWG Change:</label2>
            <input type="checkbox" id="swgctrl" name="swgctrl" %s>
            </div>
)rawliteral";

const char* htmlSWGPct0 = R"rawliteral(
            <div class="form-group">
            <label2 id="swgpcttxt0" for="swgpct0">ORP %d - %d mV (%%):</label2>
            <input type="side" id="swgpct0" name="swgpct0" value="%d" required>
            </div>
)rawliteral";
const char* htmlSWGPct1 = R"rawliteral(
            <div class="form-group">
            <label2 id="swgpcttxt1" for="swgpct1">ORP %d - %d mV (%%):</label2>
            <input type="side" id="swgpct1" name="swgpct1" value="%d" required>
            </div>
)rawliteral";
const char* htmlSWGPct2 = R"rawliteral(
            <div class="form-group">
            <label2 id="swgpcttxt2" for="swgpct2">ORP %d - %d mV (%%):</label2>
            <input type="side" id="swgpct2" name="swgpct2" value="%d" required>
            </div>
)rawliteral";
const char* htmlSWGPct3 = R"rawliteral(
            <div class="form-group">
            <label2 id="swgpcttxt3" for="swgpct3">ORP %d - %d mV (%%):</label2>
            <input type="side" id="swgpct3" name="swgpct3" value="%d" required>
            </div>
)rawliteral";
const char* htmlSWGPct4 = R"rawliteral(
            <div class="form-group">
            <label2 id="swgpcttxt4" for="swgpct4">ORP %d - %d mV (%%):</label2>
            <input type="side" id="swgpct4" name="swgpct4" value="%d" required>
            </div>
)rawliteral";

const char* htmlMqttDTTopic = R"rawliteral(
            <label for="dttopic">MQTT Date Topic:</label>
            <input type="text" id="dttopic" name="dttopic" title="Set topic to 'datetime' to enable schedule. Leave blank to disable. Schedule applies to MQTT ORP reporting as well" value="%s" required>
)rawliteral";

const char* htmlSchedule0 = R"rawliteral(
            <div class="form-group">
            <label2 id="scheduletxt0" for="schedule0">Sunday:</label2>
            <input type="side2" id="schedule0" name="schedule0" value="%s" title="Default is 09:00:00" required>
            <input type="side2" id="schedule0e" name="schedule0e" value="%s" title="Default is 15:00:00" required>
            </div>
)rawliteral";
const char* htmlSchedule1 = R"rawliteral(
            <div class="form-group">
            <label2 id="scheduletxt1" for="schedule1">Monday:</label2>
            <input type="side2" id="schedule1" name="schedule1" value="%s" required>
            <input type="side2" id="schedule1e" name="schedule1e" value="%s" required>
            </div>
)rawliteral";
const char* htmlSchedule2 = R"rawliteral(
            <div class="form-group">
            <label2 id="scheduletxt2" for="schedule2">Tuesday:</label2>
            <input type="side2" id="schedule2" name="schedule2" value="%s" required>
            <input type="side2" id="schedule2e" name="schedule2e" value="%s" required>
            </div>
)rawliteral";
const char* htmlSchedule3 = R"rawliteral(
            <div class="form-group">
            <label2 id="scheduletxt3" for="schedule3">Wednesday:</label2>
            <input type="side2" id="schedule3" name="schedule3" value="%s" required>
            <input type="side2" id="schedule3e" name="schedule3e" value="%s" required>
            </div>
)rawliteral";
const char* htmlSchedule4 = R"rawliteral(
            <div class="form-group">
            <label2 id="scheduletxt4" for="schedule4">Thursday:</label2>
            <input type="side2" id="schedule4" name="schedule4" value="%s" required>
            <input type="side2" id="schedule4e" name="schedule4e" value="%s" required>
            </div>
)rawliteral";
const char* htmlSchedule5 = R"rawliteral(
            <div class="form-group">
            <label2 id="scheduletxt5" for="schedule5">Friday:</label2>
            <input type="side2" id="schedule5" name="schedule5" value="%s" required>
            <input type="side2" id="schedule5e" name="schedule5e" value="%s" required>
            </div>
)rawliteral";
const char* htmlSchedule6 = R"rawliteral(
            <div class="form-group">
            <label2 id="scheduletxt6" for="schedule6">Saturday:</label2>
            <input type="side2" id="schedule6" name="schedule6" value="%s" required>
            <input type="side2" id="schedule6e" name="schedule6e" value="%s" required>
            </div>
)rawliteral";

const char* htmlFormEnd = R"rawliteral(
            <input type="submit" value="Submit">
        </form>
        <p>You must power cycle the device to take effect.</p>
        <p>Last message received: %s</p>
    </div>
</body>

</html>
)rawliteral";

char temp[600];

void web_handle_root()
{
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html", htmlFormStart);

  snprintf(temp, sizeof(temp), htmlHostname, setting_info.hostname);
  server.sendContent(temp);

  if (strlen(setting_info.mqtt_broker) == 0)
    snprintf(temp, sizeof(temp), htmlMqttBroker, MQTT_SUGGEST_BROKER_DEFAULT);
  else
    snprintf(temp, sizeof(temp), htmlMqttBroker, setting_info.mqtt_broker);
  server.sendContent(temp);

  snprintf(temp, sizeof(temp), htmlMqttUser, setting_info.mqtt_user);
  server.sendContent(temp);

  server.sendContent(htmlMqttPassword);

  snprintf(temp, sizeof(temp), htmlMqttPort, setting_info.mqtt_port);
  server.sendContent(temp);

  snprintf(temp, sizeof(temp), htmlMqttTopic, setting_info.mqtt_topic);
  server.sendContent(temp);

  snprintf(temp, sizeof(temp), htmlMqttPumpTopic, setting_info.mqtt_pump_topic);
  server.sendContent(temp);  

  snprintf(temp, sizeof(temp), htmlMqttSWGTopic, setting_info.mqtt_swg_topic);
  server.sendContent(temp);

  snprintf(temp, sizeof(temp), htmlMqttORAlarmPTopic, setting_info.mqtt_orp_alarm_topic);
  server.sendContent(temp);

  snprintf(temp, sizeof(temp), htmlOrpCalmV, setting_info.orp_cal_mV);
  server.sendContent(temp);

  snprintf(temp, sizeof(temp), htmlSWGCtrl, setting_info.swg_enable ? "checked" : "");
  server.sendContent(temp);

  snprintf(temp, sizeof(temp), htmlSWGTgt, setting_info.swg_orp_target);
  server.sendContent(temp);

  snprintf(temp, sizeof(temp), htmlSWGHyst, setting_info.swg_orp_hysteresis);
  server.sendContent(temp);

  snprintf(temp, sizeof(temp), htmlSWGStdDev, setting_info.swg_orp_std_dev);
  server.sendContent(temp);

  snprintf(temp, sizeof(temp), htmlSWGInterval, setting_info.swg_orp_interval);
  server.sendContent(temp);

  snprintf(temp, sizeof(temp), htmlSWGTime, setting_info.swg_data_sample_time_sec);
  server.sendContent(temp);

  snprintf(temp, sizeof(temp), htmlSWGPct0,
           setting_info.swg_orp_target - setting_info.swg_orp_hysteresis - (setting_info.swg_orp_interval * 1) + 1,
           setting_info.swg_orp_target - setting_info.swg_orp_hysteresis - (setting_info.swg_orp_interval * 0),
           setting_info.swg_orp_pct[0]);
  server.sendContent(temp);
  snprintf(temp, sizeof(temp), htmlSWGPct1,
           setting_info.swg_orp_target - setting_info.swg_orp_hysteresis - (setting_info.swg_orp_interval * 2) + 1,
           setting_info.swg_orp_target - setting_info.swg_orp_hysteresis - (setting_info.swg_orp_interval * 1),
           setting_info.swg_orp_pct[1]);
  server.sendContent(temp);
  snprintf(temp, sizeof(temp), htmlSWGPct2,
           setting_info.swg_orp_target - setting_info.swg_orp_hysteresis - (setting_info.swg_orp_interval * 3) + 1,
           setting_info.swg_orp_target - setting_info.swg_orp_hysteresis - (setting_info.swg_orp_interval * 2),
           setting_info.swg_orp_pct[2]);
  server.sendContent(temp);
  snprintf(temp, sizeof(temp), htmlSWGPct3,
           setting_info.swg_orp_target - setting_info.swg_orp_hysteresis - (setting_info.swg_orp_interval * 4) + 1,
           setting_info.swg_orp_target - setting_info.swg_orp_hysteresis - (setting_info.swg_orp_interval * 3),
           setting_info.swg_orp_pct[3]);
  server.sendContent(temp);
  snprintf(temp, sizeof(temp), htmlSWGPct4,
           setting_info.swg_orp_target - setting_info.swg_orp_hysteresis - (setting_info.swg_orp_interval * 5),
           setting_info.swg_orp_target - setting_info.swg_orp_hysteresis - (setting_info.swg_orp_interval * 4),
           setting_info.swg_orp_pct[4]);
  server.sendContent(temp);

  char val1[40];
  char val2[40];
  int hr, minute, second;
  int val;

  snprintf(temp, sizeof(temp), htmlMqttDTTopic, setting_info.mqtt_datetime_topic);
  server.sendContent(temp);

  val = setting_info.start_schedule[0]; hr = val / (60*60); val -= hr * 60 * 60; minute = val / 60; val -= minute * 60; second = val;
  sprintf(val1, "%02d:%02d:%02d", hr, minute, second);
  val = setting_info.end_schedule[0]; hr = val / (60*60); val -= hr * 60 * 60; minute = val / 60; val -= minute * 60; second = val;
  sprintf(val2, "%02d:%02d:%02d", hr, minute, second);
  snprintf(temp, sizeof(temp), htmlSchedule0, val1, val2); server.sendContent(temp);

  val = setting_info.start_schedule[1]; hr = val / (60*60); val -= hr * 60 * 60; minute = val / 60; val -= minute * 60; second = val;
  sprintf(val1, "%02d:%02d:%02d", hr, minute, second);
  val = setting_info.end_schedule[1]; hr = val / (60*60); val -= hr * 60 * 60; minute = val / 60; val -= minute * 60; second = val;
  sprintf(val2, "%02d:%02d:%02d", hr, minute, second);
  snprintf(temp, sizeof(temp), htmlSchedule1, val1, val2); server.sendContent(temp);

  val = setting_info.start_schedule[2]; hr = val / (60*60); val -= hr * 60 * 60; minute = val / 60; val -= minute * 60; second = val;
  sprintf(val1, "%02d:%02d:%02d", hr, minute, second);
  val = setting_info.end_schedule[2]; hr = val / (60*60); val -= hr * 60 * 60; minute = val / 60; val -= minute * 60; second = val;
  sprintf(val2, "%02d:%02d:%02d", hr, minute, second);
  snprintf(temp, sizeof(temp), htmlSchedule2, val1, val2); server.sendContent(temp);

  val = setting_info.start_schedule[3]; hr = val / (60*60); val -= hr * 60 * 60; minute = val / 60; val -= minute * 60; second = val;
  sprintf(val1, "%02d:%02d:%02d", hr, minute, second);
  val = setting_info.end_schedule[3]; hr = val / (60*60); val -= hr * 60 * 60; minute = val / 60; val -= minute * 60; second = val;
  sprintf(val2, "%02d:%02d:%02d", hr, minute, second);
  snprintf(temp, sizeof(temp), htmlSchedule3, val1, val2); server.sendContent(temp);

  val = setting_info.start_schedule[4]; hr = val / (60*60); val -= hr * 60 * 60; minute = val / 60; val -= minute * 60; second = val;
  sprintf(val1, "%02d:%02d:%02d", hr, minute, second);
  val = setting_info.end_schedule[4]; hr = val / (60*60); val -= hr * 60 * 60; minute = val / 60; val -= minute * 60; second = val;
  sprintf(val2, "%02d:%02d:%02d", hr, minute, second);
  snprintf(temp, sizeof(temp), htmlSchedule4, val1, val2); server.sendContent(temp);

  val = setting_info.start_schedule[5]; hr = val / (60*60); val -= hr * 60 * 60; minute = val / 60; val -= minute * 60; second = val;
  sprintf(val1, "%02d:%02d:%02d", hr, minute, second);
  val = setting_info.end_schedule[5]; hr = val / (60*60); val -= hr * 60 * 60; minute = val / 60; val -= minute * 60; second = val;
  sprintf(val2, "%02d:%02d:%02d", hr, minute, second);
  snprintf(temp, sizeof(temp), htmlSchedule5, val1, val2); server.sendContent(temp);

  val = setting_info.start_schedule[6]; hr = val / (60*60); val -= hr * 60 * 60; minute = val / 60; val -= minute * 60; second = val;
  sprintf(val1, "%02d:%02d:%02d", hr, minute, second);
  val = setting_info.end_schedule[6]; hr = val / (60*60); val -= hr * 60 * 60; minute = val / 60; val -= minute * 60; second = val;
  sprintf(val2, "%02d:%02d:%02d", hr, minute, second);
  snprintf(temp, sizeof(temp), htmlSchedule6, val1, val2); server.sendContent(temp);

  snprintf(temp, sizeof(temp), htmlFormEnd, receivedMessage.c_str());
  server.sendContent(temp);

  server.sendContent(F(""));
}

int schedule_get_second(const char *msg)
{
  if (strlen(msg) < 8)
    return -1;
  if (!isdigit(msg[0]) && !isdigit(msg[1]) && msg[2] != ':')
    return -1;
  int hour = (msg[0] - '0') * 10 + msg[1] - '0';
  if (!isdigit(msg[3]) && !isdigit(msg[4]) && msg[5] != ':')
    return -1;
  int minute = (msg[3] - '0') * 10 + msg[4] - '0';
  if (!isdigit(msg[6]) && !isdigit(msg[7]))
    return -1;
  int second = (msg[6] - '0') * 10 + msg[7] - '0';
  return (hour * 60 * 60) + (minute * 60) + second;
}

void web_handle_mqtt_submit()
{
  receivedMessage = "";
  if (server.hasArg("hostname")) {
    receivedMessage += server.arg("hostname");
    strncpy(setting_info.hostname, server.arg("hostname").c_str(), 64);
    setting_info.hostname[63] = 0;
  }
  if (server.hasArg("broker")) {
    receivedMessage += " ";
    receivedMessage = server.arg("broker");
    strncpy(setting_info.mqtt_broker, server.arg("broker").c_str(), 64);
    setting_info.mqtt_broker[63] = 0;
  }
  if (server.hasArg("username")) {
    receivedMessage += " ";
    receivedMessage += server.arg("username");
    strncpy(setting_info.mqtt_user, server.arg("username").c_str(), 64);
    setting_info.mqtt_user[63] = 0;
  }
  if (server.hasArg("password")) {
    receivedMessage += " ";
    receivedMessage += server.arg("password");
    strncpy(setting_info.mqtt_password, server.arg("password").c_str(), 64);
    setting_info.mqtt_password[63] = 0;
  }
  if (server.hasArg("port")) {
    receivedMessage += " ";
    receivedMessage += server.arg("port");
    setting_info.mqtt_port = atoi(server.arg("port").c_str());
  }
  if (server.hasArg("topic")) {
    receivedMessage += " ";
    receivedMessage += server.arg("topic");
    strncpy(setting_info.mqtt_topic, server.arg("topic").c_str(), 64);
    setting_info.mqtt_topic[63] = 0;
  }
  if (server.hasArg("pumptopic")) {
    receivedMessage += " ";
    receivedMessage += server.arg("pumptopic");
    strncpy(setting_info.mqtt_pump_topic, server.arg("pumptopic").c_str(), 64);
    setting_info.mqtt_pump_topic[63] = 0;
  }
  if (server.hasArg("swgtopic")) {
    receivedMessage += " ";
    receivedMessage += server.arg("swgtopic");
    strncpy(setting_info.mqtt_swg_topic, server.arg("swgtopic").c_str(), 64);
    setting_info.mqtt_swg_topic[63] = 0;
  }
  if (server.hasArg("orpcal")) {
    receivedMessage += " ";
    receivedMessage += server.arg("orpcal");
    setting_info.orp_cal_mV = atoi(server.arg("orpcal").c_str());
  }

  if (server.hasArg("swgtarget")) {
    receivedMessage += " ";
    receivedMessage += server.arg("swgtarget");
    setting_info.swg_orp_target = atoi(server.arg("swgtarget").c_str());
  }
  if (server.hasArg("swghysteresis")) {
    receivedMessage += " ";
    receivedMessage += server.arg("swghysteresis");
    setting_info.swg_orp_hysteresis = atoi(server.arg("swghysteresis").c_str());
  }
  if (server.hasArg("swgstddev")) {
    receivedMessage += " ";
    receivedMessage += server.arg("swgstddev");
    setting_info.swg_orp_std_dev = atof(server.arg("swgstddev").c_str());
  }
  if (server.hasArg("swginterval")) {
    receivedMessage += " ";
    receivedMessage += server.arg("swginterval");
    setting_info.swg_orp_interval = atoi(server.arg("swginterval").c_str());
  }
  if (server.hasArg("swgtime")) {
    receivedMessage += " ";
    receivedMessage += server.arg("swgtime");
    setting_info.swg_data_sample_time_sec = atoi(server.arg("swgtime").c_str());
  }
  for (int i = 0; i < 5; i++) {
    char tag[40];
    sprintf(tag, "swgpct%d", i);
    if (server.hasArg(tag)) {
      receivedMessage += " ";
      receivedMessage += server.arg(tag);
      setting_info.swg_orp_pct[i] = atoi(server.arg(tag).c_str());
    }
  }
  if (server.hasArg("swgctrl")) {
    receivedMessage += " SWG Enable";
    setting_info.swg_enable = 1;
  } else {
    receivedMessage += " SWG Disable";
    setting_info.swg_enable = 0;
  }

  if (server.hasArg("orpalarmtopic")) {
    receivedMessage += " ";
    receivedMessage += server.arg("orpalarmtopic");
    strncpy(setting_info.mqtt_orp_alarm_topic, server.arg("orpalarmtopic").c_str(), 64);
    setting_info.mqtt_orp_alarm_topic[63] = 0;
  }

  for (int i = 0; i < 7; i++) {
    char tag0[40];
    char tag1[40];
    sprintf(tag0, "schedule%d", i);
    sprintf(tag1, "schedule%de", i);
    if (server.hasArg(tag0) && server.hasArg(tag1)) {
      receivedMessage += " ";
      receivedMessage += server.arg(tag0);
      receivedMessage += " ";
      receivedMessage += server.arg(tag1);
      int ts = schedule_get_second(server.arg(tag0).c_str());
      int te = schedule_get_second(server.arg(tag1).c_str());
      if (ts >= 0 &&  te >= 0 && ts < te) {
        setting_info.start_schedule[i] = ts;
        setting_info.end_schedule[i] = te;
      }
    }
  }

  DBG_PRINT("Received message: ");
  DBG_PRINTLN(receivedMessage);
  system_setting_save();

  // Redirect back to the main page after submission
  server.sendHeader("Location", "/");
  server.send(303); // Use 303 for "See Other" to prevent re-submission on refresh

  orp_data_setup();

  mqtt_client.stop();
  mqtt_connect_first_time = 1;
}

void web_setup()
{
  server.on("/", HTTP_GET, web_handle_root);
  server.on("/submit", HTTP_POST, web_handle_mqtt_submit); // Handle POST requests to /submit
  server.begin();
  DBG_PRINTLN("HTTP server started");
}

void mqtt_msg_recv(int messageSize)
{
  unsigned char msg[80];
  char topic[128];

  strncpy(topic, mqtt_client.messageTopic().c_str(), sizeof(topic));
  topic[sizeof(topic)-1] = '\0';
  MQTT_PRINT("MQTT: \"");
  MQTT_PRINT(topic);
  MQTT_PRINT("\"");
  int total = mqtt_client.read(msg, sizeof(msg) - 1);
  msg[total] = '\0';
  MQTT_PRINT(" \"");
  MQTT_PRINT((char *) msg);
  MQTT_PRINTLN("\"");
  if (strlen(mqtt_pump_topic_match) > 0 &&
      strstr(topic, mqtt_pump_topic_match) != NULL) {
    if (strcmp(topic, mqtt_pump_topic_match) == 0) {
      if (strcmp((char *) msg, "0") == 0) {
        mqtt_pump_state = 0;
      } else if (strcmp((char *) msg, "1") == 0) {
        mqtt_pump_state = 1;
      }
    }  
    // else if (strstr(topic, "/RPM") != NULL) {
    //   if (atoi((char *) msg) <= 100)
    //     mqtt_pump_state = 0;
    // } else if (strstr(topic, "/Watts") != NULL) {
    //   if (atoi((char *) msg) <= 100)
    //     mqtt_pump_state = 0;
    // }
  }
  if (strlen(setting_info.mqtt_swg_topic) > 0 &&
      strcasecmp(topic, setting_info.mqtt_swg_topic) == 0) {
    mqtt_swg_pct = atoi((char *) msg);
    if (mqtt_swg_pct < 0)
      mqtt_swg_pct = -1;
    else if (mqtt_swg_pct > 100)
      mqtt_swg_pct = -1;
  }

  if (strlen(setting_info.mqtt_datetime_topic) > 0 &&
      strcasecmp(topic, setting_info.mqtt_datetime_topic) == 0) {
      uint32_t val = ntohl(*(uint32_t *) msg);
    setTime(val);
  }
}

void mqtt_setup()
{
  mqtt_client.setId("ORP");
  mqtt_client.onMessage(mqtt_msg_recv);
}

IPAddress mqtt_addr(0, 0, 0, 0);
const IPAddress no_addr(0, 0, 0, 0);

void mqtt_loop()
{
  if (WiFi.status() != WL_CONNECTED)
    return;

  if (op_state != OP_NORMAL)
    return;

  if (strlen(setting_info.mqtt_broker) == 0) {
    return;
  }

  if (mqtt_client.connected()) {
    mqtt_client.poll();
  }

  if (mqtt_connect_first_time && !mqtt_client.connected()) {
    op_state = OP_MQTT_CONNECT;
    oled_screen_refresh = 1;
    return;
  }
  
  if (!mqtt_client.connected() && ((millis() - mqtt_connect_ts) >= 60000)) {
    op_state = OP_MQTT_CONNECT;
    oled_screen_refresh = 1;
    return;
  }
}

int mqtt_connect()
{
  if (!mqtt_client.connected()) {
    mqtt_connect_first_time = 0;
    mqtt_subscribed = 0;
    if (mqtt_addr == no_addr) {
      set_status_msg("MQTT resolving");
      oled_screen_refresh = 1;
      oled_update_mqtt_connect(1);
      int result = WiFi.hostByName(setting_info.mqtt_broker, mqtt_addr);
      if (result != 1) {
        set_status_msg("MQTT resolve failed");
        oled_update_mqtt_connect(1);
        mqtt_connect_ts = millis();
        return 1;
      }
    }
    if (mqtt_addr != no_addr) {
      set_status_msg("MQTT connecting");
      oled_screen_refresh = 1;
      oled_update_mqtt_connect(1);
      mqtt_client.setUsernamePassword(setting_info.mqtt_user, setting_info.mqtt_password);
      if (mqtt_client.connect(mqtt_addr, setting_info.mqtt_port)) {
        set_status_msg("MQTT connected");
        oled_screen_refresh = 1;
        oled_update_mqtt_connect(1);
        return 0;
      } else {
        set_status_msg("MQTT connect failed");
        oled_screen_refresh = 1;
        oled_update_mqtt_connect(1);
        mqtt_connect_ts = millis();
        return 1;
      }
    } else {
      mqtt_connect_ts = millis();
      return 1;
    }
  } else if (!mqtt_subscribed) {
    if (strlen(setting_info.mqtt_pump_topic) > 0) {
      mqtt_client.subscribe(setting_info.mqtt_pump_topic);
    } else {
      // If no pump topic, assume pump is on.
      mqtt_pump_state = 1;
    }
    if (strlen(setting_info.mqtt_swg_topic) > 0) {
      mqtt_client.subscribe(setting_info.mqtt_swg_topic);
    }
    if (strlen(setting_info.mqtt_datetime_topic) > 0) {
      mqtt_client.subscribe(setting_info.mqtt_datetime_topic);
    }
    set_status_msg("MQTT subscribed");
    oled_screen_refresh = 1;
    oled_update_mqtt_connect(1);
    mqtt_subscribed = 1;
    if (!is_orp_reading_expired()) {
      if (mqtt_is_pump_on())
        mqtt_publish(orp_reading);
      else
        mqtt_publish(0.0);
    }
    //
    // Create ORP alarm
    mqtt_swg_alarm_create();
    return 1;
  } else {
    return 1;
  }
}

unsigned long mqtt_publish_ts = 0;
float mqtt_publish_orp = -1;

void mqtt_publish(float orp)
{
  char msg[40];

  // Only publish every 15 seconds if it is the same value
  if (mqtt_publish_orp == orp) {
    if ((millis() - mqtt_publish_ts) <= 15000) {
      return;
    }
  }

  sprintf(msg, "%0.1f", orp);
  mqtt_client.beginMessage(setting_info.mqtt_topic);
  mqtt_client.print(msg);
  mqtt_client.endMessage();
  MQTT_PRINT("MQTT: ORP ");
  MQTT_PRINTLN(msg);
  mqtt_publish_orp = orp;
  mqtt_publish_ts = millis();
}

void mqtt_swg_publish(int pct)
{
  char msg_topic[80];
  char msg[80];

  if (strlen(setting_info.mqtt_swg_topic) <= 0) {
    return;
  }

  if (!setting_info.swg_enable) {
    return;
  }

  if (strstr(setting_info.mqtt_swg_topic, "set") == NULL)
    sprintf(msg_topic, "%s/set", setting_info.mqtt_swg_topic);
  else
    sprintf(msg_topic, "%s", setting_info.mqtt_swg_topic);
  sprintf(msg, "%d", pct);
  mqtt_client.beginMessage(msg_topic);
  mqtt_client.print(msg);
  mqtt_client.endMessage();
  MQTT_PRINT("MQTT: SWG ");
  MQTT_PRINTLN(msg);
}

void mqtt_swg_alarm_create()
{
  char msg[80];

  if (strlen(setting_info.mqtt_orp_alarm_topic) <= 0) {
    return;
  }

  sprintf(msg, "%s/to/add", setting_info.mqtt_orp_alarm_topic);
  mqtt_client.beginMessage(msg);
  mqtt_client.print("{\"name\": \"ORP Alarm\", \"service_name\": \"ORP Alarm\", \"service\": \"Switch\"}");
  mqtt_client.endMessage();
}

void mqtt_orp_alarm_publish(int alarm)
{
  char msg_topic[80];

  if (strlen(setting_info.mqtt_orp_alarm_topic) <= 0) {
    return;
  }

  sprintf(msg_topic, "%s/to/set", setting_info.mqtt_orp_alarm_topic);
  mqtt_client.beginMessage(msg_topic);
  if (alarm)
      mqtt_client.print("{\"name\": \"ORP Alarm\", \"service_name\": \"ORP Alarm\", \"characteristic\": \"On\", \"value\": true}");
  else
      mqtt_client.print("{\"name\": \"ORP Alarm\", \"service_name\": \"ORP Alarm\", \"characteristic\": \"On\", \"value\": false}");
  mqtt_client.endMessage();
}

void web_loop()
{
  server.handleClient();
}

int mqtt_is_subscribed()
{
  if (mqtt_subscribed && mqtt_client.connected())
    return 1;
  return 0;
}

int mqtt_is_pump_on()
{
  if (mqtt_pump_state == 1)
    return 1;
  return 0;
}

int mdns_loop()
{
  if (!is_wifi_connected())
   return 0;

  if (mdns_ready) {
    mdns.run();
    return 1;
  }

  if ((millis() - mdns_ready_ts) < 5000) {
    return 0;
  }

  if (!mdns.begin(WiFi.localIP(), setting_info.hostname)) {
    set_status_msg("mDNS failed");
    mdns_ready_ts = millis();
    return 0;
  }
  mdns_ready = 1;
  mdns.addServiceRecord("http", HTTP_PORT, MDNSServiceTCP);
  return 1;
}
#else
int wifi_loop() { return 1; }
void mqtt_loop() {}
void web_loop() {}
void wifi_link_chk_loop() {}
int mqtt_is_subscribed() { return 0; }
int mqtt_connect() { return 1; }
int mqtt_is_pump_on() { return 1; }
int mdns_loop() {}
#endif /* WIFI_SUPPORT */

void time_setup()
{
  setSyncInterval(15 * 60);
}

void setup()
{
  serial_setup();
  oled_setup();
  oled_update_normal(1);

  DBG_PRINTLN("ORP Monitor");

  rotary_button_setup();

  time_setup();
  wifi_setup();
  mqtt_setup();
  web_setup();
  orp_data_setup();
}

void loop()
{
  rotary.loop();
  button.loop();
  orp_loop();
  mqtt_loop();
  web_loop();
  mdns_loop();
  orp_swg_ctrl_loop();

  switch (op_state) {
  case OP_MENU:
    oled_update_menu();
    break;
  case OP_ORP_CAL:
    if (orp_cal_loop()) {
      op_state = OP_NORMAL;
      clear_idle_timer();
    }
    oled_update_orp();
    break;
  case OP_WIFI_SETUP:
    if (wifi_loop()) {
      op_state = OP_NORMAL;
      clear_idle_timer();
    }
    oled_update_wifi();
    break;
  case OP_MQTT_CONNECT:
    if (mqtt_connect()) {
      op_state = OP_NORMAL;
      clear_idle_timer();
    }
    break;
  case OP_SETTING_INFO:
    if (oled_update_setting_info()) {
      op_state = OP_NORMAL;
      clear_idle_timer();
    }
    break;
  default:
    oled_update_normal(0);
    break;
  }
  
  oled_idle_check();
  check_status_msg_expired();
  wifi_link_chk_loop();
}
