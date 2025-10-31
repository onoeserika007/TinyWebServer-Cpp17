# TinyWebServer-Cpp11

A lightweight, high-performance web server implemented in modern C++20. Features include event-driven I/O, thread pool, MySQL connection pool, lock-free async logging, and zero-copy file transfer.

### Features
* Event-driven I/O: Uses epoll in edge-triggered mode for non-blocking I/O
* Thread Pool: Separates I/O and computation tasks for optimal resource utilization
* MySQL Connection Pool: Thread-safe connection management with auto-reconnection
* Lock-free Async Logger: High-performance logging with async queue and file rotation
* Static File Server: Zero-copy transfer using mmap/sendfile with HTTP Range support
* HTTP Framework: Modular design with parser, router, controller and middleware support
* Timer Wheel: O(1) complexity for connection timeout management

### Build Requirements
* Linux
* GCC 13+ (for C++20 support)
* CMake 3.16+
* MySQL development libraries
* make/ninja
### Quick Start
1. Install dependencies (Ubuntu/Debian):
```bash
sudo apt install g++-13 libmysqlclient-dev cmake make
```
2. Build:
```bash
mkdir -p build
CC=/usr/bin/gcc-13 CXX=/usr/bin/g++-13 cmake -S . -B build
cmake --build build -j$(nproc)
```
3. Setup MySQL:
```sql
CREATE DATABASE webserver_db;
USE webserver;
CREATE TABLE user (
    username CHAR(50) NULL,
    passwd CHAR(50) NULL
) ENGINE=InnoDB;
```
4. Configure server:
``` json
{
    "server": {
        "port": 8080,
        "thread_count": 32
    },
    "mysql": {
        "host": "localhost",
        "user": "root",
        "password": "your_password",
        "database": "webserver_db",
        "pool_size": 8
    },
    "log": {
        "file": "server.log",
        "level": "INFO",
        "async": true
    }
}
```
5. Run:
```
{project_dir}/bin/epoll_server
```

### Project Structure

```
.
├── src/
│   ├── base/          # Core components (logging, config, timer)
│   ├── util/          # Utilities (thread pool, connection pool)
│   └── webserver/     # HTTP server implementation
├── thirdparty/        # Third-party dependencies
├── test/              # Test cases
├── root/             # Static resources
└── CMakeLists.txt    # Main build configuration
```

### API Examples
Register a new user:
```bash
curl -X POST http://localhost:8080/register -d "user=test&passwd=123"
```

Login:
```bash
curl -X POST http://localhost:8080/login -d "user=test&passwd=123"
```

Stream video with range support:
```bash
curl -H "Range: bytes=1000-2000" http://localhost:8080/video.mp4
```

### Configuration
See config.json for all available options. Key configurations:
* Server port and thread count
* MySQL connection settings
* Logging configuration (async/sync, log level, rotation)