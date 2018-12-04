create view freq_view as
  select
    ts,
    dur,
    ref as cpu,
    name,
    value
  from counters
  where name = 'cpufreq'
    and ref = 0
    and ref_type = 'cpu';

create view idle_view
  as select
    ts,
    dur,
    ref as cpu,
    name,
    value
  from counters
  where name = 'cpuidle'
    and ref = 0
    and ref_type = 'cpu';

create virtual table freq_idle
              using span_join(freq_view PARTITIONED cpu,
                              idle_view PARTITIONED cpu)

