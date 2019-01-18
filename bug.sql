create virtual table window_38 using window;

create virtual table window_39 using window;

create virtual table window_40 using window;

create virtual table window_41 using window;

create virtual table window_42 using window;

create virtual table window_43 using window;

create virtual table window_44 using window;

create virtual table window_45 using window;

       select max(value), min(value) from         counters where name = 'Active'         and ref = 0

       select max(value), min(value) from         counters where name = 'Inactive'         and ref = 0

       select max(value), min(value) from         counters where name = 'MemAvailable'         and ref = 0

       select max(value), min(value) from         counters where name = 'SwapCached'         and ref = 0

create virtual table window_07919d88_4baa_486a_b5a5_1ddeddefe1a6 using window;

create virtual table window_2c2788c2_2c46_4e90_a28b_b481533caf55 using window;

create virtual table window_ed169514_e02b_4012_b787_57d65148320e using window;

create virtual table window_f771a634_116a_4daf_958f_8a206cc9fbcd using window;

create virtual table window_d54c7763_aa2f_47a0_9a68_f99b0d5259b0 using window;

create virtual table window_bc447143_1589_4195_b5b8_80da650de588 using window;

create virtual table window_1d35a470_cb01_4de8_b854_9fecc1389eef using window;

create virtual table window_26cf33f7_3152_4a0e_9792_276f56c983af using window;

create virtual table window_1915e62b_b984_4791_8a4f_8618b6235608 using window;

create virtual table window_52830282_3817_4d24_9738_32884aec49e7 using window;

create virtual table window_a9e2e809_9f54_4cba_9249_396ca11202ed using window;

create virtual table span_38               using span_join(sched PARTITIONED cpu,                               window_38 PARTITIONED cpu);

create virtual table span_39               using span_join(sched PARTITIONED cpu,                               window_39 PARTITIONED cpu);

create virtual table span_40               using span_join(sched PARTITIONED cpu,                               window_40 PARTITIONED cpu);

create virtual table span_41               using span_join(sched PARTITIONED cpu,                               window_41 PARTITIONED cpu);

create virtual table span_42               using span_join(sched PARTITIONED cpu,                               window_42 PARTITIONED cpu);

create virtual table span_43               using span_join(sched PARTITIONED cpu,                               window_43 PARTITIONED cpu);

create virtual table span_44               using span_join(sched PARTITIONED cpu,                               window_44 PARTITIONED cpu);

create virtual table span_45               using span_join(sched PARTITIONED cpu,                               window_45 PARTITIONED cpu);

select ts, value from counters         where 41080015080 <= ts_end and ts <= 130030156440         and name = 'Active' and ref = 0;

select ts, value from counters         where 41080015080 <= ts_end and ts <= 130030156440         and name = 'Inactive' and ref = 0;

select ts, value from counters         where 41080015080 <= ts_end and ts <= 130030156440         and name = 'MemAvailable' and ref = 0;

select ts, value from counters         where 41080015080 <= ts_end and ts <= 130030156440         and name = 'SwapCached' and ref = 0;

select utid from thread where upid=9

select utid from thread where upid=5

select utid from thread where upid=28

select utid from thread where upid=11

select utid from thread where upid=3

select utid from thread where upid=58

select utid from thread where upid=62

select utid from thread where upid=64

select utid from thread where upid=6

select utid from thread where upid=77

select utid from thread where upid=162

update window_38 set       window_start=41000000000,       window_dur=89030156440,       quantum=100000000       where rowid = 0;

select         quantum_ts as bucket,         sum(dur)/cast(100000000 as float) as utilization         from span_38         where cpu = 0         and utid != 0         group by quantum_ts

update window_39 set       window_start=41000000000,       window_dur=89030156440,       quantum=100000000       where rowid = 0;

select         quantum_ts as bucket,         sum(dur)/cast(100000000 as float) as utilization         from span_39         where cpu = 1         and utid != 0         group by quantum_ts

update window_40 set       window_start=41000000000,       window_dur=89030156440,       quantum=100000000       where rowid = 0;

select         quantum_ts as bucket,         sum(dur)/cast(100000000 as float) as utilization         from span_40         where cpu = 2         and utid != 0         group by quantum_ts

update window_41 set       window_start=41000000000,       window_dur=89030156440,       quantum=100000000       where rowid = 0;

