#!/usr/bin/env bash
set -euo pipefail

# Development Environment Setup Script for PiTrac
# Installs and configures ZSH, Oh-My-ZSH, Neovim, and useful development tools
SCRIPT_DIR="$(cd -- "$(dirname "$0")" >/dev/null 2>&1 && pwd)"
source "${SCRIPT_DIR}/../common.sh"

# Load defaults from config file
load_defaults "dev-environment" "$@"

# Configuration
FORCE="${FORCE:-0}"
INSTALL_ZSH="${INSTALL_ZSH:-1}"
SET_ZSH_DEFAULT="${SET_ZSH_DEFAULT:-1}"
OMZ_THEME="${OMZ_THEME:-robbyrussell}"
OMZ_PLUGINS="${OMZ_PLUGINS:-git,docker,kubectl,npm,python,pip,sudo,command-not-found}"
INSTALL_NEOVIM="${INSTALL_NEOVIM:-1}"
NVIM_CONFIG="${NVIM_CONFIG:-basic}"
INSTALL_DEV_TOOLS="${INSTALL_DEV_TOOLS:-1}"
DEV_TOOLS="${DEV_TOOLS:-htop,ncdu,tree,jq,ripgrep,fd-find,bat,fzf,tmux}"
INSTALL_DOCKER="${INSTALL_DOCKER:-0}"
DOCKER_USER_GROUP="${DOCKER_USER_GROUP:-1}"
INSTALL_VSCODE_SERVER="${INSTALL_VSCODE_SERVER:-0}"
CONFIGURE_GIT="${CONFIGURE_GIT:-1}"
GIT_USER_NAME="${GIT_USER_NAME:-}"
GIT_USER_EMAIL="${GIT_USER_EMAIL:-}"
GIT_DEFAULT_BRANCH="${GIT_DEFAULT_BRANCH:-main}"
INSTALL_PYTHON_DEV="${INSTALL_PYTHON_DEV:-1}"
PYTHON_PACKAGES="${PYTHON_PACKAGES:-pip,setuptools,wheel,virtualenv,ipython,black,pylint}"
INSTALL_NODEJS="${INSTALL_NODEJS:-0}"
NODEJS_VERSION="${NODEJS_VERSION:-lts}"

# Prompt for yes/no with default
prompt_yes_no() {
  local prompt="$1"
  local default="${2:-n}"
  local result
  
  read -p "$prompt [y/N]: " result
  result="${result:-$default}"
  [[ "$result" =~ ^[Yy]$ ]]
}

# Install and configure ZSH with Oh-My-ZSH
install_zsh() {
  echo "=== ZSH and Oh-My-ZSH Installation ==="
  
  if need_cmd zsh && [ -d "$HOME/.oh-my-zsh" ]; then
    echo "ZSH and Oh-My-ZSH already installed"
    return 0
  fi
  
  log_info "Installing ZSH..."
  apt_ensure zsh curl
  
  # Install Oh-My-ZSH if not present
  if [ ! -d "$HOME/.oh-my-zsh" ]; then
    echo "Installing Oh-My-ZSH..."
    
    # Download and run Oh-My-ZSH installer
    sh -c "$(curl -fsSL https://raw.githubusercontent.com/ohmyzsh/ohmyzsh/master/tools/install.sh)" "" --unattended
    
    # Configure .zshrc with PiTrac-friendly settings
    setup_zshrc
  fi
  
  # Offer to change default shell
  if [ "$SHELL" != "$(which zsh)" ]; then
    echo ""
    if prompt_yes_no "Change default shell to ZSH? (requires logout/login to take effect)"; then
      echo "Changing default shell to ZSH..."
      chsh -s "$(which zsh)"
      echo "Default shell changed to ZSH"
      echo "Please logout and login for the change to take effect"
    fi
  fi
  
  echo "ZSH configuration completed"
}

