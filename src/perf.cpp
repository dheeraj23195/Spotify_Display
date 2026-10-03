#include "perf.h"

static const int WINDOW = 150;  // frames per reading (5 s at 30 fps)
static uint64_t sumCompose = 0, sumFlip = 0;
static uint32_t maxCompose = 0;
static int frames = 0;
static volatile uint32_t lastComposeAvg = 0, lastComposeMax = 0, lastFlipAvg = 0;
static TaskHandle_t netTask = nullptr, webTask = nullptr;

void perfFrame(uint32_t composeUs, uint32_t flipUs) {
  sumCompose += composeUs;
  sumFlip += flipUs;
  if (composeUs > maxCompose) maxCompose = composeUs;
  if (++frames < WINDOW) return;
  lastComposeAvg = sumCompose / frames;
  lastComposeMax = maxCompose;
  lastFlipAvg = sumFlip / frames;
  sumCompose = sumFlip = 0;
  maxCompose = 0;
  frames = 0;
}

void perfSetTask(PerfTask which, TaskHandle_t handle) {
  (which == PerfTask::Network ? netTask : webTask) = handle;
}

PerfSnapshot perfGet() {
  PerfSnapshot p;
  p.composeAvgUs = lastComposeAvg;
  p.composeMaxUs = lastComposeMax;
  p.flipAvgUs = lastFlipAvg;
  p.freeHeap = ESP.getFreeHeap();
  p.minFreeHeap = ESP.getMinFreeHeap();
  p.largestBlock = ESP.getMaxAllocHeap();
  p.freePsram = ESP.getFreePsram();
  p.netStackFree = netTask ? uxTaskGetStackHighWaterMark(netTask) : 0;
  p.webStackFree = webTask ? uxTaskGetStackHighWaterMark(webTask) : 0;
  return p;
}