select         quantum_ts as bucket,         sum(dur)/cast(100000000 as float) as utilization         from span_41         where cpu = 3         and utid != 0         group by quantum_ts

update window_42 set       window_start=41000000000,       window_dur=89030156440,       quantum=100000000       where rowid = 0;

select         quantum_ts as bucket,         sum(dur)/cast(100000000 as float) as utilization         from span_42         where cpu = 4         and utid != 0         group by quantum_ts

update window_43 set       window_start=41000000000,       window_dur=89030156440,       quantum=100000000       where rowid = 0;

select         quantum_ts as bucket,         sum(dur)/cast(100000000 as float) as utilization         from span_43         where cpu = 5         and utid != 0         group by quantum_ts

update window_44 set       window_start=41000000000,       window_dur=89030156440,       quantum=100000000       where rowid = 0;

select         quantum_ts as bucket,         sum(dur)/cast(100000000 as float) as utilization         from span_44         where cpu = 6         and utid != 0         group by quantum_ts

update window_45 set       window_start=41000000000,       window_dur=89030156440,       quantum=100000000       where rowid = 0;

select         quantum_ts as bucket,         sum(dur)/cast(100000000 as float) as utilization         from span_45         where cpu = 7         and utid != 0         group by quantum_ts

create view process_07919d88_4baa_486a_b5a5_1ddeddefe1a6 as           select * from sched where utid in (37,38,39,162,367,425,426,428,429,430,431,435,437,507,654,655,713,736,737,738,739,740,826,870,871,872,1065,1174,1175,1176,1177,1480,1481,1591,1592,1595,1597,1606,1607);

create view process_2c2788c2_2c46_4e90_a28b_b481533caf55 as           select * from sched where utid in (46,47,54,178,225,226,785,812,829,1067,1068,1069,1078,1079,1080,1081,1601,1602,1603,1604,1605,1613,1615);

create view process_ed169514_e02b_4012_b787_57d65148320e as           select * from sched where utid in (52,153,637,1366,1367);

create view process_f771a634_116a_4daf_958f_8a206cc9fbcd as           select * from sched where utid in (59,60,61,117,119,349,358,362,377,380,413,416,427,463,464,493,514,515,516,530,663,664,714,718,727,881,882,883,884,885,886,887,888,889,1084,1599,1600);

create view process_d54c7763_aa2f_47a0_9a68_f99b0d5259b0 as           select * from sched where utid in (76,78,81,82,84,86,87,88,101,112,113,115,118,130,136,139,140,145,148,182,183,185,207,210,217,218,223,244,270,272,273,304,342,343,344,345,346,347,407,476,504,525,557,636,645,688,691,692,761,762,782,784,789,792,801,807,836,864,865,867,869,879,880,1283,1489,1619);

create view process_bc447143_1589_4195_b5b8_80da650de588 as           select * from sched where utid in (97,100,114,120,128,161,205,212,222,243,257,374,434,566,567,568,788,795,796,797,798,799,800,822,823,830,860,861,862,863,1109);

create view process_1d35a470_cb01_4de8_b854_9fecc1389eef as           select * from sched where utid in (110,152,261,408,409,1019);

create view process_26cf33f7_3152_4a0e_9792_276f56c983af as           select * from sched where utid in (169,171,172,369,372,373,375,376,378,379,381,547,551,552,553,559,572,574,769,844,874,875,876);

create view process_1915e62b_b984_4791_8a4f_8618b6235608 as           select * from sched where utid in (189,201,232,236,262,263,264,265,266,267,268,816,926,928,1450,1451,1452,1614);

create view process_52830282_3817_4d24_9738_32884aec49e7 as           select * from sched where utid in (147,215,216,220,221,805);

create view process_a9e2e809_9f54_4cba_9249_396ca11202ed as           select * from sched where utid in (181,227,228);

update window_38 set       window_start=41000000000,       window_dur=89030156440,       quantum=100000000       where rowid = 0;

create virtual table span_07919d88_4baa_486a_b5a5_1ddeddefe1a6               using span_join(process_07919d88_4baa_486a_b5a5_1ddeddefe1a6PARTITIONED cpu,                               window_07919d88_4baa_486a_b5a5_1ddeddefe1a6 PARTITIONED cpu);

