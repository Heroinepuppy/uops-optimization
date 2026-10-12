# Remote benchmark control

Run all commands below on the Raspberry Pi through SSH. Installing tmux on
the laptop does not provide a persistent session on the Pi.

## Start the benchmark in tmux

Install tmux on the Pi if needed:

```bash
sudo apt install tmux
tmux -V
```

If a benchmark is already running in VS Code or another terminal, stop it
with Ctrl+C first. Do not run two benchmark processes writing the same CSV.
The runner resumes completed calls; an incomplete call is measured again.

From the repository directory:

```bash
tmux new -s benchmarks
.venv/bin/python benchmarks/run_all_benchmarks.py
```

Detach with **Ctrl+B**, then **D**. The benchmark continues on the Pi when
SSH or VS Code is closed. No additional pipeline logs are enabled.

Reconnect through SSH and attach to the terminal:

```bash
tmux attach -t benchmarks
```

List existing sessions:

```bash
tmux ls
```

In tmux, **Ctrl+B**, then **[** enters scroll mode; **Q** exits scroll mode.
Ctrl+C in the benchmark terminal stops the benchmark. A Pi reboot ends the
session; rerun the same command to resume saved results.

## Watch result-file updates

In a separate SSH terminal, from the repository directory:

```bash
watch -n 10 'ls -lh results/data/armv8-vulkan/benchmark_matrix.csv'
```

The display refreshes every ten seconds. File size and modification time
change after each completed CPU or GPU call. A single call may take several
minutes without changing the CSV. The default measurement budget is 300
seconds per call, estimated after the first round; setup and validation are
additional, and this is not a hard timeout.

The CPU/Vulkan runner normally writes to
`results/data/armv8-vulkan/benchmark_matrix.csv`. Check its startup output
for the selected directory; CPU-only runs default to `results/data/armv8`.

## Watch temperature, throttling and CPU clock

From any directory on the Pi:

```bash
watch -n 5 'vcgencmd measure_temp; vcgencmd get_throttled; vcgencmd measure_clock arm'
```

The display refreshes every five seconds. Temperature is in degrees Celsius;
the ARM clock is in Hz (approximately 2400000000 for 2.4 GHz).
`throttled=0x0` means no current or historical flags are set. A nonzero value
requires decoding the flags; it does not necessarily mean thermal throttling
is happening now. A loud fan alone does not establish throttling.

Stop either watch display with Ctrl+C. Closing these separate monitoring
terminals does not stop the benchmark running in tmux.
