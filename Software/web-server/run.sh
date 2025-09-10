#!/bin/bash

MODE=${1:-dev}
PORT=${2:-8080}

echo "Starting PiTrac Web Server"
echo "   Mode: $MODE"
echo "   Port: $PORT"
echo ""

if [ "$MODE" = "dev" ]; then
    echo "Development mode - Auto-reload enabled"
    echo "   Changes to .py and .html files will auto-reload"
    echo ""
    python3 main.py
elif [ "$MODE" = "prod" ]; then
    echo "Production mode - Optimized performance"
    echo ""
    uvicorn main:app --host 0.0.0.0 --port $PORT --workers 2
else
    echo "Unknown mode: $MODE"
    echo "   Usage: ./run.sh [dev|prod] [port]"
    echo "   Example: ./run.sh dev 8080"
    exit 1
fi