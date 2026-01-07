# OpsPulse

**Real-Time Incident & Ops Command Server**

A multithreaded client-server system for real-time incident management and operational event broadcasting. Built with modern C++20, this project demonstrates production-grade systems engineering patterns commonly used in trading and financial infrastructure environments.

```
   ____            ____        __          
  / __ \____  ____/ __ \__  __/ /___ ___  
 / / / / __ \/ __/ /_/ / / / / / __ `__ \ 
/ /_/ / /_/ (__  ) ____/ /_/ / / / / / / / 
\____/ .___/____/_/    \__,_/_/_/ /_/ /_/  
    /_/           
```

## Features

### Server
- **TCP Socket Server** with concurrent connection handling
- **Thread Pool** for CPU-bound request processing
- **Length-Prefixed Message Framing** (4-byte big-endian) for reliable message parsing
- **Central Request Queue** decoupling I/O from processing
- **In-Memory State Store** with reader-writer locks for concurrent access
- **Fan-Out Broadcaster** with per-client send queues (prevents slow clients from blocking)
- **Append-Only Event Log** for persistence and crash recovery
- **Role-Based Authentication** (admin, operator, viewer)

### Client
- Interactive CLI with colored output
- Real-time push notifications for events and incidents
- Subscription-based channel filtering

### Data Model
- **Events**: Timestamped log entries with channels, severity levels, and tags
- **Incidents**: Tracked issues with SEV levels (1-5), status workflow, and owner assignment
- **Channels**: Logical groupings (e.g., `trading`, `infra`, `risk`, `market-data`)

## Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                         OpsPulse Server                          │
├─────────────────────────────────────────────────────────────────┤
│                                                                  │
│  ┌──────────────┐    ┌──────────────┐    ┌──────────────┐      │
│  │ Accept Thread │───▶│ I/O Threads  │───▶│ Request Queue│      │
│  └──────────────┘    └──────────────┘    └──────┬───────┘      │
│                                                   │              │
│                                                   ▼              │
│  ┌──────────────┐    ┌──────────────┐    ┌──────────────┐      │
│  │  Broadcaster │◀───│ Worker Pool  │───▶│  State Store │      │
│  └──────┬───────┘    └──────────────┘    └──────────────┘      │
│         │                    │                                   │
│         │                    ▼                                   │
│         │            ┌──────────────┐                           │
│         │            │  Event Log   │                           │
│         │            └──────────────┘                           │
│         ▼                                                        │
│  ┌──────────────────────────────────────┐                       │
│  │     Per-Client Send Queues           │                       │
│  └──────────────────────────────────────┘                       │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
                               │
                               ▼
                    ┌─────────────────────┐
                    │   TCP Connections   │
                    └─────────────────────┘
                               │
              ┌────────────────┼────────────────┐
              ▼                ▼                ▼
        ┌──────────┐    ┌──────────┐    ┌──────────┐
        │ Client 1 │    │ Client 2 │    │ Client N │
        └──────────┘    └──────────┘    └──────────┘
```

### Threading Model

| Thread | Responsibility |
|--------|---------------|
| Accept Thread | Accepts new TCP connections, creates client sessions |
| I/O Threads | Reads from client sockets, frames messages, pushes to request queue |
| Worker Pool | Processes requests, updates state, emits events |
| Broadcaster Thread | Flushes per-client send queues to sockets |

## Building

### Prerequisites
- CMake 3.16+
- C++20 compiler (GCC 10+, Clang 12+, MSVC 2019+)

### Build Steps

```bash
# Clone and enter directory
cd OpsPulse

# Create build directory
mkdir build && cd build

# Configure
cmake ..

# Build
cmake --build . --parallel

# Executables will be in build/
```

### Build Options

```bash
# Enable sanitizers for debugging
cmake -DENABLE_SANITIZERS=ON ..

# Build with release optimizations
cmake -DCMAKE_BUILD_TYPE=Release ..
```

## Usage

### Starting the Server

```bash
./opspulse_server [options]

Options:
  -p, --port PORT        Server port (default: 9090)
  -w, --workers N        Worker thread count (default: 4)
  -l, --log PATH         Event log file path (default: data/events.log)
  -u, --users PATH       Users config file (default: config/users.json)
  --no-auth              Disable authentication
  -h, --help             Show help
```

### Using the CLI Client

```bash
./opspulse_client
```

**Commands:**

```
Connection:
  connect <host> <port>    Connect to server
  auth <user> <token>      Authenticate
  disconnect               Disconnect from server

Subscriptions:
  sub <channel>            Subscribe to channel
  unsub <channel>          Unsubscribe from channel

Events:
  event <channel> <level> <msg>  Publish event
  events [channel] [limit]       List recent events

Incidents:
  inc create <sev> <channel> <title>  Create incident (sev 1-5)
  inc list [channel] [status]         List incidents
  inc ack <id>                        Acknowledge incident
  inc assign <id> <owner>             Assign owner
  inc resolve <id>                    Resolve incident
  inc comment <id> <text>             Add comment
```

