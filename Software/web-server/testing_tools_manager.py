"""Testing Tools Manager for PiTrac Web Server

Manages execution of various testing and diagnostic tools for PiTrac
"""

import asyncio
import json
import logging
import os
import subprocess
from pathlib import Path
from typing import Any, Dict, List, Optional
from datetime import datetime

logger = logging.getLogger(__name__)


class TestingToolsManager:
    """Manages PiTrac testing and diagnostic tools"""

    def __init__(self, config_manager):
        self.config_manager = config_manager
        self.pitrac_binary = "/usr/lib/pitrac/pitrac_lm"
        self.running_processes = {}
        self.completed_results = {}
        
        self.tools = {
            "pulse_test": {
                "name": "Strobe Pulse Test",
                "description": "Test IR strobe pulse functionality",
                "category": "hardware",
                "args": ["--pulse_test", "--system_mode", "camera1"],
                "requires_sudo": False,
                "timeout": 30
            },
            "camera1_still": {
                "name": "Camera 1 Still Image",
                "description": "Capture a still image from Camera 1",
                "category": "camera",
                "args": ["--system_mode", "camera1", "--cam_still_mode", 
                        "--output_filename=cam1_still_picture.png"],
                "requires_sudo": False,
                "timeout": 10
            },
            "camera2_still": {
                "name": "Camera 2 Still Image", 
                "description": "Capture a still image from Camera 2",
                "category": "camera",
                "args": ["--system_mode", "camera2", "--cam_still_mode",
                        "--output_filename=cam2_still_picture.png"],
                "requires_sudo": False,
                "timeout": 10
            },
            "camera1_ball_location": {
                "name": "Camera 1 Ball Location",
                "description": "Check ball location for Camera 1",
                "category": "calibration",
                "args": ["--system_mode", "camera1", "--check_ball_location"],
                "requires_sudo": False,
                "timeout": 10
            },
            "camera2_ball_location": {
                "name": "Camera 2 Ball Location",
                "description": "Check ball location for Camera 2",
                "category": "calibration", 
                "args": ["--system_mode", "camera2", "--check_ball_location"],
                "requires_sudo": False,
                "timeout": 10
            },
            "test_images": {
                "name": "Test with Sample Images",
                "description": "Run detection on test images",
                "category": "testing",
                "args": ["--system_mode", "test"],
                "requires_sudo": True,
                "timeout": 60
            },
            "automated_testing": {
                "name": "Automated Test Suite",
                "description": "Run full automated testing suite",
                "category": "testing",
                "args": ["--system_mode", "automated_testing"],
                "requires_sudo": False,
                "timeout": 120
            },
            "test_gspro_server": {
                "name": "Test GSPro Server",
                "description": "Test GSPro server connectivity",
                "category": "connectivity",
                "args": ["--system_mode", "test_gspro_server"],
                "requires_sudo": False,
                "timeout": 30
            },
            "test_e6_connect": {
                "name": "Test E6 Connect",
                "description": "Test E6 Connect server connectivity",
                "category": "connectivity",
                "args": ["--system_mode", "test_e6_connect"],
                "requires_sudo": False,
                "timeout": 30
            }
        }

    def get_available_tools(self) -> Dict[str, Any]:
        """Get list of available testing tools organized by category"""
        categories = {}
        for tool_id, tool_info in self.tools.items():
            category = tool_info["category"]
            if category not in categories:
                categories[category] = []
            categories[category].append({
                "id": tool_id,
                "name": tool_info["name"],
                "description": tool_info["description"],
                "requires_sudo": tool_info["requires_sudo"]
            })
        return categories

    async def run_tool(self, tool_id: str) -> Dict[str, Any]:
        """Run a specific testing tool
        
        Args:
            tool_id: ID of the tool to run
            
        Returns:
            Dict with status, output, and any error messages
        """
        if tool_id not in self.tools:
            return {
                "status": "error",
                "message": f"Unknown tool: {tool_id}"
            }
        
        if tool_id in self.running_processes:
            return {
                "status": "error", 
                "message": f"Tool {tool_id} is already running"
            }
        
        tool_info = self.tools[tool_id]
        
        try:
            config_path = self.config_manager.generate_golf_sim_config()
            
            cmd = [self.pitrac_binary]
            cmd.extend(tool_info["args"])
            cmd.append(f"--config_file={config_path}")
            
            config = self.config_manager.get_config()
            
            web_share_dir = config.get("gs_config", {}).get("ipc_interface", {}).get(
                "kWebServerShareDirectory", "~/LM_Shares/Images/"
            )
            expanded_dir = web_share_dir.replace("~", str(Path.home()))
            cmd.append(f"--web_server_share_dir={expanded_dir}")
            
            cmd.append("--logging_level=trace")
            
            env = os.environ.copy()
            env["LD_LIBRARY_PATH"] = "/usr/lib/pitrac"
            env["PITRAC_ROOT"] = "/usr/lib/pitrac"
            env["PITRAC_MSG_BROKER_FULL_ADDRESS"] = "tcp://localhost:61616"
            env["PITRAC_BASE_IMAGE_LOGGING_DIR"] = str(Path.home() / "LM_Shares/Images")
            env["PITRAC_WEBSERVER_SHARE_DIR"] = str(Path.home() / "LM_Shares/WebShare")
            env["DISPLAY"] = ":0.0"
            
            if tool_info["requires_sudo"]:
                cmd = ["sudo", "-E"] + cmd
            
            logger.info(f"Running tool {tool_id}: {' '.join(cmd)}")
            
            process = await asyncio.create_subprocess_exec(
                *cmd,
                stdout=asyncio.subprocess.PIPE,
                stderr=asyncio.subprocess.PIPE,
                env=env
            )
            
            self.running_processes[tool_id] = process
            
            try:
                stdout, stderr = await asyncio.wait_for(
                    process.communicate(),
                    timeout=tool_info["timeout"]
                )
                
                output = stdout.decode() if stdout else ""
                error = stderr.decode() if stderr else ""
                
                result = {
                    "status": "success" if process.returncode == 0 else "failed",
                    "output": output,
                    "error": error,
                    "return_code": process.returncode,
                    "timestamp": datetime.now().isoformat()
                }
                
                if "still" in tool_id:
                    if "cam1" in tool_id:
                        image_path = Path.home() / "LM_Shares/Images/cam1_still_picture.png"
                    else:
                        image_path = Path.home() / "LM_Shares/Images/cam2_still_picture.png"
                    
                    if image_path.exists():
                        result["image_path"] = str(image_path)
                        result["image_url"] = f"/api/images/{image_path.name}"
                
                return result
                
            except asyncio.TimeoutError:
                process.kill()
                await process.wait()
                return {
                    "status": "timeout",
                    "message": f"Tool {tool_id} timed out after {tool_info['timeout']} seconds"
                }
            finally:
                if tool_id in self.running_processes:
                    del self.running_processes[tool_id]
                    
        except Exception as e:
            logger.error(f"Error running tool {tool_id}: {e}")
            return {
                "status": "error",
                "message": str(e)
            }

    async def stop_tool(self, tool_id: str) -> Dict[str, Any]:
        """Stop a running tool
        
        Args:
            tool_id: ID of the tool to stop
            
        Returns:
            Dict with status
        """
        if tool_id not in self.running_processes:
            return {
                "status": "error",
                "message": f"Tool {tool_id} is not running"
            }
        
        try:
            process = self.running_processes[tool_id]
            process.terminate()
            
            try:
                await asyncio.wait_for(process.wait(), timeout=5.0)
            except asyncio.TimeoutError:
                process.kill()
                await process.wait()
            
            del self.running_processes[tool_id]
            
            return {
                "status": "success",
                "message": f"Tool {tool_id} stopped"
            }
            
        except Exception as e:
            logger.error(f"Error stopping tool {tool_id}: {e}")
            return {
                "status": "error",
                "message": str(e)
            }

    def get_running_tools(self) -> List[str]:
        """Get list of currently running tools"""
        return list(self.running_processes.keys())