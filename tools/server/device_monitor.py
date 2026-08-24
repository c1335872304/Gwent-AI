import time
import subprocess
import sys


def read_cpu_times():
    with open("/proc/stat", "r") as f:
        parts = f.readline().split()[1:]
    values = list(map(int, parts))
    idle = values[3] + values[4]
    total = sum(values)
    return idle, total


def cpu_percent(prev_idle, prev_total, idle, total):
    idle_delta = idle - prev_idle
    total_delta = total - prev_total
    if total_delta <= 0:
        return 0.0
    return 100.0 * (1.0 - idle_delta / total_delta)


def mem_percent():
    info = {}
    with open("/proc/meminfo", "r") as f:
        for line in f:
            key, value = line.split(":", 1)
            info[key] = int(value.strip().split()[0])

    total = info["MemTotal"]
    available = info["MemAvailable"]
    return (total - available) / total * 100.0


def gpu_info():
    try:
        out = subprocess.check_output(
            [
                "nvidia-smi",
                "--query-gpu=utilization.gpu,memory.used,memory.total",
                "--format=csv,noheader,nounits",
            ],
            text=True,
            stderr=subprocess.DEVNULL,
        ).strip()

        first = out.splitlines()[0]
        gpu_util, mem_used, mem_total = [float(x.strip()) for x in first.split(",")]
        gpu_mem_percent = mem_used / mem_total * 100.0 if mem_total > 0 else 0.0
        return gpu_util, gpu_mem_percent

    except Exception:
        return None, None


def main():
    interval = 2.0

    prev_idle, prev_total = read_cpu_times()

    print("开始监控，按 Ctrl+C 退出")
    print()

    try:
        while True:
            time.sleep(interval)

            idle, total = read_cpu_times()
            cpu = cpu_percent(prev_idle, prev_total, idle, total)
            prev_idle, prev_total = idle, total

            mem = mem_percent()
            gpu, gpu_mem = gpu_info()

            if gpu is None:
                line = f"CPU: {cpu:6.2f}% | 内存: {mem:6.2f}% | GPU: N/A | 显存: N/A"
            else:
                line = (
                    f"CPU: {cpu:6.2f}% | "
                    f"内存: {mem:6.2f}% | "
                    f"GPU: {gpu:6.2f}% | "
                    f"显存: {gpu_mem:6.2f}%"
                )

            sys.stdout.write("\r" + line + " " * 20)
            sys.stdout.flush()


    except KeyboardInterrupt:
        print("\n已退出监控")


if __name__ == "__main__":
    main()
