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

#define DBG_SUPPORT 1     /* Print general debug information */
//#define DBG_STATUS 1    /* Print equivalent of menu on serail port */
//#define DBG_MQTT 1      /* Print MQTT messages */
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

typedef struct {
  uint32_t signature1;
  uint32_t signature2;
  char ssid[64];
  char password[64];
  char mqtt_broker[64];
  char mqtt_topic[64];
  char mqtt_user[64];
  char mqtt_password[64];
  char hostname[64];
  int mqtt_port;
  int orp_cal_mV;
} Setting_Info;

#define SIGNATURE1    0x57494649
#define SIGNATURE2    0x00000002
#define MQTT_BROKER_DEFAULT   ""
#define MQTT_SUGGEST_BROKER_DEFAULT   "aqualink.local"
#define MQTT_TOPIC_DEFAULT    "aqualinkd/CHEM/ORP/set"
#define MQTT_USER_DEFAULT     "pi"
#define MQTT_PORT_DEFAULT     1883
#define HOSTNAME_DEFAULT      "ORP"
#define ORP_CAL_MV_DEFAULT    225
Setting_Info setting_info;

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
#endif /* WIFI_SUPPORT */

unsigned long mqtt_connect_ts = 0;
int mqtt_connect_first_time = 1;

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
}

void clear_status_msg()
{
  status_msg[0] = 0;
  status_msg_ts = 0;
}

