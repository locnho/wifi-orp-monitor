#ifndef SWG_ANALYZER_H
#define SWG_ANALYZER_H  1

#define MAX_ORP_DATA                (15*60)
#define SWG_ORP_STD_DEV_DEFAULT     2.5
#define SWG_ORP_DEFAULT             700
#define SWG_ORP_HYSTERESIS_DEFAULT  10
#define SWG_ORP_INTERVAL_DEFAULT    15
#define SWG_ORP_GUARD_DEFAULT       5
#define SWG_ORP_PCT0_DEFAULT        15
#define SWG_ORP_PCT1_DEFAULT        25
#define SWG_ORP_PCT2_DEFAULT        50
#define SWG_ORP_PCT3_DEFAULT        75
#define SWG_ORP_PCT4_DEFAULT        100
#define SWG_DATA_SAMPLE_TIME_SEC_DEFAULT (5*60)

class SWGAnalyzer {
public:
  SWGAnalyzer();
  void setup(int sample_time_sec, float std_dev, int orp_target_val, int orp_target_hysteresis_val, int orp_target_interval, int orp_target_guard, int orp_pct_val[5]);
  void orp_add(float val);
  int get_swg_pct();

  float get_last_orp();
  int get_last_swg_pct();

  void enable_datetime_check(int enable);
  int is_scheduled();
  int is_alarmed();
  void set_schedule(int day_num, int start, int end);

protected:
  float orp_data[MAX_ORP_DATA];
  unsigned long orp_data_ts[MAX_ORP_DATA];
  char orp_data_valid[MAX_ORP_DATA];
  int orp_data_curr;
  int max_sample;

  unsigned long orp_sample_time_ms;
  float orp_std_dev;
  int orp_target;
  int orp_hysteresis;
  int orp_interval;
  int orp_guard;
  int orp_pct[5];
  int orp_low_bound;

  float last_orp;
  int last_orp_pct;
  int active_guard;
  int alarm;

  int date_check;
  unsigned int start_time[7];
  unsigned int end_time[7];

  int orp_std_deviation(float &std_dev, float &mean);
};

#endif
