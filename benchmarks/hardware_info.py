"""Detect the host hardware and SIMD features used by the benchmark runner."""
from __future__ import annotations

import json
import platform
import re
import shutil
import subprocess
from dataclasses import asdict, dataclass


@dataclass(frozen=True)
class HardwareInfo:
    cpu: str
    architecture: str
    logical_cpus: int
    instruction_sets: tuple[str, ...]
    gpus: tuple[str, ...]


def _command(*args: str) -> str | None:
    executable = shutil.which(args[0])
    if not executable:
        return None
    try:
        return subprocess.check_output((executable, *args[1:]), text=True,
                                       stderr=subprocess.DEVNULL, timeout=3).strip()
    except (OSError, subprocess.SubprocessError):
        return None


def detect() -> HardwareInfo:
    system = platform.system()
    cpu = platform.processor() or platform.machine() or "unbekannt"
    flags = set()
    if system == "Linux":
        try:
            text = open("/proc/cpuinfo", encoding="utf-8").read().lower()
            flags.update(re.findall(r"(?:^|\s)(avx512[^\s]*|avx2|avx|fma|neon|asimd|sse4_2|sse4_1)(?:\s|$)", text, re.M))
            for line in text.splitlines():
                if line.startswith("model name"):
                    cpu = line.split(":", 1)[-1].strip()
                    break
        except OSError:
            pass
    elif system == "Windows":
        cpu = _command("wmic", "cpu", "get", "name") or cpu
    features = []
    if any(f in flags for f in ("neon", "asimd")):
        features.append("NEON (128-bit)")
    if any(f.startswith("sse") for f in flags):
        features.append("SSE (128-bit)")
    if "avx" in flags:
        features.append("AVX (128-bit)")
    if "avx2" in flags:
        features.append("AVX2 (256-bit)")
    if "fma" in flags:
        features.append("FMA")
    if any(f.startswith("avx512") for f in flags):
        features.append("AVX-512 (512-bit)")
    gpus = []
    nvidia = _command("nvidia-smi", "--query-gpu=name", "--format=csv,noheader")
    if nvidia:
        gpus.extend(line.strip() for line in nvidia.splitlines() if line.strip())
    elif (rocm := _command("rocminfo")):
        gpus.extend(dict.fromkeys(line.split(":", 1)[1].strip() for line in rocm.splitlines()
                                  if "Marketing Name:" in line))
    if not gpus and (vulkan := _command("vulkaninfo", "--summary")):
        # Ignore CPU software renderers when reporting compute hardware.
        for block in re.split(r"GPU[0-9]+:", vulkan)[1:]:
            if re.search(r"deviceType\s*=\s*PHYSICAL_DEVICE_TYPE_(INTEGRATED|DISCRETE)_GPU", block):
                name = re.search(r"deviceName\s*=\s*(.+)", block)
                if name:
                    gpus.append(name.group(1).strip())
    return HardwareInfo(cpu, platform.machine(), __import__("os").cpu_count() or 1,
                        tuple(features) or ("keine erkannten SIMD-Erweiterungen",),
                        tuple(gpus) or ("keine GPU erkannt",))


def format_info(info: HardwareInfo) -> str:
    return (f"Hardware: CPU {info.cpu} ({info.architecture}, {info.logical_cpus} logische CPUs)\n"
            f"  SIMD/FMA: {', '.join(info.instruction_sets)}\n"
            f"  GPU: {', '.join(info.gpus)}")


if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()
    info = detect()
    print(json.dumps(asdict(info), ensure_ascii=False, indent=2) if args.json else format_info(info))