void check_status_msg_expired()
{
  if (status_msg[0] != 0 && (millis() - status_msg_ts) > 30000) {
    clear_status_msg();
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
  if ((millis() - ord_ts) > 3000)
    return 1;
  return 0;
}

void oled_orp_update()
{
  u8g2.setFont(u8g2_font_helvB08_tf);
  u8g2.drawStr(0, 12, "CAL");
  if (orp_caled != 1) {
    u8g2.drawLine(2, 0, 18, 15);
    u8g2.drawLine(3, 0, 19, 15);
    u8g2.drawLine(2, 15, 18, 0);
    u8g2.drawLine(3, 15, 19, 0);
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

void oled_title_update(int show_ip = 1)
{
  u8g2.setFont(u8g2_font_crox5tb_tf);
  u8g2.drawStr(8 * 5, 16, "ORP");

  oled_orp_update();
  oled_wifi_update(show_ip);
}

void oled_status_alive_update(int show_status = 1)
{
  oled_alive_update();
  
  if (show_status && strlen(status_msg) > 0) {
    u8g2.setFont(u8g2_font_helvB08_tf);
    u8g2.drawStr(0, 63, status_msg);
  }
}

void oled_title_update_print()
{
  if (orp_caled == 1) {
    STATUS_PRINTLN("CAL");
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
  char msg[40];

  if (now == 0 && (millis() - oled_refresh_ts) <= 500)
    return;
  if (oled_idle) {
    return;
  }
  oled_refresh_ts = millis();

  oled_title_update_print();
  if (is_orp_reading_expired())
    sprintf(msg, "----mV");
  else
    sprintf(msg, "%0.1fmV", orp_reading);
  STATUS_PRINTLN(msg);
  oled_status_alive_update_print();

  if (!oled_detected)
    return;

  u8g2.clearBuffer();
  oled_title_update();
  oled_status_alive_update();

  u8g2.setFont(u8g2_font_ncenB18_tf);
  int x = (u8g2.getDisplayWidth()-10 - u8g2.getStrWidth(msg)) / 2;
  u8g2.drawButtonUTF8(5 + x, 40, U8G2_BTN_BW0, u8g2.getDisplayWidth()-5*2,  0,  0, msg);

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
  sprintf(msg, "RSSI %d", WiFi.RSSI());
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
    return;
  }

  clear_idle_timer();
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

void wifi_setting_clear()
{
  memset((void *) &setting_info, 0, sizeof(setting_info));
  strcpy(setting_info.mqtt_broker, MQTT_BROKER_DEFAULT);
  strcpy(setting_info.mqtt_topic, MQTT_TOPIC_DEFAULT);
  strcpy(setting_info.mqtt_user, MQTT_USER_DEFAULT);
  strcpy(setting_info.hostname, HOSTNAME_DEFAULT);
  setting_info.mqtt_port = MQTT_PORT_DEFAULT;
  setting_info.orp_cal_mV = ORP_CAL_MV_DEFAULT;
  setting_info.signature1 = SIGNATURE1;
  setting_info.signature2 = SIGNATURE2;
}

void wifi_setting_init()
{
#if defined(ARDUINO_SEEED_XIAO_M0)
  setting_info = setting_info_storage.read();
  if (setting_info.signature1 != SIGNATURE1 &&
      setting_info.signature2 != SIGNATURE2) {
    wifi_setting_clear();
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
  str = prefs.getString("MQTTUSER", MQTT_USER_DEFAULT);
  strcpy(setting_info.mqtt_user, str.c_str());
  str = prefs.getString("MQTTPW", "");
  strcpy(setting_info.mqtt_password, str.c_str());
  str = prefs.getString("HOSTNAME", HOSTNAME_DEFAULT);
  strcpy(setting_info.hostname, str.c_str());
  setting_info.mqtt_port = prefs.getInt("MQTTPORT", MQTT_PORT_DEFAULT);
  setting_info.orp_cal_mV = prefs.getInt("ORPCALMV", ORP_CAL_MV_DEFAULT);
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
  DBG_PRINTLN(setting_info.orp_cal_mV);
}

void wifi_setting_save()
{
#if defined(ARDUINO_SEEED_XIAO_M0)
  setting_info_storage.write(setting_info);
#else
  prefs.putString("SSID", setting_info.ssid);
  prefs.putString("PW", setting_info.password);
  prefs.putString("MQTTBROKER", setting_info.mqtt_broker);
  prefs.putString("MQTTTOPIC", setting_info.mqtt_topic);
  prefs.putString("MQTTUSER", setting_info.mqtt_user);
  prefs.putString("MQTTPW", setting_info.mqtt_password);
  prefs.putInt("MQTTPORT", setting_info.mqtt_port);
  prefs.putString("HOSTNAME", setting_info.hostname);
  prefs.putInt("ORPCALMV", setting_info.orp_cal_mV);
#endif
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
  wifi_setting_init();
  wifi_init();
}
#else
void wifi_setup() { }
bool is_wifi_connected() { return 0; }
#endif /* WIFI_SUPPORT */

///////////////////////////////////////////////////////////////////////////////
// ORP Function
///////////////////////////////////////////////////////////////////////////////
void orp_loop()
{
//#define SIM_ORP_TEST  1
#if defined(SIM_ORP_TEST)
  if ((millis() - ord_ts) >= 3000) {
    ord_ts = millis();
    orp_caled = 1;
    if (orp_reading == 0.0) {
      orp_reading = 225;
    } else if (orp_reading >= 800) {
      orp_reading = 225;
    } else {
      orp_reading++;
    }
#if defined(WIFI_SUPPORT)
    if ((millis() - mqtt_orp_publish_ts) >= ORP_PUBLISH_TIMEMS) {
      mqtt_publish(orp_reading);
      mqtt_orp_publish_ts = millis();
    }
#endif
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
    }
    orp_line_cnt = 0;
    break;
  default:
    if (orp_caled < 0) {
      if (strncmp(orp_line, "?CAL,0", 6) == 0)
        orp_caled = 0;
      if (strncmp(orp_line, "?CAL,1", 6) == 0)
        orp_caled = 1;
    }
    
    if (!isdigit(orp_line[0])) {
      orp_line_cnt = 0;
      break;
    }
    orp_line[orp_line_cnt] = 0;
    orp_reading = atof(orp_line);
    char msg[40];
    sprintf(msg, "ORP: %0.1f", orp_reading);
    ORP_PRINTLN(msg);
    ord_ts = millis();
    orp_line_cnt = 0;
#if defined(WIFI_SUPPORT)
    if ((millis() - mqtt_orp_publish_ts) >= ORP_PUBLISH_TIMEMS) {
      mqtt_publish(orp_reading);
      mqtt_orp_publish_ts = millis();
    }
#endif
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
    wifi_setting_clear();
    wifi_setting_save();
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
      wifi_setting_save();
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

void wifi_rssi_update()
{
  rssi = WiFi.RSSI();
}

/////////////////////////////////////////////////////////////////////
// MQTT Functions
/////////////////////////////////////////////////////////////////////
#include <WebServer.h>
#include <ArduinoMqttClient.h>

WiFiClient wifi_client;
MqttClient mqtt_client(wifi_client);
bool mqtt_subscribed = 0;

WebServer server(80);
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
        label {
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
    <div class="form-container">
        <h1 >ORP Configuration</h1>
        <form action="/submit" method="POST">
)rawliteral";

const char* htmlHostname = R"rawliteral(
            <label for="hostname">Host Name:</label>
            <input type="text" id="hostname" name="hostname" value="%s" required>
)rawliteral";

const char* htmlMqttBroker = R"rawliteral(
            <label for="broker">MQTT Server:</label>
            <input type="text" id="broker" name="broker" value="%s" required>
)rawliteral";

const char* htmlMqttUser = R"rawliteral(
            <label for="username">MQTT User:</label>
            <input type="text" id="username" name="username" value="%s" required>
)rawliteral";

const char* htmlMqttPassword = R"rawliteral(
            <label for="password">MQTT Password:</label>
            <input type="password" id="password" name="password" required>
)rawliteral";

const char* htmlMqttPort = R"rawliteral(
            <label for="port">MQTT Port:</label>
            <input type="text" id="port" name="port" value="%d" required>
)rawliteral";

const char* htmlMqttTopic = R"rawliteral(
            <label for="topic">MQTT Topic:</label>
            <input type="text" id="topic" name="topic" value="%s" required>
)rawliteral";

