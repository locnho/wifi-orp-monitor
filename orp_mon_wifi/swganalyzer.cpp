#ifdef ARDUINO
  #include <Arduino.h>
#else
  #include <math.h>
#endif
#include <time.h>
#include "swganalyzer.h"

SWGAnalyzer::SWGAnalyzer()
{
  int pct[5] = {
      SWG_ORP_PCT0_DEFAULT,
      SWG_ORP_PCT1_DEFAULT,
      SWG_ORP_PCT2_DEFAULT,
      SWG_ORP_PCT3_DEFAULT,
      SWG_ORP_PCT4_DEFAULT
  };
  millis_cb = NULL;
  time_cb = NULL;
  localtime_cb = NULL;
  setup();
  setup_alg(SWG_DATA_SAMPLE_TIME_SEC_DEFAULT, SWG_ORP_STD_DEV_DEFAULT, SWG_ORP_DEFAULT, SWG_ORP_HYSTERESIS_DEFAULT, SWG_ORP_INTERVAL_DEFAULT,
            SWG_ORP_GUARD_DEFAULT, pct);
}

void SWGAnalyzer::set_time_functions(unsigned long (*millis_cb_f)(), time_t (*time_cb_f)(time_t *timer), struct tm * (*localtime_cb_f)(const time_t * timer))
{
  millis_cb = millis_cb_f;
  time_cb = time_cb_f;
  localtime_cb = localtime_cb_f;
}

void SWGAnalyzer::setup()
{
  alarm = 0;
  orp_target = 0;
  last_orp = 0;
  last_orp_pct = 0;
  alarm = 0;

  date_check = 0;
  for (int i = 0; i < 7; i++) {
    start_time[i] = 0;
    end_time[i] = 0;
  }
}

void SWGAnalyzer::setup_alg(int sample_time_sec, float std_dev, int orp_target_val,
                            int orp_target_hysteresis_val, int orp_target_interval,
                            int orp_target_guard, int orp_pct_val[5])
{
  unsigned long ms_time = millis_cb ? millis_cb() : 0;

  orp_target = orp_target_val;

  active_guard = 0;
  orp_interval = orp_target_interval;
  orp_hysteresis = orp_target_hysteresis_val;
  orp_guard = orp_target_guard;
  orp_std_dev = std_dev;
  orp_data_curr = 0;
  orp_sample_time_ms = sample_time_sec * 1000;
  if (orp_sample_time_ms > MAX_ORP_DATA * 1000) {
    orp_sample_time_ms = MAX_ORP_DATA * 1000;
  }
  max_sample = sample_time_sec;
  if (max_sample > MAX_ORP_DATA) {
    max_sample = MAX_ORP_DATA;
  }
  for (int i = 0; i < max_sample; i++) {
    orp_data[i] = 0;
    orp_data_ts[i] = ms_time;
    orp_data_valid[i] = 0;
  }
  orp_low_bound = orp_target - orp_hysteresis - (orp_interval * 5);
  orp_pct[0] = orp_pct_val[0];
  orp_pct[1] = orp_pct_val[1];
  orp_pct[2] = orp_pct_val[2];
  orp_pct[3] = orp_pct_val[3];
  orp_pct[4] = orp_pct_val[4];
}

void SWGAnalyzer::enable_datetime_check(int enable)
{
  date_check = enable ? 1 : 0;
}

void SWGAnalyzer::orp_add(int val, bool swg_active)
{
  (void) swg_active;

  unsigned long ms_time = millis_cb ? millis_cb() : 0;

  if (orp_data_curr >= max_sample) {
    orp_data_curr = 0;
  }
  orp_data[orp_data_curr] = val;
  orp_data_valid[orp_data_curr] = 1;
  orp_data_ts[orp_data_curr++] = ms_time;
  if (orp_data_curr >= max_sample) {
    orp_data_curr = 0;
  }
}

