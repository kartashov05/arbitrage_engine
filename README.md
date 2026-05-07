---

# Cross-Exchange Spot Order Book Arbitrage Scanner

A C++20 cross-exchange market data and arbitrage scanner for Binance and MEXC spot markets.

The project builds local L2 order books from live WebSocket depth streams, synchronizes them with REST snapshots, and scans both top-of-book and depth-aware cross-exchange arbitrage opportunities after taker fees.

This is a research and engineering project focused on market data infrastructure, exchange connectivity, local order book correctness, and low-latency C++ design.

It does not place real orders.

---

## Overview

This project implements a live spot market data pipeline for detecting fee-adjusted cross-exchange arbitrage signals between Binance and MEXC.

The scanner connects to exchange WebSocket feeds, receives live depth updates, reconstructs local order books, validates update sequences, loads REST snapshots, and evaluates whether the available ask depth on one exchange can be bought and sold into bid depth on another exchange with positive net spread after taker fees.

The goal of the project is not to execute trades automatically.

Instead, it is a technical case study in exchange integration, local order book reconstruction, market data quality control, and depth-aware arbitrage calculation.

---

## What This Project Demonstrates

This project demonstrates practical work with:

* C++20 market data infrastructure
* Live WebSocket exchange integration
* Binance Spot depth stream parsing
* MEXC Spot protobuf depth stream parsing
* REST snapshot synchronization
* Local L2 order book reconstruction
* Sequence validation and gap detection
* Stale update filtering
* Multi-symbol subscription planning
* Threaded live data ingestion
* Cross-exchange market data storage
* Fee-adjusted arbitrage calculations
* Top-of-book spread monitoring
* Depth-aware VWAP-based opportunity scanning
* YAML-based runtime configuration
* Structured logging
* GoogleTest-based test coverage

---

## Important Scope Clarification

This is a scanner and analytics tool only.

It does not:

* Place live orders
* Manage exchange balances
* Submit market or limit orders
* Perform withdrawals or transfers
* Manage inventory across exchanges
* Execute real arbitrage trades
* Handle production risk checks
* Guarantee executable opportunities
* Provide financial advice

The project detects potential cross-exchange price differences from live order book data. Any real trading system would require additional execution logic, risk management, capital allocation, latency monitoring, exchange account handling, rate-limit control, and production observability.

---

## Features

* Live Binance Spot WebSocket depth stream support
* Live MEXC Spot WebSocket depth stream support
* MEXC protobuf depth message decoding
* Binance REST snapshot loading
* MEXC REST snapshot loading
* Local order book reconstruction for both exchanges
* WebSocket update buffering during snapshot synchronization
* Sequence bridging between REST snapshot and buffered updates
* Gap detection and resynchronization marking
* Stale update filtering
* Multi-symbol subscription planning
* Threaded live market data ingestion
* Shared cross-exchange `MarketDataStore`
* Top-of-book spread scanner
* Depth-aware arbitrage scanner
* VWAP-based executable notional calculation
* Taker-fee-adjusted net spread calculation
* Minimum and maximum notional filters
* YAML configuration
* Structured logging with `spdlog`
* Unit tests with GoogleTest

---

## Supported Exchanges

| Exchange | REST Snapshots | WebSocket Depth | Message Format | Local Order Book |
| --- | ---: | ---: | --- | ---: |
| Binance Spot | Yes | Yes | JSON | Yes |
| MEXC Spot | Yes | Yes | Protobuf | Yes |

---

## Architecture

```text
Configuration
      ↓
Subscription planner
      ↓
Binance WebSocket client       MEXC WebSocket client
      ↓                        ↓
Binance depth parser           MEXC protobuf depth parser
      ↓                        ↓
Binance local book manager     MEXC local book manager
      ↓                        ↓
REST snapshot synchronization  REST snapshot synchronization
      ↓                        ↓
Sequence validation            Sequence validation
      ↓                        ↓
Cross-exchange MarketDataStore
      ↓
Top-of-book spread scanner
      ↓
Depth-aware arbitrage engine
      ↓
Structured logs and scan results
```

---

## Order Book Synchronization

The engine follows the standard local order book reconstruction model:

1. Subscribe to the exchange WebSocket depth stream.
2. Buffer incoming depth updates.
3. Fetch a REST order book snapshot.
4. Apply the snapshot to the local book.
5. Drop stale buffered updates.
6. Apply only updates that correctly bridge the snapshot sequence.
7. Continue applying live updates in order.
8. Detect sequence gaps.
9. Mark the book for resynchronization when needed.

