# Sampling Heap Profiler

Because it is infeasible to sample every allocation when doing system-wide
profiling, we sample only a subset of allocations representative of the
distribution of allocations.

## Poisson Sampling
We employ Poisson Sampling to determine which allocations to sample. 