const char* htmlOrpCalmV = R"rawliteral(
            <label for="orpcal">ORP Calibration mV:</label>
            <input type="text" id="orpcal" name="orpcal" value="%d" required>
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
  
void web_handle_root()
{
  char temp[400];
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

  snprintf(temp, sizeof(temp), htmlOrpCalmV, setting_info.orp_cal_mV);
  server.sendContent(temp);

  snprintf(temp, sizeof(temp), htmlFormEnd, receivedMessage.c_str());
  server.sendContent(temp);

  server.sendContent(F(""));
}

void web_handle_mqtt_submit()
{
  receivedMessage = "";
  if (server.hasArg("hostname")) {
    receivedMessage = server.arg("hostname");
    strncpy(setting_info.hostname, server.arg("hostname").c_str(), 64);
    setting_info.hostname[63] = 0;
  }
  if (server.hasArg("broker")) {
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
  if (server.hasArg("orpcal")) {
    receivedMessage += " ";
    receivedMessage += server.arg("orpcal");
    setting_info.orp_cal_mV = atoi(server.arg("orpcal").c_str());
  }

  DBG_PRINT("Received message: ");
  DBG_PRINTLN(receivedMessage);
  wifi_setting_save();

  // Redirect back to the main page after submission
  server.sendHeader("Location", "/");
  server.send(303); // Use 303 for "See Other" to prevent re-submission on refresh

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
    return;
  }
  
  if (!mqtt_client.connected() && ((millis() - mqtt_connect_ts) >= 60000)) {
    op_state = OP_MQTT_CONNECT;
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
      oled_update_mqtt_connect(1);
      mqtt_client.setUsernamePassword(setting_info.mqtt_user, setting_info.mqtt_password);
      if (mqtt_client.connect(mqtt_addr, setting_info.mqtt_port)) {
        set_status_msg("MQTT connected");
        oled_update_mqtt_connect(1);
        return 0;
      } else {
        set_status_msg("MQTT connect failed");
        oled_update_mqtt_connect(1);
        mqtt_connect_ts = millis();
        return 1;
      }
    } else {
      mqtt_connect_ts = millis();
      return 1;
    }
  } else if (!mqtt_subscribed) {
    //mqtt_client.subscribe(setting_info.mqtt_topic);
    set_status_msg("MQTT subscribed");
    oled_update_mqtt_connect(1);
    mqtt_subscribed = 1;
    mqtt_publish(orp_reading);
    return 1;
  } else {
    return 1;
  }
}

void mqtt_publish(float orp)
{
  char msg[40];
  sprintf(msg, "%0.1f", orp);
  mqtt_client.beginMessage(setting_info.mqtt_topic);
  mqtt_client.print(msg);
  mqtt_client.endMessage();
  MQTT_PRINT("MQTT: ");
  MQTT_PRINTLN(msg);
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

#else
int wifi_loop() { return 1; }
void mqtt_loop() {}
void web_loop() {}
void wifi_rssi_update() {}
int mqtt_is_subscribed() { return 0; }
int mqtt_connect() { return 1; }
#endif /* WIFI_SUPPORT */

void setup()
{
  serial_setup();
  oled_setup();
  oled_update_normal(1);

  DBG_PRINTLN("ORP Monitor");

  rotary_button_setup();

  wifi_setup();
  mqtt_setup();
  web_setup();
}

void loop()
{
  rotary.loop();
  button.loop();
  orp_loop();
  mqtt_loop();
  web_loop();

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
  wifi_rssi_update();
}