int SWGAnalyzer::orp_std_deviation(float &std_dev, float &mean)
{
  float sum_deviation = 0;
  int total_data = 0;
  int sum = 0;

  mean = 0.0;
  std_dev = 0.0;

  // Calculate the mean
  unsigned long ts = millis_cb ? millis_cb() : 0;
  for (int i = 0; i < max_sample; ++i) {
    if (orp_data_valid[i] <= 0) {
      continue;
    }
    if ((ts - orp_data_ts[i]) > orp_sample_time_ms) {
      orp_data_valid[i] = 0;
      continue;
    }
    sum += orp_data[i];
    total_data++;
  }
  if (total_data <= 0) {
    mean = 0.0;
    return -1;
  }
  if (total_data < int(max_sample * 0.75)) {
    mean = 0.0;
    return -1;
  }

  mean = float(sum) / total_data;

  // Calculate the sum of squares of differences from the mean (variance numerator)
  for (int i = 0; i < max_sample; ++i) {
    if (orp_data_valid[i] <= 0)
      continue;
    sum_deviation += (float) pow(float(orp_data[i]) - mean, 2);
  }

  // Calculate the standard deviation (square root of variance)
  std_dev = float(sqrt(sum_deviation / total_data));

  if (std_dev > orp_std_dev) {
    alarm = 1;
    return -1;
  }
  alarm = 0;
  return 0;
}

int SWGAnalyzer::get_swg_pct(bool swg_active)
{
  (void) swg_active;
  float std_dev;
  float mean;

  if (orp_std_deviation(std_dev, mean) < 0) {
    last_orp = int(mean);
    last_orp_pct = 0;
    return -1;
  }

  if (active_guard > 0) {
    if (mean > orp_target) {
      last_orp = int(mean);
      last_orp_pct = 0;
      active_guard = 0;
      return 0;
    }
  } else {
    if (mean >= (orp_target - orp_hysteresis)) {
      last_orp = int(mean);
      last_orp_pct = 0;
      active_guard = 0;
      return 0;
    }
  }
  if (mean < orp_low_bound) {
      last_orp = int(mean);
    last_orp_pct = 0;
    active_guard = 0;
    alarm = 1;
    return 0;
  }
  if (mean > orp_target) {
    mean = (float) orp_target;
  }

  last_orp = (int) mean;

  int base_orp = orp_target - orp_hysteresis;
  if (mean >= (base_orp - (orp_interval * 1) + 1)) {
    last_orp_pct = orp_pct[0];
    active_guard = 1;
    return orp_pct[0];
  } else if (mean >= (base_orp - (orp_interval * 2) + 1) && mean <= (base_orp - (orp_interval * 1))) {
    last_orp_pct = orp_pct[1];
    active_guard = 2;
    return orp_pct[1];
  } else if (mean >= (base_orp - (orp_interval * 3) + 1) && mean <= (base_orp - (orp_interval * 2))) {
    last_orp_pct = orp_pct[2];
    active_guard = 3;
    return orp_pct[2];
  } else if (mean >= (base_orp - (orp_interval * 4) + 1) && mean <= (base_orp - (orp_interval * 3))) {
    last_orp_pct = orp_pct[3];
    active_guard = 4;
    return orp_pct[3];
  } else if (mean >= (base_orp - (orp_interval * 5) + 1) && mean <= (base_orp - (orp_interval * 4))) {
    last_orp_pct = orp_pct[4];
    active_guard = 5;
    return orp_pct[4];
  } else {
    last_orp_pct = orp_pct[4];
    active_guard = 6;
    return orp_pct[4];
  }
}

int SWGAnalyzer::is_scheduled()
{
  if (!date_check)
    return 1;

  if (time_cb == NULL || localtime_cb == NULL)
    return 0;

  const time_t time_val_sec = time_cb(NULL);
  struct tm time_tm = *localtime_cb(&time_val_sec);

  int today = time_tm.tm_wday;
  if (today < 0 || today >= 7)
    return 0;
  if (start_time[today] == 0 && end_time[today] == 0)
    return 0;

  unsigned int tm_sec= time_tm.tm_hour * 60 * 60 + time_tm.tm_min * 60 + time_tm.tm_sec;
  if (tm_sec >= start_time[today] && tm_sec <= end_time[today])
    return 1;
  return 0;
}

void SWGAnalyzer::set_schedule(int day_num, int start, int end)
{
  if (day_num < 0 || day_num >= 7)
    return;
  start_time[day_num] = start;
  end_time[day_num] = end;
}

