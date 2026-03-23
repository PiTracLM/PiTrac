#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
cd "$SCRIPT_DIR"

command_exists() {
    command -v "$1" >/dev/null 2>&1
}

install_bashly_locally() {
    echo "Docker not found, setting up Bashly locally..."
    
    if ! command_exists ruby; then
        echo "Installing Ruby..."
        if command_exists apt-get; then
            sudo apt-get update
            sudo apt-get install -y ruby ruby-dev build-essential
        elif command_exists yum; then
            sudo yum install -y ruby ruby-devel gcc make
        elif command_exists brew; then
            brew install ruby
        else
            echo "Error: Cannot install Ruby automatically. Please install Ruby manually."
            exit 1
        fi
    fi
    
    if ! command_exists bashly; then
        echo "Installing Bashly gem..."
        if command_exists sudo; then
            sudo gem install bashly
        else
            gem install bashly
        fi
    fi
    
    echo "✓ Bashly is ready"
}

generate_with_docker() {
    echo "Using Docker to generate Bashly script..."
    
    docker build -f Dockerfile.bashly -t bashly-generator .
    
    if [[ -f "settings.yml" ]]; then
        docker run --rm \
            -v "$PWD:/app" \
            -v "$PWD/bashly.yml:/app/src/bashly.yml" \
            -v "$PWD/settings.yml:/app/settings.yml" \
            bashly-generator bashly generate
    else
        docker run --rm \
            -v "$PWD:/app" \
            -v "$PWD/bashly.yml:/app/src/bashly.yml" \
            bashly-generator bashly generate
    fi
}

generate_locally() {
    echo "Using local Bashly to generate script..."
    
    mkdir -p src
    cp bashly.yml src/bashly.yml
    
    if [[ -f "settings.yml" ]]; then
        cp settings.yml src/settings.yml
    fi
    
    bashly generate
    
    rm -rf src/bashly.yml src/settings.yml
    rmdir src 2>/dev/null || true
}

if command_exists docker && docker info >/dev/null 2>&1; then
    generate_with_docker
elif command_exists bashly; then
    generate_locally
else
    install_bashly_locally
    generate_locally
fi

chmod +x pitrac

echo "✓ Generated pitrac script"
echo "Test with: ./pitrac --help"