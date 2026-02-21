#include <Arduino.h>
#include <TimeLib.h>
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
  setup(SWG_DATA_SAMPLE_TIME_SEC_DEFAULT, SWG_ORP_STD_DEV_DEFAULT, SWG_ORP_DEFAULT, SWG_ORP_HYSTERESIS_DEFAULT, SWG_ORP_INTERVAL_DEFAULT,
        SWG_ORP_GUARD_DEFAULT, pct);
}

void SWGAnalyzer::setup(int sample_time_sec, float std_dev, int orp_target_val, int orp_target_hysteresis_val, int orp_target_interval, int orp_target_guard, int orp_pct_val[5])
{
  active_guard = 0;
  alarm = 0;
  orp_pct[0] = orp_pct_val[0];
  orp_pct[1] = orp_pct_val[1];
  orp_pct[2] = orp_pct_val[2];
  orp_pct[3] = orp_pct_val[3];
  orp_pct[4] = orp_pct_val[4];
  orp_target = orp_target_val;
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
    orp_data[i] = 0.0;
    orp_data_ts[i] = millis();
    orp_data_valid[i] = 0;
  }

  orp_low_bound = orp_target - orp_hysteresis - (orp_interval * 5);
  last_orp_pct = 0;
  last_orp = 0;

  date_check = 0;
  for (int i = 0; i < 7; i++) {
    start_time[i] = 0;
    end_time[i] = 0;
  }
}

void SWGAnalyzer::enable_datetime_check(int enable)
{
  date_check = enable ? 1 : 0;
}

void SWGAnalyzer::orp_add(float val)
{
  if (orp_data_curr >= max_sample) {
    orp_data_curr = 0;
  }
  orp_data[orp_data_curr] = val;
  orp_data_valid[orp_data_curr] = 1;
  orp_data_ts[orp_data_curr++] = millis();
  if (orp_data_curr >= max_sample) {
    orp_data_curr = 0;
  }
}

int SWGAnalyzer::orp_std_deviation(float &std_dev, float &mean)
{
  float sum_deviation = 0.0;
  int i;

  int total_data = 0;
  mean = 0.0;
  std_dev = 0.0;
  // Calculate the mean
  unsigned long ts = millis();
  for (i = 0; i < max_sample; ++i) {
    if (orp_data_valid[i] <= 0) {
      continue;
    }
    if ((ts - orp_data_ts[i]) > orp_sample_time_ms) {
      orp_data_valid[i] = 0;
      continue;
    }
    mean += orp_data[i];
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

  mean /= total_data;

  // Calculate the sum of squares of differences from the mean (variance numerator)
  for (i = 0; i < max_sample; ++i) {
    if (orp_data_valid[i] <= 0)
      continue;
    sum_deviation += pow(float(orp_data[i]) - mean, 2);
  }

  // Calculate the standard deviation (square root of variance)
  std_dev = sqrt(sum_deviation / total_data);

  if (std_dev > orp_std_dev) {
    alarm = 1;
    return -1;
  }
  alarm = 0;
  return 0;
}

int SWGAnalyzer::get_swg_pct()
{
  float std_dev;
  float mean;

  if (orp_std_deviation(std_dev, mean) < 0) {
    last_orp = mean;
    last_orp_pct = 0;
    return -1;
  }

  if (active_guard > 0) {
    if (mean > orp_target) {
      last_orp = mean;
      last_orp_pct = 0;
      active_guard = 0;
      return 0;
    }
  } else {
    if (mean >= (orp_target - orp_hysteresis)) {
      last_orp = mean;
      last_orp_pct = 0;
      active_guard = 0;
      return 0;
    }
  }
  if (mean < orp_low_bound) {
    last_orp = mean;
    last_orp_pct = 0;
    active_guard = 0;
    alarm = 1;
    return 0;
  }
  if (mean > orp_target) {
    mean = orp_target;
  }

  last_orp = mean;

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

float SWGAnalyzer::get_last_orp()
{
  return last_orp;
}

int SWGAnalyzer::get_last_swg_pct()
{
  return last_orp_pct;
}

int SWGAnalyzer::is_scheduled()
{
  if (!date_check)
    return 1;

  int today = weekday() - 1;
  if (today < 0 || today >= 7)
    return 0;
  if (start_time[today] == 0 && end_time[today] == 0)
    return 0;

  int tm_sec= hour() * 60 * 60 + minute() * 60 + second();
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

int SWGAnalyzer::is_alarmed()
{
  return alarm;
}
