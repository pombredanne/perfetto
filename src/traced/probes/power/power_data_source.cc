
#include <android-base/logging.h>
#include <android-base/strings.h>
#include <android/hardware/health/2.0/IHealth.h>
#include <healthhalutils/HealthHalUtils.h>


using ::android::hardware::health::V2_0::get_health_service;


int main() {
  auto x = get_health_service();
  printf("SVC: %p\n", &*x);
  while (true) {
    x->getChargeCounter([] (auto result, int32_t value) {
      printf("val: %d\n", value);
    });
    sleep(1);
  }
}
