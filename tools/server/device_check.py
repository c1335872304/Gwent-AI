import os
import platform
import socket

import torch

try:
    import psutil
except ImportError:
    psutil = None


def gb(value):
    return value / 1024**3


print("=" * 60)
print("Server")
print("=" * 60)
print("Host:", socket.gethostname())
print("OS:", platform.platform())
print("Python:", platform.python_version())
print("PyTorch:", torch.__version__)


print("\n" + "=" * 60)
print("CPU")
print("=" * 60)

print("CPU model:", platform.processor() or "Unknown")
print("Logical CPUs:", os.cpu_count())

if psutil:
    print("Physical CPUs:", psutil.cpu_count(logical=False))
    print("CPU usage:", f"{psutil.cpu_percent(interval=1):.1f}%")
else:
    print("Physical CPUs: psutil not installed")
    print("CPU usage: psutil not installed")


print("\n" + "=" * 60)
print("Memory")
print("=" * 60)

if psutil:
    mem = psutil.virtual_memory()
    print("Total RAM:", f"{gb(mem.total):.2f} GB")
    print("Used RAM:", f"{gb(mem.used):.2f} GB")
    print("Available RAM:", f"{gb(mem.available):.2f} GB")
    print("RAM usage:", f"{mem.percent:.1f}%")
else:
    print("Memory info: psutil not installed")


print("\n" + "=" * 60)
print("GPU / CUDA")
print("=" * 60)

print("CUDA available:", torch.cuda.is_available())
print("CUDA version:", torch.version.cuda)
print("GPU count:", torch.cuda.device_count())

if torch.cuda.is_available():
    for i in range(torch.cuda.device_count()):
        props = torch.cuda.get_device_properties(i)

        print(f"\nGPU {i}:")
        print("  Name:", props.name)
        print("  VRAM:", f"{gb(props.total_memory):.2f} GB")

        free_mem, total_mem = torch.cuda.mem_get_info(i)

        print("  VRAM free:", f"{gb(free_mem):.2f} GB")
        print("  VRAM used:", f"{gb(total_mem - free_mem):.2f} GB")
        print(
            "  Compute capability:",
            f"{props.major}.{props.minor}",
        )


print("\n" + "=" * 60)
print("PyTorch device")
print("=" * 60)

device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
print("Selected device:", device)