This logic is implemented separately for Binance and MEXC because the two exchanges use different message formats and sequence fields.

---

## Arbitrage Logic

For each configured symbol, the scanner evaluates both cross-exchange directions:

```text
Buy on Binance -> sell on MEXC
Buy on MEXC    -> sell on Binance
```

For each direction, the engine walks the available ask and bid depth and calculates:

* executable quantity
* executable notional
* average buy price
* average sell price
* gross spread
* taker fees
* net spread in basis points

A route is considered an opportunity only when it passes the configured filters:

```text
net_spread_bps >= min_net_spread_bps
notional >= min_notional_usdt
notional <= max_notional_usdt
enough order book depth exists
```

The scanner supports both simple top-of-book spread inspection and deeper VWAP-based calculations across multiple order book levels.

---

## Top-of-Book Spread Monitoring

The live scanner also reports the best cross-exchange top-of-book spreads.

Example log format:

```text
[top-spread] symbol=ETHUSDT buy=mexc sell=binance gross_bps=3.215392728067806 fee_bps=20 net_bps=-16.784607271932195
binance_bid=29.0749@2333.28
binance_ask=51.5586@2333.29
mexc_bid=9.90318@2332.52
mexc_ask=1.27551@2332.53
```

In this example, the best theoretical direction is:

```text
Buy ETH on MEXC
Sell ETH on Binance
```

The raw price difference is positive, but the net spread is negative after taker fees:

```text
gross_bps = 3.215
fee_bps   = 20
net_bps   = -16.785
```

So the scanner correctly rejects the trade as non-executable after fees.

---

## Live Scan Summary

Example live scan output:

```text
Live scan #130: ready_pairs=10 top_spreads=10 opportunities=0
```

Meaning:

| Field | Meaning |
| --- | --- |
| `ready_pairs=10` | 10 symbols have ready local books on both exchanges |
| `top_spreads=10` | top-of-book spreads were calculated for 10 symbols |
| `opportunities=0` | no depth-aware arbitrage opportunities passed the configured filters |

A final scanner status may look like this:

```text
Live scanner stopped.
binance_raw=10000
binance_parsed=10000
binance_ignored=0
mexc_raw=10000
mexc_parsed=9999
mexc_ignored=1
binance_ready=10
mexc_ready=10
binance_need_resync=0
mexc_need_resync=0
binance_gaps=0
mexc_gaps=0
```

This indicates that both exchange feeds were processed successfully, all configured books became ready, and no sequence gaps were detected during the run.

---

## Tech Stack

| Area | Technology |
| --- | --- |
| Language | C++20 |
| Build system | CMake + Ninja |
| macOS compiler | Apple Clang |
| Linux compiler | GCC 13+ or Clang 17+ |
| Networking | Boost.Asio |
| WebSocket / HTTP | Boost.Beast |
| TLS | OpenSSL |
| Binance parsing | nlohmann/json |
| MEXC parsing | Protocol Buffers |
| Configuration | yaml-cpp |
| Logging | spdlog + fmt |
| Testing | GoogleTest |

---

## Requirements

The project is designed for macOS and Linux.

Required tooling:

```text
C++20 compiler
CMake
Ninja
OpenSSL
Boost
Protocol Buffers
yaml-cpp
spdlog
fmt
GoogleTest
```

Recommended compilers:

```text
macOS: Apple Clang
Linux: GCC 13+ or Clang 17+
```

---

## Build

Build:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Run the binary:

```bash
./build/arbitrage_engine
```

---

## Configuration

The scanner uses a YAML configuration file.

Example path:

```text
config/config.yaml
```

The configuration is expected to define the symbols, exchange settings, taker fees, and arbitrage filters used by the scanner.

Typical configurable values include:

```yaml
symbols:
  - BTCUSDT
  - ETHUSDT
  - SOLUSDT
  - XRPUSDT

fees:
  binance:
    taker_bps: 10
  mexc:
    taker_bps: 10

arbitrage:
  min_net_spread_bps: 10
  max_book_age_ms: 500
  min_notional_usdt: 20
  max_notional_usdt: 1000
```

Adjust the values to match the actual exchange fee tier, symbols, and scanning limits you want to test.

---

## Example Modes

Print planned subscriptions:

```bash
./build/arbitrage_engine config/config.yaml
```

Fetch a Binance REST snapshot:

```bash
./build/arbitrage_engine config/config.yaml --binance-snapshot=BTCUSDT
```

Fetch a MEXC REST snapshot:

```bash
./build/arbitrage_engine config/config.yaml --mexc-snapshot=BTCUSDT
```

Run a Binance local live order book:

```bash
./build/arbitrage_engine config/config.yaml --binance-local-live=BTCUSDT --max-messages=1000
```

Run a MEXC local live order book:

```bash
./build/arbitrage_engine config/config.yaml --mexc-local-live=BTCUSDT --max-messages=1000
```

Run a REST snapshot arbitrage scan:

```bash
./build/arbitrage_engine config/config.yaml --scan-snapshots
```

Run a REST snapshot top-of-book spread scan:

```bash
./build/arbitrage_engine config/config.yaml --scan-snapshots-top
```

Run the live cross-exchange scanner:

```bash
./build/arbitrage_engine config/config.yaml --scan-live --max-messages=10000
```

---

## Testing

Run the test suite with:

```bash
ctest --test-dir build --output-on-failure
```

Current test status:

```text
46/46 tests passing
```

Implemented and tested components include:

* Binance REST snapshot parser
* Binance WebSocket depth parser
* Binance local order book manager
* MEXC REST snapshot parser
* MEXC protobuf WebSocket depth parser
* MEXC local order book manager
* Market data store
* Subscription planner
* Fee-adjusted arbitrage engine
* Snapshot arbitrage scan
* Live cross-exchange scan

---

## Current Status

Implemented:

* Binance Spot REST snapshot support
* Binance Spot WebSocket depth support
* MEXC Spot REST snapshot support
* MEXC Spot WebSocket depth support
* MEXC protobuf market data parser
* Local L2 order book reconstruction
* Snapshot synchronization
* Gap detection
* Resynchronization state tracking
* Multi-symbol live scanning
* Cross-exchange market data storage
* Top-of-book spread calculation
* Depth-aware arbitrage calculation
* Fee-adjusted opportunity filtering
* GoogleTest test suite

Observed live scan status:

```text
binance_ready=10
mexc_ready=10
binance_gaps=0
mexc_gaps=0
opportunities=0
```

This means the scanner was able to build stable local books on both exchanges and evaluate spreads, but no executable opportunities were found after taker fees during the sampled run.

---

## Design Decisions

### Local books instead of REST-only polling

The scanner uses WebSocket streams and local order book reconstruction because arbitrage analysis depends on fresh market data and available depth.

REST snapshots are used for synchronization, while WebSocket updates keep the books current.

### Depth-aware calculation instead of top-of-book only

Top-of-book spreads can be misleading when available size is too small.

The arbitrage engine therefore walks book depth and calculates VWAP prices for the executable notional range.

### Fee-adjusted opportunity filtering

A positive raw spread is not enough.

The scanner subtracts configured taker fees and only reports opportunities that pass net spread and notional filters.

### Separate exchange-specific book managers

Binance and MEXC use different formats and sequencing rules.

Keeping exchange-specific synchronization logic separate makes the system easier to validate and extend.

### No execution layer

The project intentionally stops at market data and opportunity detection.

This keeps the scope focused on infrastructure correctness rather than account operations, order routing, and production trading risk.

---

## Use Cases

* Market data infrastructure research
* Cross-exchange order book comparison
* Spot market microstructure analysis
* Arbitrage signal prototyping
* Exchange WebSocket integration practice
* Local order book reconstruction testing
* Low-latency market data pipeline case study

---

## Limitations

This project does not account for:

* Real order placement latency
* Exchange account balances
* Deposit and withdrawal delays
* Transfer fees
* Exchange-specific trading restrictions
* Dynamic fee tiers
* Rate limits under production load
* Partial fills
* Slippage after signal detection
* Queue position
* Order cancellation logic
* Production monitoring and alerting
* Full risk management
* Capital allocation across exchanges

Because of these limitations, every detected spread should be interpreted as a market data signal, not as a guaranteed executable trade.

---

## Disclaimer

This project is for educational and research purposes only.

It does not execute trades and should not be used as-is for live trading.

Real-world arbitrage requires latency control, account balances, exchange-specific order handling, transfer logic, risk checks, rate-limit management, monitoring, and robust production infrastructure.

---