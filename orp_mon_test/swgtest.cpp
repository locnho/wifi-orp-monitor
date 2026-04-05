/*
 * SWG Analyzer Validation Code
 *
 * To build under Windows with Visual Studio cl.exe
 * 
 * C:\cl /EHsc /I "." /I "../orp_mon_wifi" swgtest.cpp Time.cpp ../orp_mon_wifi/swganalyzer.cpp
 *
 */
#define _CRT_SECURE_NO_WARNINGS
 #include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include "TimeLib.h"
#include "swganalyzer.h"
#include <chrono>
#include <ctime>

using namespace std;

SWGAnalyzerv2 swg_anlyzer;
vector<vector<string>> orp_data;

int read_data()
{
    ifstream file("orp_data.csv");
    if (!file.is_open()) {
        cerr << "Error opening file!" << endl;
        return -1;
    }

    string line;
    // Optional: Skip header line
    getline(file, line); 

    while (getline(file, line)) {
        stringstream ss(line);
        string cell;
        vector<string> row;

        while (getline(ss, cell, ',')) {
            row.push_back(cell);
        }
        orp_data.push_back(row);
    }
    file.close();
    return 0;
}

unsigned long sim_time_sec = 0;
unsigned long my_millis()
{
  return sim_time_sec * 1000;
}

