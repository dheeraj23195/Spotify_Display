#pragma once
#include <Arduino.h>

// Numbers for the status page: how busy the render loop is, memory, task stacks.

struct PerfSnapshot {
  uint32_t composeAvgUs, composeMaxUs;  // time to build one frame (render + copy to panel buffer)
  uint32_t flipAvgUs;                   // time spent waiting for the panel to swap buffers
  uint32_t freeHeap, minFreeHeap, largestBlock, freePsram;  // bytes
  uint32_t netStackFree, webStackFree;  // least stack ever left, bytes
};

enum class PerfTask { Network, Web };

void perfFrame(uint32_t composeUs, uint32_t flipUs);  // render loop, once per frame
void perfSetTask(PerfTask which, TaskHandle_t handle);
PerfSnapshot perfGet();