### Example Session

```bash
# Terminal 1: Start server
./opspulse_server -p 9090

# Terminal 2: Operator client
./opspulse_client
> connect localhost 9090
> auth operator operator-secret
> sub trading
> sub infra
> event trading warn "Market data feed A showing 500ms latency"
> inc create 2 trading "Market Data Feed Degradation"
> inc list
> inc ack INC-1001
> inc assign INC-1001 operator

# Terminal 3: Another client sees updates in real-time
./opspulse_client
> connect localhost 9090
> auth viewer viewer-secret
> sub *
# Will see all events and incident updates pushed automatically
```

## Protocol

### Message Format

Messages use a length-prefixed framing protocol:

```
+----------------+------------------+
| Length (4B BE) | JSON Payload     |
+----------------+------------------+
```

### Message Types

**Client → Server:**
```json
// Authentication
{"type": "auth", "payload": {"user": "operator", "token": "secret"}}

// Subscribe to channels
{"type": "subscribe", "payload": {"channels": ["trading", "infra"]}}

// Publish event
{"type": "event", "payload": {"channel": "trading", "level": "warn", "msg": "High latency", "tags": ["latency"]}}

// Create incident
{"type": "incident_create", "payload": {"sev": 2, "title": "API Degradation", "channel": "infra"}}

// Update incident
{"type": "incident_update", "payload": {"id": "INC-1001", "status": "ACKED", "owner": "dylan"}}
```

**Server → Client:**
```json
// Auth response
{"type": "auth_response", "payload": {"success": true, "session_id": "abc123"}}

// Acknowledgment
{"type": "ack", "payload": {"resource_id": "INC-1001", "message": "OK"}}

// Push event (to subscribers)
{"type": "push_event", "payload": {"id": "EVT-123", "channel": "trading", ...}}

// Push incident update
{"type": "push_incident_update", "payload": {"id": "INC-1001", "field": "status", "new_value": "ACKED"}}
```

## Configuration

### Users File (`config/users.json`)

```json
{
    "users": [
        {"username": "admin", "token": "admin-secret", "role": "admin"},
        {"username": "operator", "token": "operator-secret", "role": "operator"},
        {"username": "viewer", "token": "viewer-secret", "role": "viewer"}
    ]
}
```

**Roles:**
- `admin`: Full access
- `operator`: Can create/update incidents and publish events
- `viewer`: Read-only access

## Persistence

The server uses an append-only log (`data/events.log`) for persistence:

```json
{"type":"event","ts":1234567890,"data":{"id":"EVT-1","channel":"trading",...}}
{"type":"incident","ts":1234567891,"data":{"id":"INC-1001","title":"...",...}}
{"type":"incident_update","ts":1234567892,"id":"INC-1001","field":"status","old":"OPEN","new":"ACKED","by":"operator"}
```

On startup, the server replays this log to rebuild state. This provides:
- Crash recovery
- Audit trail
- Simple debugging

## Design Decisions

### Why Length-Prefixed Framing?
- Avoids partial read issues common with newline-delimited protocols
- Clear message boundaries
- Efficient parsing (no scanning for delimiters)

### Why Per-Client Send Queues?
- Slow clients don't block the server
- Backpressure handling (queue size limits)
- Worker threads never block on I/O

### Why Reader-Writer Locks?
- Most operations are reads (listing events/incidents)
- Writes are less frequent
- Better concurrency than exclusive mutex

### Why Append-Only Log?
- Simple and reliable persistence
- Natural event sourcing pattern
- Easy to backup and replicate

## Interview Talking Points

1. **"How do you handle slow clients?"**
   > I use per-client send queues. Worker threads never block on socket writes. A dedicated broadcaster thread flushes queues, and if a client's queue fills up, we apply backpressure by dropping messages rather than blocking the system.

2. **"How do you prevent partial reads?"**
   > I implemented length-prefixed framing with a state machine parser. Each message has a 4-byte big-endian length header. The parser accumulates bytes until a complete frame is received.

3. **"How do you handle concurrent access to state?"**
   > The state store uses `std::shared_mutex` for reader-writer locking. Most operations are reads which can happen concurrently. Writes are serialized but don't block readers unnecessarily.

4. **"How does the server recover from crashes?"**
   > I implemented event sourcing-lite with an append-only log. Every state change is logged before acknowledgment. On startup, we replay the log to rebuild the in-memory state.

5. **"How is work distributed across threads?"**
   > Accept thread handles new connections. I/O threads read from sockets and parse frames. A thread pool processes requests and updates state. A broadcaster thread writes to clients. This separation prevents any single operation from blocking others.

## License

MIT License - Feel free to use this for learning and interviews.