int main(void)
{
  char tstamp[80];
  swg_anlyzer.set_millis_cb(my_millis);
  swg_anlyzer.setup();
  swg_anlyzer.setup_alg(695, 3, 3 * 24, 5);
  for (int i = 0; i < 7; i++) {
    swg_anlyzer.set_schedule(i, 0, 60*60*24);
  }
  swg_anlyzer.enable_datetime_check(0);

  if (read_data() < 0)
    return -1;

  struct tm time_start_dt = {}; // Initialize to zero
  time_start_dt.tm_year = 2026 - 1900; // 2026
  time_start_dt.tm_mon = 3 - 1;        // March
  time_start_dt.tm_mday = 29;          // 29th
  time_start_dt.tm_hour = 0;
  time_start_dt.tm_min = 0;
  time_start_dt.tm_sec = 0;
  time_t time_start = mktime(&time_start_dt);

  for (const auto& element : orp_data) {
    time_t timestamp = stoi(element[0]);
    int time_offset = 7*60*60;
    timestamp -= time_offset;
    struct tm dt_local = *localtime(&timestamp);
    time_t localtstamp = mktime(&dt_local);
    // struct tm dt_utc = *gmtime(&timestamp);
    // time_t utctstamp = mktime(&dt_utc);
    sim_time_sec = (unsigned long) (localtstamp);
    setTime(localtstamp);

    if (localtstamp < time_start)
      continue;

    // cout << dt_local.tm_hour << " " << dt_local.tm_min << " " << dt_local.tm_sec << " " << localtstamp << endl;
    // cout << dt_utc.tm_hour << " " << dt_utc.tm_min << " " << dt_utc.tm_sec << " " << utctstamp << endl;
    // cout << "Time Info " << hour() << " " << minute() << " " << second() << " Wk day " << weekday() - 1 << endl;

    // Limit stamp from 9:00 to 14:59 per day
    if (dt_local.tm_hour < 9 || dt_local.tm_hour >= 15)
      continue;

    unsigned int orp_val = stoi(element[1]);
    unsigned int swg_pct = stoi(element[2]);

    struct tm temp_stamp = {}; // Initialize to zero
    temp_stamp.tm_year = 2026 - 1900; // 4/2/2026
    temp_stamp.tm_mon = 4 - 1;
    temp_stamp.tm_mday = 2;          
    temp_stamp.tm_hour = 9;
    temp_stamp.tm_min = 0;
    temp_stamp.tm_sec = 0;
    time_t sim_swg_start = mktime(&temp_stamp);
    if (localtstamp >= sim_swg_start)
      swg_pct = 75;

    swg_anlyzer.orp_add(orp_val, swg_pct > 0 ? 1 : 0);
    int pct = swg_anlyzer.get_swg_pct(swg_pct > 0 ? 1 : 0);

    strftime(tstamp, sizeof(tstamp), "%Y-%m-%d %H:%M:%S", &dt_local);
    cout << tstamp << " ORP " << stoi(element[1]) << " SWG " << stoi(element[2]) << " PCT " << pct << " ";
    cout << "Avg " << swg_anlyzer.get_orp_day_avg(0) << " " << swg_anlyzer.get_orp_day_avg(1) << " "
                   << swg_anlyzer.get_orp_day_avg(2) << " " << swg_anlyzer.get_orp_day_avg(3) << " "
                   << swg_anlyzer.get_orp_day_avg(4) << " " << swg_anlyzer.get_orp_day_avg(5) << " "
                   << swg_anlyzer.get_orp_day_avg(6) << " ";
    cout << "ORP Target " << swg_anlyzer.get_orp_target() << " "; 
    // cout << "Total " << swg_anlyzer.orp_day_total[0] << " " << swg_anlyzer.orp_day_total[1] << " " << swg_anlyzer.orp_day_total[2] << " " << swg_anlyzer.orp_day_total[3] << " "
    //                << swg_anlyzer.orp_day_total[4] << " " << swg_anlyzer.orp_day_total[5] << " " << swg_anlyzer.orp_day_total[6] << " "; 
    // cout << "Measured Time " << swg_anlyzer.orp_day_measured_time[0] << " " << swg_anlyzer.orp_day_measured_time[1] << " " << swg_anlyzer.orp_day_measured_time[2] << " " << swg_anlyzer.orp_day_measured_time[3] << " "
    //                   << swg_anlyzer.orp_day_measured_time[4] << " " << swg_anlyzer.orp_day_measured_time[5] << " " << swg_anlyzer.orp_day_measured_time[6] << " ";
    // cout << "Wkday " << swg_anlyzer.orp_day_swg_wkday[0] << " " << swg_anlyzer.orp_day_swg_wkday[1] << " " << swg_anlyzer.orp_day_swg_wkday[2] << " " << swg_anlyzer.orp_day_swg_wkday[3] << " "
    //                << swg_anlyzer.orp_day_swg_wkday[4] << " " << swg_anlyzer.orp_day_swg_wkday[5] << " " << swg_anlyzer.orp_day_swg_wkday[6] << endl;  
    // cout << "Delay TS " << int64_t(swg_anlyzer.get_orp_day_delay_ts_ms()) << " Current Time " << my_millis() << " ";
    // cout << "Measure Time " << swg_anlyzer.orp_day_measure_time_ms << " ";
    // cout << "SWG Time " << swg_anlyzer.orp_day_swg_active_time_ms << " Cfg " << swg_anlyzer.orp_day_cfg_swg_time_ms << " ";
    switch (swg_anlyzer.get_orp_day_state()) {
    case ORP_DAY_STATE_INIT:
      cout << "INIT ";
      break;
    case ORP_DAY_STATE_DELAY:
      cout << "DELAY ";
      break;
    case ORP_DAY_STATE_MEASURE:
      cout << "MEASURE ";
      break;
    case ORP_DAY_STATE_SCHEDULE_SWG:
      cout << "SCHEDULE DELAY ";
      break;
    case ORP_DAY_STATE_ACTIVE_SWG:
      cout << "SWG ACTIVE ";
      break;
    }
    switch (swg_anlyzer.get_orp_day_reason_code()) {
    case ORP_DAY_RC_INIT:
      cout << "- Initializing";
      break;
    case ORP_DAY_RC_SCHEDULE_SWG_COMPLETE:
      cout << "- SWG schedule completed";
      break;
    case ORP_DAY_RC_SCHEDULE_SWG_WAITING:
      cout << "- SWG schedule day waiting";
      break;
    case ORP_DAY_RC_MEASURING:
      cout << "- Measuing Water";
      break;
    case ORP_DAY_RC_MEAS_COMPLETE:
      cout << "- Measuring completed";
      break;
    case ORP_DAY_RC_MEAS_DELAY_FOR_DAY:
      cout << "- Measure delay for day";
      break;
    case ORP_DAY_RC_MEAS_DELAY:
      cout << "- Measure delay";
      break;
    case ORP_DAY_RC_ACT_SWG_COMPLETE:
      cout << "- SWG active completed";
      break;
    case ORP_DAY_RC_ACT_SWG:
      cout << "- SWG active";
      break;
    case ORP_DAY_RC_DELAY_COMPLETE:
      cout << "- Delay completed";
      break;
    case ORP_DAY_RC_DELAY:
      cout << "- Delay active";
      break;
    }
    cout << endl;
  }

  return 0;
}
