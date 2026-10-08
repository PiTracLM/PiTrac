# PiTrac Web Server API Documentation

## Overview
The PiTrac Web Server provides a comprehensive API for interacting with golf shot tracking data. This API enables real-time shot monitoring, retrieval of shot history, and system diagnostics.

## Base URL
`http://<pitrac-server-ip>:<port>`

## Authentication
Currently, the API does not implement authentication. Future versions may add security measures.

## Endpoints

### 1. Dashboard
- **GET** `/`
- **Description**: Renders the main dashboard HTML page
- **Response**: HTML dashboard with current shot data

### 2. WebSocket Connection
- **Endpoint**: `/ws`
- **Protocol**: WebSocket
- **Description**: Real-time shot data updates
- **Connection Behaviors**:
  - On connect: Sends current shot data
  - Continuous: Broadcasts shot updates to all connected clients

### 3. Shot Data Endpoints

#### Get Current Shot
- **GET** `/api/shot`
- **Description**: Retrieves the most recent shot data
- **Response**:
  ```json
  {
    "speed": 98.5,         // Ball speed in mph
    "launch_angle": 12.3,  // Launch angle in degrees
    "side_angle": -2.1,    // Side angle in degrees
    "backspin": 3200,      // Backspin RPM
    "sidespin": 500,       // Sidespin RPM
    "timestamp": "2025-09-03T14:30:45.123Z"
  }
  ```

#### Get Shot History
- **GET** `/api/history`
- **Query Parameters**:
  - `limit` (optional): Number of historical shots to return (default: 10, max: 100)
- **Response**: Array of shot data objects

#### Reset Shot Data
- **POST** `/api/reset`
- **Description**: Resets the current shot data
- **Response**:
  ```json
  {
    "status": "reset",
    "timestamp": "2025-09-03T14:30:45.123Z"
  }
  ```

### 4. Image Retrieval
- **GET** `/api/images/{filename}`
- **Description**: Retrieves shot images by filename
- **Responses**:
  - `200`: Image file
  - `{"error": "Image not found"}` if image doesn't exist

### 5. System Diagnostics

#### Health Check
- **GET** `/health`
- **Description**: Provides system health and connectivity status
- **Response**:
  ```json
  {
    "status": "healthy",              // Overall system status
    "pitrac_running": true,           // Main PiTrac service status
    "websocket_clients": 3            // Active WebSocket connections
  }
  ```

#### System Statistics
- **GET** `/api/stats`
- **Description**: Provides detailed system statistics
- **Response**:
  ```json
  {
    "websocket_connections": 3,
    "transport": "http",
    "shot_history_count": 25
  }
  ```

## Shot Result Ingest

### Internal Endpoint
- **POST** `/api/internal/shot-result`
- **Description**: `pitrac_lm` posts each result here as JSON; the server stores it and broadcasts it to WebSocket clients
- **Fields**: `result_type` (int), `speed_mps`, `carry`, `launch_angle`, `side_angle`, `back_spin`, `side_spin`, `message`, `shot_id`, `images`
- Status messages keep the previous shot's numbers and only update `result_type` and `message`
- Hits are saved to shot history

## Error Handling
- Most errors are logged internally
- API endpoints return appropriate HTTP status codes
- WebSocket connections automatically handle disconnects

## Recommended Clients
- WebSocket support required
- Supports both real-time and polling access patterns

## Limitations
- No authentication currently implemented
- Maximum of 100 historical shots retrievable
- Image retrieval limited to stored shot images

## Future Roadmap
- Add authentication
- Implement more granular filtering for shot history
- Expand image metadata retrieval
- Add configuration management via API

## Performance Notes
- WebSocket recommended for real-time updates
- REST endpoints provide fallback data retrieval
- Sub-100ms typical response times for most endpoints