//
// SWGAnalyzer v2 class 
SWGAnalyzerv2::SWGAnalyzerv2()
{
  setup();
  setup_alg(SWG_ORP_DEFAULT, SWG_ORP_ACTIVE_TIME_HRS_DEFAULT, SWG_ORP_DELAY_TIME_HRS_DEFAULT, SWG_ORP_MEASURE_TIME_HRS_DEFAULT);
}

void SWGAnalyzerv2::setup() {
  SWGAnalyzer::setup();
  orp_day_reason_code = ORP_DAY_RC_INIT;
}

void SWGAnalyzerv2::setup_alg(int orp_day_cfg_target_val, int orp_day_cfg_swg_time_hours, int orp_day_cfg_delay_time_hours, int orp_day_cfg_measure_time_hours)
{
  orp_target = orp_day_cfg_target_val;

  for (int i = 0; i < TOTAL_NUM_DAYS_SAMPLE; i++) {
    orp_day_sum[i] = 0;
    orp_day_total[i] = 0;
    orp_day_avg[i] = 0;
    orp_day_swg_wkday[i] = -1;
  }
  orp_day_curr = 0;
  orp_day_state = ORP_DAY_STATE_INIT;
  orp_day_swg_active_time_valid = 0;
  orp_day_swg_active_time_st = 0;
  orp_day_swg_active_time_ms = 0;
  orp_day_delay_ts_ms = orp_day_measure_ts_ms = 0;
  
  orp_day_cfg_swg_time_ms = orp_day_cfg_swg_time_hours * 60 * 60 * 1000;
  orp_day_cfg_delay_time_ms = orp_day_cfg_delay_time_hours * 60 * 60 * 1000;
  orp_day_cfg_measure_time_ms = orp_day_cfg_measure_time_hours * 60 * 60 * 1000;
}

void SWGAnalyzerv2::setup_alg(int sample_time_sec, float std_dev, int orp_target_val, int orp_target_hysteresis_val, int orp_target_interval, int orp_target_guard, int orp_pct_val[5]) {
  SWGAnalyzer::setup_alg(sample_time_sec, std_dev, orp_target_val, orp_target_hysteresis_val, orp_target_interval, orp_target_guard, orp_pct_val);
}

void SWGAnalyzerv2::orp_add(int val,  bool swg_active)
{
  if (millis_cb == NULL || time_cb == NULL || localtime_cb == NULL)
    return;

  unsigned long ms_time = millis_cb();
  const time_t time_val_sec = time_cb(NULL);
  struct tm time_tm = *localtime_cb(&time_val_sec);

  // Check if same day
  if (time_tm.tm_wday == orp_day_swg_wkday[orp_day_curr]) {
    orp_day_sum[orp_day_curr] += val;
    orp_day_total[orp_day_curr] += 1;
    orp_day_avg[orp_day_curr] = orp_day_sum[orp_day_curr] / orp_day_total[orp_day_curr];
     orp_day_measured_time[orp_day_curr] += ms_time - orp_day_measured_ts[orp_day_curr];
     orp_day_measured_ts[orp_day_curr] = ms_time;
    if (swg_active)
      orp_day_swg_act[orp_day_curr] = swg_active;
    return;
  }
  // Next day sample
  int prev = orp_day_curr;
  if (orp_day_curr == TOTAL_NUM_DAYS_SAMPLE - 1) {
    orp_day_curr = 0;
  } else {
    orp_day_curr++;
  }
  // First data sample of next day
  orp_day_sum[orp_day_curr] = val;
  orp_day_total[orp_day_curr] = 1;
  orp_day_swg_act[orp_day_curr] = swg_active;
  orp_day_avg[orp_day_curr] = orp_day_sum[orp_day_curr] / orp_day_total[orp_day_curr];
  orp_day_swg_wkday[orp_day_curr] = time_tm.tm_wday;
  orp_day_measured_time[orp_day_curr] = 0;
  orp_day_measured_ts[orp_day_curr] = ms_time;
}

