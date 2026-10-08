const js = require('@eslint/js');
const prettierConfig = require('eslint-config-prettier');

module.exports = [
    { ignores: ['static/vendor/**', 'static/js/lucide.min.js'] },
    js.configs.recommended,
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
                isNaN: 'readonly',
                localStorage: 'readonly',
                navigator: 'readonly',
                requestAnimationFrame: 'readonly',
                FormData: 'readonly',
                lucide: 'readonly',
                location: 'readonly',
                HTMLElement: 'readonly',
                Event: 'readonly',
                CustomEvent: 'readonly',
                getComputedStyle: 'readonly',
                Node: 'readonly',
                FileReader: 'readonly'
            }
        },
        rules: {
            'no-unused-vars': ['error', {
                'argsIgnorePattern': '^_',
                'caughtErrors': 'none',
                'varsIgnorePattern': '^(setTheme|openImage|resetShot|controlPiTrac|startBtn|stopBtn|restartBtn|calibration|showStatusMessage)$'
            }],
            'no-console': ['warn', { 'allow': ['warn', 'error'] }],
            'curly': ['error', 'multi-line'],
            'no-var': 'error',
            'prefer-const': 'error'
        }
    },
    prettierConfig
];
