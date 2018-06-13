#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>

#include "perfetto/base/logging.h"

int main() {
  const char kCounterPath[] = "/sys/devices/soc/800f000.qcom,spmi/spmi-0/spmi0-02/800f000.qcom,spmi:qcom,pmi8998@2:qcom,qpnp-smb2/power_supply/battery/charge_counter";
  const char kCurrentNowPath[] = "/sys/devices/soc/800f000.qcom,spmi/spmi-0/spmi0-02/800f000.qcom,spmi:qcom,pmi8998@2:qcom,qpnp-smb2/power_supply/battery/current_now";
  const char kMarkerPath[] = "/d/tracing/trace_marker";

// C|1365|monitor PID|31835

  while(true) {
    int counter_fd = open(kCounterPath, O_RDONLY);
    PERFETTO_CHECK(counter_fd >= 0);
    int current_fd = open(kCurrentNowPath, O_RDONLY);
    PERFETTO_CHECK(current_fd >= 0);

    char buf[64] = {};
    ssize_t rsize = read(counter_fd, &buf, sizeof(buf)-1);
    PERFETTO_CHECK(rsize > 0);
    buf[rsize] = '\0';
    long counter = atol(buf);

    rsize = read(current_fd, &buf, sizeof(buf)-1);
    PERFETTO_CHECK(rsize > 0);
    buf[rsize] = '\0';
    long current = atol(buf);

    int marker_fd = open(kMarkerPath, O_WRONLY);
    PERFETTO_CHECK(marker_fd >= 0);
    sprintf(buf, "C|1|BatteryCounter|%ld", counter);
    write(marker_fd, buf, strlen(buf));

    sprintf(buf, "C|1|BatteryCurrent|%ld", current);
    write(marker_fd, buf, strlen(buf));

    close(marker_fd);
    close(counter_fd);
    close(current_fd);
    sleep(10);
  }
}
