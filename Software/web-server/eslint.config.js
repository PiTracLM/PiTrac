module.exports = [
    {
        files: ['**/*.js'],
        languageOptions: {
            ecmaVersion: 2021,
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
            'indent': ['error', 4],
            'linebreak-style': ['error', 'unix'],
            'quotes': ['error', 'single'],
            'semi': ['error', 'always'],
            'no-trailing-spaces': 'error',
            'no-unused-vars': ['error', {
                'argsIgnorePattern': '^_|^e$',
                'varsIgnorePattern': '^(saveChanges|resetAll|reloadConfig|showDiff|exportConfig|importConfig|filterConfig|closeModal|setTheme|openImage|resetShot|controlPiTrac|startBtn|stopBtn|restartBtn)$'
            }],
            'no-console': ['warn', { 'allow': ['warn', 'error'] }],
            'comma-dangle': ['error', 'never'],
            'no-multiple-empty-lines': ['error', { 'max': 1, 'maxEOF': 0 }],
            'eol-last': ['error', 'always'],
            'space-before-function-paren': ['error', {
                'anonymous': 'never',
                'named': 'never',
                'asyncArrow': 'always'
            }],
            'object-curly-spacing': ['error', 'always'],
            'array-bracket-spacing': ['error', 'never'],
            'space-in-parens': ['error', 'never'],
            'keyword-spacing': ['error', { 'before': true, 'after': true }],
            'space-infix-ops': 'error',
            'comma-spacing': ['error', { 'before': false, 'after': true }],
            'brace-style': ['error', '1tbs', { 'allowSingleLine': true }],
            'curly': ['error', 'multi-line'],
            'no-var': 'error',
            'prefer-const': 'error'
        }
    }
];