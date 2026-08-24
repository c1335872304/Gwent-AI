from __future__ import annotations

import argparse
import os
import shutil
import socket
import subprocess
import sys
import time
import urllib.error
import urllib.request


def local_port_is_open(port: int) -> bool:
    try:
        with socket.create_connection(("127.0.0.1", port), timeout=1):
            return True
    except OSError:
        return False


def check_core_health(local_port: int) -> tuple[bool, str]:
    url = f"http://127.0.0.1:{local_port}/health"
    try:
        with urllib.request.urlopen(url, timeout=3) as response:
            return True, response.read().decode("utf-8")
    except urllib.error.HTTPError as exc:
        return False, f"HTTP {exc.code}: {exc.reason}"
    except urllib.error.URLError as exc:
        return False, str(exc.reason)
    except Exception as exc:  # pragma: no cover - CLI diagnostics
        return False, str(exc)


def main() -> int:
    parser = argparse.ArgumentParser(description="Open an SSH tunnel to a remote Gwent Core HTTP server.")
    parser.add_argument("--ssh-host", default=os.environ.get("GWENT_SSH_HOST"))
    parser.add_argument("--ssh-user", default=os.environ.get("GWENT_SSH_USER"))
    parser.add_argument("--ssh-port", type=int, default=int(os.environ.get("GWENT_SSH_PORT", "22")))
    parser.add_argument("--local-port", type=int, default=8008)
    parser.add_argument("--remote-host", default="127.0.0.1")
    parser.add_argument("--remote-port", type=int, default=8008)
    args = parser.parse_args()

    if not args.ssh_host or not args.ssh_user:
        parser.error("--ssh-host and --ssh-user are required (or set GWENT_SSH_HOST / GWENT_SSH_USER)")

    ssh = shutil.which("ssh")
    if not ssh:
        print("ssh executable was not found. Install/enable OpenSSH Client first.", file=sys.stderr)
        return 1

    command = [
        ssh,
        "-N",
        "-L", f"{args.local_port}:{args.remote_host}:{args.remote_port}",
        "-p", str(args.ssh_port),
        "-o", "ExitOnForwardFailure=yes",
        "-o", "ConnectTimeout=10",
        "-o", "ServerAliveInterval=30",
        "-o", "ServerAliveCountMax=3",
        f"{args.ssh_user}@{args.ssh_host}",
    ]

    print(f"Opening tunnel: localhost:{args.local_port} -> {args.remote_host}:{args.remote_port} via {args.ssh_host}")
    process: subprocess.Popen[bytes] | None = None
    try:
        process = subprocess.Popen(command)
        for _ in range(20):
            if process.poll() is not None:
                print(f"SSH exited with code {process.returncode}", file=sys.stderr)
                return int(process.returncode or 1)
            if local_port_is_open(args.local_port):
                break
            time.sleep(0.5)
        else:
            print("SSH is running but the local port did not open.", file=sys.stderr)
            return 1

        ok, result = check_core_health(args.local_port)
        print("Core health:", result)
        if not ok:
            print("Tunnel is open, but the remote Core did not answer /health yet.")

        return int(process.wait())
    except KeyboardInterrupt:
        if process is not None and process.poll() is None:
            process.terminate()
        return 0


if __name__ == "__main__":
    raise SystemExit(main())
