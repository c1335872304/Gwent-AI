#!/bin/sh
set -eu

: "${GWENT_CORE_LIBRARY:=/opt/gwent/lib/libgwent_core.so}"

if [ ! -f "${GWENT_CORE_LIBRARY}" ]; then
    echo "trainer startup failed: Core library not found: ${GWENT_CORE_LIBRARY}" >&2
    exit 2
fi

exec "$@"
