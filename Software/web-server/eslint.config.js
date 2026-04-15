const prettierConfig = require('eslint-config-prettier');

module.exports = [
    {
        files: ['**/*.js'],
        languageOptions: {
            ecmaVersion: 'latest',
            sourceType: 'script',
            globals: {
                window: 'readonly',
                document: 'readonly',
                console: 'readonly',
                fetch: 'readonly',
                WebSocket: 'readonly',
                URL: 'readonly',
                Blob: 'readonly',
                Promise: 'readonly',
                setTimeout: 'readonly',
                setInterval: 'readonly',
                clearInterval: 'readonly',
                clearTimeout: 'readonly',
                alert: 'readonly',
                confirm: 'readonly',
                JSON: 'readonly',
                Object: 'readonly',
                Array: 'readonly',
                Number: 'readonly',
                String: 'readonly',
                isNaN: 'readonly'
            }
        },
        rules: {
            'no-unused-vars': ['error', {
                'argsIgnorePattern': '^_|^e$',
                'varsIgnorePattern': '^(saveChanges|resetAll|reloadConfig|showDiff|exportConfig|importConfig|filterConfig|closeModal|setTheme|openImage|resetShot|controlPiTrac|startBtn|stopBtn|restartBtn|calibration)$'
            }],
            'no-console': ['warn', { 'allow': ['warn', 'error'] }],
            'curly': ['error', 'multi-line'],
            'no-var': 'error',
            'prefer-const': 'error'
        }
    },
    prettierConfig
];
