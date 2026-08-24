from __future__ import annotations

import warnings

import torch


def _device_works(device: torch.device) -> bool:
    try:
        x = torch.empty((1,), device=device)
        if device.type == "cuda":
            torch.cuda.synchronize(device)
        del x
        return True
    except Exception as exc:  # pragma: no cover - hardware dependent
        warnings.warn(f"Torch device {device} is not usable; falling back if possible: {exc}", RuntimeWarning)
        return False


def resolve_torch_device(preference: str | torch.device | None = "auto") -> torch.device:
    """Resolve a training/inference device, preferring GPU for the default path.

    `auto` means: try CUDA first, then Apple MPS, then CPU.  Explicit GPU
    requests still fall back to CPU if the runtime reports that the device is
    unavailable or cannot allocate a tiny tensor.  This keeps smoke tests and
    developer laptops usable while making GPU the normal training default.
    """
    if isinstance(preference, torch.device):
        pref = str(preference)
    else:
        pref = str(preference or "auto").strip().lower()

    candidates: list[torch.device]
    if pref in {"auto", "gpu", "cuda_if_available"}:
        candidates = []
        if torch.cuda.is_available():
            candidates.append(torch.device("cuda"))
        mps = getattr(torch.backends, "mps", None)
        if mps is not None and mps.is_available():
            candidates.append(torch.device("mps"))
        candidates.append(torch.device("cpu"))
    else:
        requested = torch.device(pref)
        candidates = [requested]
        if requested.type != "cpu":
            candidates.append(torch.device("cpu"))

    for device in candidates:
        if device.type == "cuda" and not torch.cuda.is_available():
            continue
        if device.type == "mps":
            mps = getattr(torch.backends, "mps", None)
            if mps is None or not mps.is_available():
                continue
        if device.type == "cpu" or _device_works(device):
            return device
    return torch.device("cpu")


def device_name(device: torch.device) -> str:
    if device.type == "cuda" and torch.cuda.is_available():
        try:
            return f"cuda:{torch.cuda.current_device()} ({torch.cuda.get_device_name(torch.cuda.current_device())})"
        except Exception:  # pragma: no cover - hardware dependent
            return "cuda"
    return str(device)
