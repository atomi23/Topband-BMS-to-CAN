#pragma once

inline int clampBmsCount(int count, int maxBms) {
  if (count < 1) return 1;
  if (count > maxBms) return maxBms;
  return count;
}
