# NetGuard

NetGuard is a C++17 command-line network operations monitor. It checks whether configured TCP services can be reached, records every check, and tracks incidents after repeated failures. The project is designed as a small, readable demonstration of networking and operations engineering concepts.

## What it does

- **Monitors several endpoints concurrently.** Each configured host and port is checked in its own asynchronous task during a monitoring cycle.
- **Resolves hostnames with the operating system resolver.** DNS errors are reported as failed checks with a diagnostic. DNS lookup time is part of the measured duration and is not bounded by NetGuard's TCP timeout.
- **Measures TCP connection time.** NetGuard creates nonblocking TCP sockets and uses `poll()` to wait up to the configured timeout for a connection attempt. The measured time includes DNS lookup and connection setup. A successful TCP handshake confirms that a port accepted a connection; it does not prove that the application behind it is healthy.
- **Opens incidents after two consecutive failures.** The configured P1, P2, or P3 severity is attached to the incident. Repeated failures while an incident is open do not create duplicates.
- **Resolves an incident after recovery.** A successful check resets the failure count and records a `RESOLVED` event for any active incident for that endpoint.
- **Writes CSV reports.** Every check is appended to `checks.csv`; incident open and resolve events are appended to `incidents.csv`.
- **Runs for a fixed number of cycles or continuously.** Set `--cycles 0` for continuous monitoring.

## Project layout

```text
NetGuard/
├── CMakeLists.txt       # C++17 build configuration and smoke test
├── config/
│   └── targets.csv      # Example host, port, and severity entries
├── src/
│   └── main.cpp         # CLI, configuration, TCP checks, incidents, and CSV output
└── README.md            # Build, run, behavior, and limitations
```

## Requirements

- CMake 3.16 or newer
- A C++17 compiler
- macOS or Linux (uses POSIX sockets)

No third-party C++ libraries are required.

## Build

From the project directory:

```bash
cmake -S . -B build
cmake --build build
```

## Run

Run three monitoring cycles, two seconds apart:

```bash
./build/netguard --cycles 3 --interval 2
```

Run continuously, checking every ten seconds:

```bash
./build/netguard --cycles 0 --interval 10
```

Choose a configuration file, TCP timeout in milliseconds, and report directory:

```bash
./build/netguard --config config/targets.csv --timeout 1500 --reports reports
```

See all supported options and defaults:

```bash
./build/netguard --help
```

Options are `--config PATH`, `--reports DIR`, `--interval SECONDS`, `--cycles N`, and `--timeout MS`. The defaults are `config/targets.csv`, `reports`, 10 seconds, one cycle, and 2000 milliseconds. The interval must be at least one second; cycles may be zero or greater; timeout must be from 1 to 60000 milliseconds.

## Configure targets

Edit `config/targets.csv`. Each non-comment line has this format:

```csv
host,port,criticality
example.com,443,P2
1.1.1.1,443,P2
localhost,9999,P3
```

The criticality field must be `P1`, `P2`, or `P3`. The included `localhost:9999` example is expected to fail unless a service is listening on that port. To open an incident for a failing endpoint, run at least two cycles. Only check systems you own or are authorized to test.

## Reports

NetGuard creates the report directory if needed and appends rows to:

- `reports/checks.csv`: timestamp, host, port, healthy flag (`1` or `0`), latency in milliseconds, and a result or diagnostic.
- `reports/incidents.csv`: timestamp, incident ID, host, port, configured severity, event (`OPEN` or `RESOLVED`), and detail.

Incident IDs start again at 1 on each program launch. CSV history remains on disk; existing reports are appended to, not rotated.

## How a monitoring cycle works

1. NetGuard loads and validates all configured targets.
2. It launches one asynchronous TCP check per target.
3. Each result is printed and recorded in `checks.csv`.
4. A failed check increments that endpoint's consecutive failure count. The second consecutive failure opens one incident.
5. A successful check clears the failure count and resolves an active incident.
6. NetGuard prints a cycle summary and waits for the configured interval before the next cycle.

## Tests

The CMake project registers a smoke test that checks the command-line help path:

```bash
ctest --test-dir build --output-on-failure
```

External connectivity depends on the local network and firewall. A restricted build environment may compile the program successfully while blocking outbound checks.

## Scope and limitations

NetGuard checks TCP reachability. It is not an ICMP ping tool, SNMP collector, service-level protocol checker, network topology mapper, or root-cause analysis system. DNS uses the operating system resolver and may take longer than the TCP timeout. Severity is supplied in the configuration rather than calculated from business impact. Incident state is held in memory, so an open incident is not restored after a restart. There is no alert delivery, authentication, or production-grade scheduling and timeout management.

## Engineering discussion

This project can be used to discuss bounded nonblocking connection attempts with `poll()`, concurrent checks with `std::async`, incident deduplication through consecutive-failure state, and the difference between port reachability and application health. Possible extensions include durable incident state, protocol-specific health checks, alerting and escalation, SLA metrics, and cloud deployment.
