# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is a C++ server application called EjiaOptionServer that handles financial options market data. It's a TCP server that receives real-time market data from upstream sources, processes it, and forwards it to client connections. The server also stores and compresses market data, generates K-line data, and uploads data to Alibaba Cloud OSS.

## Architecture

1. **Main Server Component**: Based on HPSocket TCP server framework
2. **Data Processing**: Real-time market data processing with multiple worker threads
3. **Data Storage**: Local file storage with GZIP compression
4. **Data Upload**: Integration with Alibaba Cloud OSS for data backup
5. **K-line Generation**: Generates 1-minute and 5-minute K-line data from tick data
6. **Monitoring**: Shell script for process monitoring and automatic restart

## Key Components

- `main.cpp`: Entry point and server initialization
- `WorkerProcessor.cpp/.h`: Core data processing logic
- `Util.cpp/.h`: Utility functions for data handling, compression, and storage
- `DataStruct.h`: Data structures for market data
- `TcpPullClientImpl.cpp/.h`: Client connections to upstream data sources
- `TcpCacheClient.cpp/.h`: Cache client for additional data sources
- `CQuoteWriter.cpp/.h`: Quote data writing functionality
- `timer/`: Timer-based scheduled tasks
- `protocol/`: Protocol buffers for data serialization

## Build System

Uses CMake with the following key characteristics:
- C++14 standard
- Multiple external libraries (HPSocket, libcurl, protobuf, jemalloc, zlib)
- Dependencies linked statically where possible
- Cross-platform compatible but primarily Linux-targeted

## Common Development Tasks

### Building the Project
```bash
mkdir build && cd build
cmake ..
make
```

### Running the Server
```bash
# From the project root directory
./bin/Debug/EjiaOptionServer.out
# or
./bin/Release/EjiaOptionServer.out
```

### Process Monitoring
The server is monitored by `restart_monitor.sh` which automatically restarts the process at 20:35 on weekdays.

## Code Patterns and Conventions

1. **Memory Management**: Uses a mix of manual memory management and smart pointers
2. **Threading**: Heavy use of multithreading with thread pools for data processing
3. **Data Structures**: Custom structures for market data with specific field layouts
4. **Error Handling**: Logging-based error handling with spdlog
5. **Network Protocol**: Custom binary protocol with packet headers
6. **Data Compression**: GZIP compression for stored market data
7. **File Storage**: Organized directory structure for different data types

## Important External Dependencies

- HPSocket: High-performance TCP/UDP communication framework
- Alibaba Cloud OSS SDK: For data upload and storage
- Protocol Buffers: Data serialization
- spdlog: Logging framework
- jemalloc: Memory allocation optimization
- libcurl: HTTP client functionality
- zlib: Data compression

## Key Data Flows

1. **Incoming Data**: TCP clients → Data parsing → Processing queue
2. **Processing**: Worker threads process market data → Generate derived data (K-lines)
3. **Storage**: Compress and save tick data to local files every 5 minutes
4. **Upload**: Upload compressed data to Alibaba Cloud OSS periodically
5. **Outgoing Data**: Processed data sent to frontend client connections

## Monitoring and Maintenance

- Automatic restart at 20:35 on weekdays via `restart_monitor.sh`
- Memory monitoring with periodic logging
- Data cleanup for files older than 3 days