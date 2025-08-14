#!/usr/bin/env python3
#!/usr/bin/env python3

import os
import sys
import json
from pathlib import Path
from http.server import HTTPServer, SimpleHTTPRequestHandler
import socket

def load_config():
    config = {}
    script_dir = Path(__file__).parent
    defaults_file = script_dir / "defaults" / "test-processor.yaml"
    
    if defaults_file.exists():
        try:
            import yaml
            with open(defaults_file) as f:
                config = yaml.safe_load(f)
        except ImportError:
            with open(defaults_file) as f:
                for line in f:
                    if ':' in line and not line.strip().startswith('#'):
                        key, value = line.split(':', 1)
                        config[key.strip()] = value.strip().strip('"')
    
    return config

class TestResultsHandler(SimpleHTTPRequestHandler):
    
    def __init__(self, *args, results_dir=None, **kwargs):
        self.results_dir = results_dir
        super().__init__(*args, **kwargs)
    
    def do_GET(self):
        if self.path == '/':
            self.serve_index()
        elif self.path == '/api/results':
            self.serve_results_json()
        elif self.path.startswith('/images/'):
            self.serve_static_file(self.path[8:], 'images')
        elif self.path.startswith('/logs/'):
            self.serve_static_file(self.path[6:], 'logs')
        else:
            self.send_error(404)
    
    def serve_index(self):
        html = """<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>PiTrac Test Results</title>
    <style>
        body {
            font-family: 'Courier New', monospace;
            background: #1e1e1e;
            color: #d4d4d4;
            padding: 20px;
            margin: 0;
        }
        .container {
            max-width: 1200px;
            margin: 0 auto;
        }
        h1 {
            color: #4ec9b0;
            border-bottom: 2px solid #4ec9b0;
            padding-bottom: 10px;
        }
        .result {
            background: #2d2d30;
            border: 1px solid #3e3e42;
            border-radius: 4px;
            padding: 15px;
            margin-bottom: 15px;
        }
        .result-header {
            display: flex;
            justify-content: space-between;
            margin-bottom: 10px;
            color: #9cdcfe;
            font-weight: bold;
        }
        .metrics {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
            gap: 10px;
            margin-top: 10px;
        }
        .metric {
            background: #1e1e1e;
            padding: 8px;
            border-radius: 3px;
        }
        .metric-label {
            color: #858585;
            font-size: 0.9em;
        }
        .metric-value {
            color: #d7ba7d;
            font-size: 1.2em;
            font-weight: bold;
        }
        .status-success { color: #4ec9b0; }
        .status-error { color: #f48771; }
        .loading { text-align: center; padding: 50px; }
        .refresh-btn {
            background: #007acc;
            color: white;
            border: none;
            padding: 8px 16px;
            border-radius: 3px;
            cursor: pointer;
            font-family: inherit;
        }
        .refresh-btn:hover { background: #005a9e; }
        a { color: #4ec9b0; }
        .no-results { text-align: center; padding: 50px; color: #858585; }
    </style>
</head>
<body>
    <div class="container">
        <h1>PiTrac Test Results</h1>
        <div style="margin-bottom: 20px;">
            <button class="refresh-btn" onclick="loadResults()">Refresh</button>
            <span style="margin-left: 20px; color: #858585;">Auto-refresh: <span id="countdown">10</span>s</span>
        </div>
        <div id="results">
            <div class="loading">Loading results...</div>
        </div>
    </div>
    
    <script>
        let countdown = 10;
        let countdownInterval;
        
        async function loadResults() {
            countdown = 10;
            try {
                const response = await fetch('/api/results');
                const data = await response.json();
                const resultsDiv = document.getElementById('results');
                
                if (data.error) {
                    resultsDiv.innerHTML = `<div class="no-results">Error: ${data.error}</div>`;
                    return;
                }
                
                if (!data.results || data.results.length === 0) {
                    resultsDiv.innerHTML = '<div class="no-results">No test results found. Run a test first!</div>';
                    return;
                }
                
                let html = '';
                for (const result of data.results) {
                    const statusClass = result.status === 'success' ? 'status-success' : 'status-error';
                    html += `
                        <div class="result">
                            <div class="result-header">
                                <span>${result.name}</span>
                                <span class="${statusClass}">${result.status.toUpperCase()}</span>
                            </div>
                            <div>Timestamp: ${result.timestamp}</div>
                            <div class="metrics">
                                <div class="metric">
                                    <div class="metric-label">Duration</div>
                                    <div class="metric-value">${result.duration}</div>
                                </div>
                                ${result.ball_speed ? `
                                <div class="metric">
                                    <div class="metric-label">Ball Speed</div>
                                    <div class="metric-value">${result.ball_speed} mph</div>
                                </div>` : ''}
                                ${result.launch_angle ? `
                                <div class="metric">
                                    <div class="metric-label">Launch Angle</div>
                                    <div class="metric-value">${result.launch_angle}°</div>
                                </div>` : ''}
                            </div>
                            ${result.log_file ? `
                            <div style="margin-top: 10px;">
                                <a href="/logs/${result.log_file}" target="_blank">View Log</a>
                            </div>` : ''}
                        </div>
                    `;
                }
                resultsDiv.innerHTML = html;
            } catch (error) {
                document.getElementById('results').innerHTML = 
                    `<div class="no-results">Failed to load results: ${error.message}</div>`;
            }
        }
        
        // Countdown timer
        function updateCountdown() {
            countdown--;
            document.getElementById('countdown').textContent = countdown;
            if (countdown <= 0) {
                loadResults();
            }
        }
        
        // Initial load
        loadResults();
        
        // Set up auto-refresh
        countdownInterval = setInterval(updateCountdown, 1000);
    </script>
</body>
</html>"""
        
        self.send_response(200)
        self.send_header('Content-Type', 'text/html; charset=utf-8')
        self.end_headers()
        self.wfile.write(html.encode())
    
    def serve_results_json(self):
        try:
            results = []
            results_dir = Path(self.results_dir)
            
            # Find all timing files
            timing_files = sorted(
                results_dir.glob("data/timing_*.txt"),
                key=lambda x: x.stat().st_mtime,
                reverse=True
            )[:10]  # Last 10 results
            
            for timing_file in timing_files:
                result = self.parse_timing_file(timing_file)
                
                # Find associated log file
                test_name = timing_file.stem.replace('timing_', '')
                log_files = list(results_dir.glob(f"logs/test_{test_name}.log"))
                if log_files:
                    result['log_file'] = log_files[0].name
                
                results.append(result)
            
            response = {'results': results}
            
        except Exception as e:
            response = {'error': str(e)}
        
        self.send_response(200)
        self.send_header('Content-Type', 'application/json')
        self.end_headers()
        self.wfile.write(json.dumps(response).encode())
    
    def parse_timing_file(self, timing_file):
        result = {
            'name': 'Unknown',
            'timestamp': 'Unknown',
            'duration': 'N/A',
            'status': 'unknown'
        }
        
        try:
            with open(timing_file, 'r') as f:
                content = f.read()
                
                for line in content.split('\n'):
                    if line.startswith('Test:'):
                        result['name'] = line.split(':', 1)[1].strip()
                    elif line.startswith('Timestamp:'):
                        result['timestamp'] = line.split(':', 1)[1].strip()
                    elif line.startswith('Duration:'):
                        duration = line.split(':', 1)[1].strip()
                        # Format duration nicely
                        if 'seconds' in duration:
                            try:
                                seconds = float(duration.split()[0])
                                result['duration'] = f"{seconds:.2f}s"
                            except:
                                result['duration'] = duration
                        else:
                            result['duration'] = duration
                    elif 'ball speed' in line.lower():
                        parts = line.split(':')
                        if len(parts) > 1:
                            result['ball_speed'] = parts[1].strip().split()[0]
                    elif 'launch angle' in line.lower():
                        parts = line.split(':')
                        if len(parts) > 1:
                            result['launch_angle'] = parts[1].strip().split()[0]
                
                if 'ERROR' in content or 'FAILED' in content:
                    result['status'] = 'error'
                elif result['duration'] != 'N/A':
                    result['status'] = 'success'
                else:
                    result['status'] = 'unknown'
                
        except Exception as e:
            print(f"Error parsing timing file {timing_file}: {e}", file=sys.stderr)
        
        return result
    
    def serve_static_file(self, filename, subdir):
        file_path = Path(self.results_dir) / subdir / filename
        
        if file_path.exists() and file_path.is_file():
            if file_path.suffix == '.png':
                content_type = 'image/png'
            elif file_path.suffix in ['.jpg', '.jpeg']:
                content_type = 'image/jpeg'
            elif file_path.suffix == '.log':
                content_type = 'text/plain; charset=utf-8'
            else:
                content_type = 'application/octet-stream'
            
            self.send_response(200)
            self.send_header('Content-Type', content_type)
            self.end_headers()
            with open(file_path, 'rb') as f:
                self.wfile.write(f.read())
        else:
            self.send_error(404, f"File not found: {filename}")
    
    def log_message(self, format, *args):
        if '/api/results' not in args[0]:  # Don't log API calls
            super().log_message(format, *args)