# Set up ZSH configuration with PiTrac-friendly settings
setup_zshrc() {
  local zshrc="$HOME/.zshrc"
  
  # Backup original if it exists
  if [ -f "$zshrc" ] && [ ! -f "${zshrc}.ORIGINAL" ]; then
    cp "$zshrc" "${zshrc}.ORIGINAL"
  fi
  
  # Create comprehensive .zshrc
  cat > "$zshrc" << 'EOF'
# PiTrac ZSH Configuration
# PiTrac development environment

# Path configuration
export PATH=.:$HOME/bin:/usr/local/bin:$PATH

# Oh-My-ZSH configuration
export ZSH="$HOME/.oh-my-zsh"
ZSH_THEME="robbyrussell"

# Plugins
plugins=(git)

source $ZSH/oh-my-zsh.sh

# PiTrac-specific aliases and functions
alias ll='ls -al'
alias la='ls -la'
alias l='ls -l'

# Navigation aliases with directory stack
alias pushdd="pushd \$PWD > /dev/null"
alias cd='pushdd;cd'
alias popdd='popd >/dev/null'
alias cd.='popdd'
alias cd..='popdd;popdd'
alias cd...='popdd;popdd;popdd'
alias cd....='popdd;popdd;popdd;popdd'

# Remove directories from stack without changing location
alias .cd='popd -n +0'
alias ..cd='popd -n +0;popd -n +0;popd -n +0'

# PiTrac development aliases
alias cdlm='cd $PITRAC_ROOT/ImageProcessing 2>/dev/null || echo "PITRAC_ROOT not set"'
alias cddev='cd ~/Dev'

# Useful development functions
function findtext() {
    grep -rni "$1" .
}

function findfile() {
    find . -name "*$1*" -type f
}

# Git aliases
alias gst='git status'
alias glog='git log --oneline'
alias gdiff='git diff'

# System aliases  
alias soc='source ~/.zshrc'
alias zshconfig='nano ~/.zshrc'

# Enable vi mode in ZSH
bindkey -v

# Improved history
HISTSIZE=10000
SAVEHIST=10000
setopt HIST_IGNORE_DUPS
setopt HIST_FIND_NO_DUPS
setopt SHARE_HISTORY

# Auto-completion improvements
autoload -U compinit
compinit

# Disable git status in large repositories (performance)
# Run this in PiTrac directories: git config --add oh-my-zsh.hide-status 1
EOF

  echo "ZSH configuration created"
}

# Install and configure Neovim
install_neovim() {
  echo "=== Neovim Installation ==="
  
  if need_cmd nvim && [ -d "$HOME/.config/nvim" ]; then
    echo "Neovim already installed and configured"
    return 0
  fi
  
  log_info "Installing Neovim and dependencies..."
  apt_ensure neovim python3-neovim git
  
  # Create Neovim config directory
  mkdir -p "$HOME/.config/nvim"
  
  # Install Vundle for plugin management
  if [ ! -d "$HOME/.config/nvim/bundle/Vundle.vim" ]; then
    echo "Installing Vundle plugin manager..."
    git clone https://github.com/VundleVim/Vundle.vim.git "$HOME/.config/nvim/bundle/Vundle.vim"
    
    # Fix Vundle path for Neovim
    sed -i 's|$HOME/.vim/bundle|$HOME/.config/nvim/bundle|g' "$HOME/.config/nvim/bundle/Vundle.vim/autoload/vundle.vim"
  fi
  
  # Create Neovim configuration
  setup_neovim_config
  
  echo "Neovim configuration completed"
  echo "To install plugins, run: nvim +PluginInstall +qall"
}

# Set up Neovim configuration
setup_neovim_config() {
  local nvim_config="$HOME/.config/nvim/init.vim"
  
  cat > "$nvim_config" << 'EOF'
" PiTrac Neovim Configuration
" PiTrac Neovim configuration

set nocompatible              " be iMproved, required
filetype off                  " required

" set the runtime path to include Vundle and initialize
set rtp+=~/.config/nvim/bundle/Vundle.vim
call vundle#begin()            " required

" Let Vundle manage Vundle, required
Plugin 'VundleVim/Vundle.vim'  

" ===================
" Development plugins
" ===================
Plugin 'scrooloose/nerdtree'           " File explorer
Plugin 'tpope/vim-fugitive'            " Git integration
Plugin 'airblade/vim-gitgutter'        " Git diff in gutter
Plugin 'vim-airline/vim-airline'       " Status line
Plugin 'vim-airline/vim-airline-themes'
Plugin 'ctrlpvim/ctrlp.vim'           " Fuzzy file finder

" Language support
Plugin 'sheerun/vim-polyglot'          " Language pack

" ===================
" end of plugins
" ===================
call vundle#end()               " required
filetype plugin indent on       " required

" ===================
" Basic editor settings
" ===================
set number                      " Line numbers
set relativenumber             " Relative line numbers
set mouse=a                    " Enable mouse
set clipboard=unnamedplus      " Use system clipboard
set ignorecase                 " Case insensitive search
set smartcase                  " Case sensitive if uppercase present
set hlsearch                   " Highlight search results
set incsearch                  " Incremental search
set expandtab                  " Use spaces instead of tabs
set shiftwidth=2              " Indentation width
set tabstop=2                 " Tab width
set autoindent                " Auto indentation
set smartindent               " Smart indentation
set wrap                      " Wrap long lines
set scrolloff=8               " Keep 8 lines above/below cursor
set colorcolumn=80            " Show column at 80 characters
set updatetime=300            " Faster completion

" ===================
" Key mappings
" ===================
" Leader key
let mapleader = " "

" NERDTree
map <C-n> :NERDTreeToggle<CR>
map <leader>n :NERDTreeFind<CR>

" Easy window navigation
nnoremap <C-h> <C-w>h
nnoremap <C-j> <C-w>j
nnoremap <C-k> <C-w>k
nnoremap <C-l> <C-w>l

" Clear search highlighting
nnoremap <leader>h :nohlsearch<CR>

" Quick save and quit
nnoremap <leader>w :w<CR>
nnoremap <leader>q :q<CR>

" ===================
" Theme and appearance
" ===================
syntax enable
set termguicolors
colorscheme default

" Airline configuration
let g:airline#extensions#tabline#enabled = 1
let g:airline_powerline_fonts = 1

" ===================
" NERDTree settings
" ===================
let NERDTreeShowHidden=1
let NERDTreeIgnore=['\.git$', '\.swp$', '\.DS_Store$']

" ===================
" Auto commands
" ===================
" Auto-close NERDTree if it's the only window left
autocmd BufEnter * if winnr('$') == 1 && exists('b:NERDTree') && b:NERDTree.isTabTree() | quit | endif

" Remember cursor position
autocmd BufReadPost * if line("'\"") > 1 && line("'\"") <= line("$") | exe "normal! g'\"" | endif
EOF

  echo "Neovim configuration created"
}

