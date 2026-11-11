# NetGuard

**A C++17 command-line monitor for TCP service availability, connection latency, and basic incident tracking.**

NetGuard checks configured host-and-port endpoints concurrently, records every result, opens an incident after repeated failures, and records recovery when a later check succeeds. It is a small portfolio project focused on technology operations engineering concepts: service availability, diagnostics, incident lifecycle, and operational reporting.

> NetGuard is a TCP reachability monitor, not a production monitoring platform. A successful TCP connection confirms that a port accepted a connection; it does not verify application behavior.

## Contents

- [Capabilities](#capabilities)
- [How a check works](#how-a-check-works)
- [Requirements](#requirements)
- [Build](#build)
- [Run](#run)
- [Configure targets](#configure-targets)
- [Incident lifecycle](#incident-lifecycle)
- [Reports](#reports)
- [Project structure](#project-structure)
- [Tests](#tests)
- [Scope and limitations](#scope-and-limitations)
- [Engineering notes](#engineering-notes)

## Capabilities

- Run TCP connectivity checks for multiple configured endpoints concurrently.
- Resolve hostnames through the operating system resolver and report DNS errors.
- Measure elapsed time for DNS resolution and TCP connection attempts.
- Use nonblocking POSIX sockets and `poll()` for bounded connection waits.
- Open one P1/P2/P3 incident per endpoint after **two consecutive failures**.
- Record a resolution event after that endpoint recovers.
- Append monitoring results and incident events to CSV files.
- Run a fixed number of cycles or keep monitoring until interrupted.

## How a check works

1. NetGuard reads and validates every target from the CSV configuration.
2. For each monitoring cycle, it starts one asynchronous check per target using `std::async`.
3. Each check resolves the host, tries its resolved addresses, and waits for a nonblocking TCP connection using `poll()`.
4. NetGuard prints the results, updates each endpoint's in-memory failure/incident state, and appends CSV rows.
5. After printing a cycle summary, it waits for the configured interval before starting again.

The TCP timeout applies to connection attempts after DNS resolution. The operating system's `getaddrinfo()` call is synchronous and can take longer than the configured timeout. The reported latency includes DNS time.

## Requirements

- CMake 3.16 or newer
- A C++17-capable compiler
- macOS or Linux (the implementation uses POSIX sockets)

The project has no third-party C++ dependencies. Windows requires a POSIX socket compatibility layer or a platform-specific networking implementation.

## Build

From the repository root:

```bash
cmake -S . -B build
cmake --build build
```

## Run

Run three checks with two seconds between cycles:

```bash
./build/netguard --cycles 3 --interval 2
```

Monitor continuously, checking every ten seconds:

```bash
./build/netguard --cycles 0 --interval 10
```

Specify the target file, connection timeout, and report directory:

```bash
./build/netguard \
  --config config/targets.csv \
  --timeout 1500 \
  --reports reports
```

Print command-line help:

```bash
./build/netguard --help
```

| Option | Default | Description |
| --- | --- | --- |
| `--config PATH` | `config/targets.csv` | CSV file containing endpoints to monitor. |
| `--reports DIR` | `reports` | Directory for CSV output. Created if needed. |
| `--interval SECONDS` | `10` | Delay between monitoring cycles; minimum is 1 second. |
| `--cycles N` | `1` | Number of cycles. Use `0` to run continuously. |
| `--timeout MS` | `2000` | TCP connection wait limit per check; valid range is 1–60000 ms. |

Connectivity failures are recorded as monitoring results; they do not by themselves make a completed run return an error status. Invalid options, configuration errors, or report write failures return a nonzero status.

## Configure targets

Edit `config/targets.csv`. Each non-comment row has three comma-separated fields:

```csv
# host,port,criticality
example.com,443,P2
1.1.1.1,443,P2
localhost,9999,P3
```

- `host` can be a DNS name or an IP address.
- `port` must be an integer from 1 through 65535.
- `criticality` must be `P1`, `P2`, or `P3` and is copied to incident records; NetGuard does not infer severity.

The sample `localhost:9999` target will fail unless a service is listening on that port. Two consecutive failed cycles are needed to open an incident. Only monitor hosts and services you own or have permission to test.

## Incident lifecycle

Failure counts and open incidents are tracked separately for each `host:port` endpoint:

```text
Healthy ── first failed check ──> Failure pending
Failure pending ── second consecutive failure ──> Incident open
Incident open ── further failures ──> Incident remains open
Incident open ── successful check ──> Incident resolved
Failure pending ── successful check ──> Healthy
```

An endpoint's first failure does not create an incident. A success before the second failure resets its consecutive-failure count. Once an incident is open, repeated failures do not create duplicate `OPEN` events; a successful check writes a `RESOLVED` event.

## Reports

NetGuard creates the report directory if it does not exist. It appends to these files on each run:

### `reports/checks.csv`

One row for each endpoint check:

```text
timestamp,host,port,healthy,latency_ms,detail
```

`healthy` is `1` for a successful TCP connection and `0` for a failed check. `detail` contains `Connected` on success or a DNS, timeout, or socket diagnostic on failure. Text fields are CSV-escaped.

### `reports/incidents.csv`

One row for each incident open or resolution event:

```text
timestamp,id,host,port,severity,event,detail
```

`event` is `OPEN` or `RESOLVED`. IDs are sequential within a program run and restart from 1 after each launch. The active incident state is in memory only; reports persist, but open incidents are not restored after a restart. Existing CSV files are appended to and are not rotated automatically.

Timestamps use the machine's local time and do not include a timezone offset.

## Project structure

```text
NetGuard/
├── CMakeLists.txt       # C++17 target, compiler warnings, thread dependency, smoke test
├── config/
│   └── targets.csv      # Example host, port, and criticality entries
├── src/
│   └── main.cpp         # CLI parsing, DNS/TCP checks, incident state, and CSV output
├── tests/               # Reserved for additional tests
├── include/             # Reserved for future headers as the project is split into modules
└── README.md
```

The current implementation keeps the application in one source file to make the end-to-end flow easy to follow. It uses the C++ standard library, POSIX networking APIs, and the operating system resolver.

## Tests

The CMake configuration registers a smoke test for the `--help` command:

```bash
ctest --test-dir build --output-on-failure
```

This smoke test does not exercise external network connectivity. Reachability results depend on the machine's DNS, network, and firewall configuration.

## Scope and limitations

NetGuard checks whether a TCP connection can be established. It does not perform ICMP ping, SNMP polling, protocol-level health checks, topology discovery, or root-cause analysis. Severity is configured by the operator, rather than determined from business impact. DNS time is not bounded by the TCP timeout. Incident state is not durable across restarts, and the project does not include alert delivery, authentication, report rotation, or production scheduler/timeout management.

## Engineering notes

The implementation demonstrates:

- Concurrency with `std::async` and `std::future`.
- Nonblocking TCP connection attempts with `fcntl()`, `connect()`, `poll()`, and `getsockopt()`.
- DNS lookup with `getaddrinfo()` and endpoint iteration across returned addresses.
- Incident deduplication using consecutive-failure counters and active-incident state.
- Append-only CSV reporting with basic escaping for quoted fields.

Possible next steps include protocol-aware probes, persistent incident state, alerting/escalation, report retention, and more isolated unit/integration tests.
