create virtual table window_8 using window;

create virtual table span_8 using span(sched, window_8, cpu);

update window_8 set window_start=104110000000, window_dur=28974195286, quantum=10000000 where rowid = 0;

select quantum_ts as bucket, sum(dur)/cast(10000000 as float) as utilization from span_8 where cpu = 7 and utid != 0 group by quantum_ts;

