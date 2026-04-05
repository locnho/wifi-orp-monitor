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
  void setup();
  void setup_alg(int sample_time_sec, float std_dev, int orp_target_val, int orp_target_hysteresis_val, int orp_target_interval, int orp_target_guard, int orp_pct_val[5]);
  void orp_add(int val,  bool swg_active);
  int get_swg_pct(bool swg_active);
  int get_alg_id() { return 1; }

  int get_orp_target() { return orp_target; }
  int get_last_orp() { return last_orp; };
  int get_last_swg_pct() { return last_orp_pct; };

  void enable_datetime_check(int enable);
  int is_scheduled();
  int is_alarmed() { return alarm; }
  void set_schedule(int day_num, int start, int end);

  void set_millis_cb(unsigned long (*cb)());

protected:
  unsigned long (*millis_cb)();

  int orp_target;
  int last_orp;
  int last_orp_pct;
  int alarm;

  //
  // Scheduling
  int date_check;
  unsigned int start_time[7];
  unsigned int end_time[7];

  //
  // Alg variables
  int orp_data[MAX_ORP_DATA];
  unsigned long orp_data_ts[MAX_ORP_DATA];
  char orp_data_valid[MAX_ORP_DATA];
  int orp_data_curr;
  int max_sample;
  unsigned long orp_sample_time_ms;

  float orp_std_dev;
  int orp_hysteresis;
  int orp_interval;
  int orp_guard;
  int orp_pct[5];
  int orp_low_bound;
  int active_guard;

  int orp_std_deviation(float &std_dev, float &mean);
};

#define ORP_DAY_STATE_INIT          0 
#define ORP_DAY_STATE_DELAY         1   
#define ORP_DAY_STATE_MEASURE       2
#define ORP_DAY_STATE_SCHEDULE_SWG  3
#define ORP_DAY_STATE_ACTIVE_SWG    4

#define SWG_ORP_ACTIVE_TIME_HRS_DEFAULT     3
#define SWG_ORP_DELAY_TIME_HRS_DEFAULT      (3 * 24)
#define SWG_ORP_MEASURE_TIME_HRS_DEFAULT    5

#define ORP_DAY_RC_INIT                     1
#define ORP_DAY_RC_SCHEDULE_SWG_COMPLETE    2
#define ORP_DAY_RC_SCHEDULE_SWG_WAITING     3
#define ORP_DAY_RC_MEASURING                4
#define ORP_DAY_RC_MEAS_COMPLETE            5
#define ORP_DAY_RC_MEAS_DELAY_FOR_DAY       6
#define ORP_DAY_RC_MEAS_DELAY               7
#define ORP_DAY_RC_ACT_SWG_COMPLETE         8
#define ORP_DAY_RC_ACT_SWG                  9
#define ORP_DAY_RC_DELAY_COMPLETE           10
#define ORP_DAY_RC_DELAY                    11

class SWGAnalyzerv2 : public SWGAnalyzer {
public:
  SWGAnalyzerv2();
  void setup();
  void setup_alg(int orp_day_cfg_target_val, int orp_day_cfg_swg_time_hours, int orp_day_cfg_delay_time_hours, int orp_day_cfg_measure_time_hours);
  void setup_alg(int sample_time_sec, float std_dev, int orp_target_val, int orp_target_hysteresis_val, int orp_target_interval, int orp_target_guard, int orp_pct_val[5]);
  void orp_add(int val,  bool swg_active);
  int get_swg_pct(bool swg_active);
  int get_alg_id() { return 2; }

  int get_orp_day_avg(int day) { return orp_day_avg[day]; }
  int get_orp_day_state() { return orp_day_state; }
  int get_orp_day_reason_code() { return orp_day_reason_code; }
  unsigned long get_orp_day_delay_ts_ms() { return orp_day_delay_ts_ms; }

protected:
  //
  // 7 day scheduling
  #define TOTAL_NUM_DAYS_SAMPLE   7
  int orp_day_sum[TOTAL_NUM_DAYS_SAMPLE];
  int orp_day_total[TOTAL_NUM_DAYS_SAMPLE];
  int orp_day_avg[TOTAL_NUM_DAYS_SAMPLE];
  int orp_day_swg_wkday[TOTAL_NUM_DAYS_SAMPLE];
  unsigned long orp_day_measured_time[TOTAL_NUM_DAYS_SAMPLE];
  unsigned long orp_day_measured_ts[TOTAL_NUM_DAYS_SAMPLE];
  bool orp_day_swg_act[TOTAL_NUM_DAYS_SAMPLE];
  int orp_day_curr;
  int orp_day_state;

  bool orp_day_swg_active_time_valid;
  unsigned long orp_day_swg_active_time_st;
  unsigned long orp_day_swg_active_time_ms;
  unsigned long orp_day_delay_ts_ms;
  unsigned long orp_day_measure_ts_ms;
  unsigned long orp_day_cfg_swg_time_ms;
  unsigned long orp_day_cfg_delay_time_ms;
  bool orp_day_measure_time_valid;
  unsigned long orp_day_measure_time_st;
  unsigned long orp_day_measure_time_ms;
  unsigned long orp_day_cfg_measure_time_ms;
  int orp_day_schedule_day;
  int orp_day_pct;
  int orp_day_reason_code;
};

#endif
