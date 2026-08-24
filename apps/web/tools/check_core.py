from __future__ import annotations

import argparse
import json
import urllib.request


def main() -> int:
    parser = argparse.ArgumentParser(description="Check a running gwent-core HTTP server.")
    parser.add_argument("--url", default="http://127.0.0.1:8008")
    args = parser.parse_args()

    with urllib.request.urlopen(f"{args.url.rstrip('/')}/health", timeout=5) as response:
        payload = json.load(response)

    print(json.dumps(payload, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