int SWGAnalyzerv2::get_swg_pct(bool swg_active)
{
  unsigned long ms_time = millis_cb ? millis_cb() : 0;
  int delta_ms;

  switch (orp_day_state) {
  case ORP_DAY_STATE_INIT:
    orp_day_measure_time_valid = 0;
    orp_day_swg_active_time_valid = 0;
    orp_day_delay_ts_ms = ms_time;
    orp_day_state = ORP_DAY_STATE_DELAY;
    last_orp_pct = 0;
    orp_day_reason_code = ORP_DAY_RC_INIT;
    return last_orp_pct;
  case ORP_DAY_STATE_ACTIVE_SWG:
    if (orp_day_swg_active_time_valid) {
      delta_ms = ms_time - orp_day_swg_active_time_st;
    } else {
      orp_day_swg_active_time_valid = 1;
      delta_ms = 0;
    }
    orp_day_swg_active_time_st = ms_time;
    if (swg_active) {
      orp_day_swg_active_time_ms += delta_ms;
    } else {
      orp_day_swg_active_time_valid = 0;
    }
    if (orp_day_swg_active_time_ms >= orp_day_cfg_swg_time_ms) {
      orp_day_state = ORP_DAY_STATE_DELAY;
      orp_day_delay_ts_ms = ms_time;
      orp_day_reason_code = ORP_DAY_RC_ACT_SWG_COMPLETE;
    } else {
      orp_day_reason_code = ORP_DAY_RC_ACT_SWG;
    }
    last_orp_pct = orp_pct[0];
    return last_orp_pct;
  case ORP_DAY_STATE_DELAY:
    if (ms_time - orp_day_delay_ts_ms >= orp_day_cfg_delay_time_ms) {
      orp_day_state = ORP_DAY_STATE_MEASURE;
      orp_day_measure_time_valid = 0;
      orp_day_reason_code = ORP_DAY_RC_DELAY_COMPLETE;
    } else {
      orp_day_reason_code = ORP_DAY_RC_DELAY;
    }
    last_orp_pct = 0;
    return last_orp_pct;
  case ORP_DAY_STATE_MEASURE:
  default:
    if (orp_day_measure_time_valid) {
      delta_ms = ms_time - orp_day_measure_time_st;
      orp_day_measure_time_st = ms_time;
    } else {
      orp_day_measure_time_valid = 1;
      orp_day_measure_time_st = ms_time;
      orp_day_measure_time_ms = 0;
      delta_ms = 0;
    }
    orp_day_measure_time_ms += delta_ms;

    last_orp_pct = 0;
    if (orp_day_measure_time_ms <= orp_day_cfg_measure_time_ms * 0.75) {
      orp_day_reason_code = ORP_DAY_RC_MEAS_DELAY;
      return last_orp_pct;
    }
    if (orp_day_measured_time[orp_day_curr] <= orp_day_cfg_measure_time_ms * 0.75) {
      orp_day_reason_code = ORP_DAY_RC_MEAS_DELAY_FOR_DAY;
      return last_orp_pct;
    }
    if (orp_day_avg[orp_day_curr] <= orp_target) {
      orp_day_state = ORP_DAY_STATE_SCHEDULE_SWG;
      orp_day_schedule_day = orp_day_curr + 1;
      if (orp_day_schedule_day >= TOTAL_NUM_DAYS_SAMPLE)
        orp_day_schedule_day = 0;
      orp_day_reason_code = ORP_DAY_RC_MEAS_COMPLETE;
      return last_orp_pct;
    } else {
      orp_day_reason_code = ORP_DAY_RC_MEASURING;
    }
    return last_orp_pct;
  case ORP_DAY_STATE_SCHEDULE_SWG:
    if (orp_day_curr == orp_day_schedule_day) {
      orp_day_state = ORP_DAY_STATE_ACTIVE_SWG;
      orp_day_swg_active_time_valid = 0;
      orp_day_swg_active_time_ms = 0;
      orp_day_reason_code = ORP_DAY_RC_SCHEDULE_SWG_COMPLETE;
    } else {
      orp_day_reason_code = ORP_DAY_RC_SCHEDULE_SWG_WAITING;
    }
    last_orp_pct = 0;
    return last_orp_pct;
  }
}