def get_local_ip():
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("8.8.8.8", 80))
        ip = s.getsockname()[0]
        s.close()
        return ip
    except:
        return "localhost"


def main():
    config = load_config()
    
    # Get settings from environment or config
    port = int(os.environ.get('TEST_WEB_PORT', 
                              config.get('web_server_port', 8080)))
    
    pitrac_root = os.environ.get('PITRAC_ROOT', os.path.expanduser('~/PiTrac'))
    test_base_dir = config.get('test_base_dir', 'TestImages')
    results_dir = Path(pitrac_root) / test_base_dir / "results"
    
    # Ensure results directory exists
    results_dir.mkdir(parents=True, exist_ok=True)
    
    # Create handler with results directory
    handler = lambda *args, **kwargs: TestResultsHandler(
        *args, results_dir=results_dir, **kwargs
    )
    
    # Start server
    local_ip = get_local_ip()
    httpd = HTTPServer(('', port), handler)
    
    print(f"\nPiTrac Test Results Server")
    print(f"{'=' * 50}")
    print(f"Results directory: {results_dir}")
    print(f"Local access: http://localhost:{port}")
    print(f"Network access: http://{local_ip}:{port}")
    
    if 'SSH_CONNECTION' in os.environ:
        print(f"\nSSH tunnel: ssh -L {port}:localhost:{port} <user>@<pi-ip>")
    
    print(f"{'=' * 50}")
    print(f"Press Ctrl+C to stop the server\n")
    
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("\nServer stopped")
        sys.exit(0)


if __name__ == '__main__':
    main()