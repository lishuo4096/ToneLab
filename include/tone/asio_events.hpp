#pragma once
#include <atomic>
// Local RtAudio integration: consume ASIO notifications on the STA owner thread.
// 0 none, 1 driver reset request, 2 external clock change.
extern std::atomic<int> tonelabAsioEvent;
extern std::atomic<unsigned> tonelabAsioExpectedRate;
