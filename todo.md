# TODO

- [ ] Profile the HIP benchmark on the Radeon RX 7900 XT to verify that all
  compute units are engaged at sufficiently large point counts. Measure
  occupancy, compute-unit utilization, register pressure and memory bandwidth
  for short and long transformation chains. Compare thread-block sizes against
  the current 256-thread baseline, validate results, and document whether the
  workload is limited by computation, memory access or launch overhead.
- [ ] Measure and report approximate GPU busy time during HIP benchmarks on
  the Radeon RX 7900 XT, analogous to the Pi's compute-queue busy measurement.
  Record the workload, sampling interval and metric definition alongside the
  result. Distinguish GPU busy percentage from per-compute-unit occupancy and
  arithmetic throughput; use an AMD-supported monitoring or profiling tool.
- [ ] Measure CPU utilization for all CPU benchmark methods, thread modes and
  supported architectures. Report utilization per selected core and for the
  benchmark process, with workload, sampling interval and percentage
  normalization (one core versus all available cores). Distinguish CPU busy
  time from useful arithmetic work and memory stalls; account for worker
  synchronization and separate measurement from setup and validation.
