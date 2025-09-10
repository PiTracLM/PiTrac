.PHONY: help install test test-unit test-integration test-websocket test-coverage test-ci test-modules clean run dev lint type-check

help:
	@echo "PiTrac Web Server -"
	@echo "============================================"
	@echo ""
	@echo "Installation:"
	@echo "  make install        - Install all dependencies"
	@echo "  make install-dev    - Install development dependencies"
	@echo ""
	@echo "Testing:"
	@echo "  make test          - Run all tests with coverage"
	@echo "  make test-unit     - Run unit tests only"
	@echo "  make test-integration - Run integration tests only"
	@echo "  make test-websocket - Run WebSocket tests only"
	@echo "  make test-smoke    - Run smoke tests (basic functionality)"
	@echo "  make test-coverage - Generate detailed coverage report"
	@echo "  make test-ci       - Run tests in CI mode"
	@echo "  make test-watch    - Run tests in watch mode"
	@echo ""
	@echo "Module Testing:"
	@echo "  make test-models   - Test data models"
	@echo "  make test-managers - Test connection/store managers"
	@echo "  make test-parsers  - Test data parsers"
	@echo "  make test-listeners - Test ActiveMQ listeners"
	@echo "  make test-server   - Test server/API endpoints"
	@echo ""
	@echo "Code Quality:"
	@echo "  make lint          - Run linting checks"
	@echo "  make type-check    - Run type checking with mypy"
	@echo "  make format        - Format code with black"
	@echo ""
	@echo "Development:"
	@echo "  make run           - Run the web server"
	@echo "  make dev           - Run in development mode with auto-reload"
	@echo "  make clean         - Clean test artifacts and cache"

install:
	pip install -r requirements.txt
	pip install -r requirements-test.txt

install-dev:
	pip install -r requirements.txt
	pip install -r requirements-test.txt
	pip install -r requirements-dev.txt

test:
	python run_tests.py

test-unit:
	python run_tests.py --quick

test-integration:
	python run_tests.py --integration

test-websocket:
	pytest -v -m websocket tests/test_websocket.py

test-smoke:
	python run_tests.py --smoke

test-coverage:
	python run_tests.py --full

test-ci:
	python run_tests.py --ci

test-watch:
	python run_tests.py --watch

test-models:
	python run_tests.py --module models

test-managers:
	python run_tests.py --module managers

test-parsers:
	python run_tests.py --module parsers

test-listeners:
	python run_tests.py --module listeners

test-server:
	python run_tests.py --module server

lint:
	@echo "Running ruff linter..."
	ruff check . --select E,F,W --ignore E501 || true
	@echo ""
	@echo "Checking for common issues..."
	@grep -r "print(" --include="*.py" --exclude-dir=tests . || true

type-check:
	@echo "Running mypy type checker..."
	mypy models.py managers.py parsers.py listeners.py server.py \
		--ignore-missing-imports \
		--no-error-summary \
		--pretty

format:
	@echo "Formatting code with black..."
	black models.py managers.py parsers.py listeners.py server.py constants.py main.py \
		--line-length 100 \
		--skip-string-normalization

run:
	python main.py

dev:
	uvicorn main:app --reload --host 0.0.0.0 --port 8080

clean:
	@echo "Cleaning test artifacts..."
	rm -rf .pytest_cache
	rm -rf htmlcov
	rm -rf .coverage
	rm -rf coverage.xml
	rm -rf test-results.xml
	rm -rf __pycache__
	rm -rf tests/__pycache__
	find . -type d -name "__pycache__" -exec rm -rf {} + 2>/dev/null || true
	find . -type f -name "*.pyc" -delete
	find . -type f -name ".DS_Store" -delete
	@echo "Clean complete!"

.PHONY: ci-test ci-lint ci-all

ci-test:
	python run_tests.py --ci

ci-lint:
	ruff check . --select E,F,W --ignore E501 --exit-non-zero-on-fix

ci-all: ci-lint ci-test
	@echo "CI checks complete!"