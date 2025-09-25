#!/bin/bash
set -e

case "$1" in
    remove|upgrade|deconfigure)
        systemctl stop pitrac-web 2>/dev/null || true
        systemctl disable pitrac-web 2>/dev/null || true
        ;;
    
    failed-upgrade)
        ;;
    
    *)
        echo "prerm called with unknown argument: $1" >&2
        exit 1
        ;;
esac

exit 0