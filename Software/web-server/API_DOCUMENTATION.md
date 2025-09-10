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
    "activemq_connected": true,       // ActiveMQ broker connection
    "activemq_running": true,         // ActiveMQ service status
    "pitrac_running": true,           // Main PiTrac service status
    "websocket_clients": 3,           // Active WebSocket connections
    "listener_stats": {               // ActiveMQ listener metrics
      "connected": true,
      "messages_processed": 42,
      "errors": 0
    }
  }
  ```

#### System Statistics
- **GET** `/api/stats`
- **Description**: Provides detailed system statistics
- **Response**:
  ```json
  {
    "websocket_connections": 3,
    "listener": {
      "connected": true,
      "messages_processed": 42,
      "errors": 0
    },
    "shot_history_count": 25
  }
  ```

## ActiveMQ Message Format

### Message Topics
- Primary Topic: `/topic/Golf.Sim`
- Message Encoding: MsgPack
- Supported Formats:
  1. Array Format: `[speed, launch_angle, side_angle, ...]`
  2. Dictionary Format: Partial or complete shot data update

### Message Processing
- Base64 encoding supported
- Validation performed on each incoming message
- Errors logged but do not interrupt message processing

## Error Handling
- Most errors are logged internally
- API endpoints return appropriate HTTP status codes
- WebSocket connections automatically handle disconnects

## Configuration
Configuration is loaded from `pitrac.yaml` with the following key network settings:
```yaml
network:
  broker_address: tcp://localhost:61616  # ActiveMQ broker
  username: pitrac_user                  # Optional credentials
  password: secure_password              # Optional credentials
```

## Recommended Clients
- WebSocket support required
- MsgPack decoding library recommended
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