# Install additional development tools
install_dev_tools() {
  echo "=== Additional Development Tools ==="
  
  log_info "Installing useful development tools..."
  apt_ensure tree htop git-extras ripgrep fd-find bat
  
  # Create symlinks for fd and bat (they have different names on Debian)
  if [ ! -f /usr/local/bin/fd ] && [ -f /usr/bin/fdfind ]; then
    $SUDO ln -sf /usr/bin/fdfind /usr/local/bin/fd
  fi
  
  if [ ! -f /usr/local/bin/bat ] && [ -f /usr/bin/batcat ]; then
    $SUDO ln -sf /usr/bin/batcat /usr/local/bin/bat
  fi
  
  echo "Development tools installed"
}

# Set up Git configuration helpers
setup_git_config() {
  echo "=== Git Configuration ==="
  
  # Check if git is already configured
  if git config --global user.name >/dev/null 2>&1; then
    echo "Git already configured for user: $(git config --global user.name)"
    return 0
  fi
  
  if prompt_yes_no "Configure Git user settings?"; then
    read -p "Git username: " git_name
    read -p "Git email: " git_email
    
    if [ -n "$git_name" ] && [ -n "$git_email" ]; then
      git config --global user.name "$git_name"
      git config --global user.email "$git_email"
      git config --global init.defaultBranch main
      git config --global pull.rebase false
      echo "Git configuration completed"
    fi
  fi
}

# Check if dev environment is installed
is_dev_environment_installed() {
  # Check if ZSH and Oh-My-ZSH are installed
  need_cmd zsh && [ -d "$HOME/.oh-my-zsh" ] && return 0
  return 1
}

# Main setup
setup_dev_environment() {
  echo "=== Development Environment Setup ==="
  echo "Install and configure development tools for easier PiTrac development"
  echo ""
  
  # Check if already configured
  if is_dev_environment_installed && [ "$FORCE" != "1" ]; then
    echo "Development environment appears to already be configured."
    echo "Set FORCE=1 to reconfigure"
    return 0
  fi
  
  echo "This will install and configure:"
  echo "- ZSH with Oh-My-ZSH (improved shell)"
  echo "- Neovim with plugins (enhanced editor)"  
  echo "- Development tools (ripgrep, fd, bat, etc.)"
  echo "- Git configuration"
  echo "- Useful aliases and functions"
  echo ""
  
  if ! prompt_yes_no "Continue with development environment setup?"; then
    echo "Development environment setup cancelled"
    return 0
  fi
  
  # Run setup steps
  install_zsh
  install_neovim
  install_dev_tools
  setup_git_config
  
  echo ""
  echo "Development environment setup completed!"
  echo ""
  echo "Installed components:"
  echo "- ZSH with Oh-My-ZSH: $(need_cmd zsh && echo "YES" || echo "NO")"
  echo "- Neovim: $(need_cmd nvim && echo "YES" || echo "NO")"
  echo "- Development tools: $(need_cmd rg && echo "YES" || echo "NO")"
  echo ""
  echo "Useful commands:"
  echo "- Switch to ZSH: zsh"
  echo "- Edit with Neovim: nvim filename"
  echo "- Install Neovim plugins: nvim +PluginInstall +qall"
  echo "- Reload ZSH config: source ~/.zshrc"
  echo ""
  echo "If you changed your default shell to ZSH, logout and login for it to take effect."
}

# Run if called directly
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
  setup_dev_environment
fi