# Crypto Trading System

A high-performance C++23 cryptocurrency trading system with real-time quotation processing, multi-strategy support, and automated execution.

## Features

- **Real-time Quotation Engine**: WebSocket-based market data ingestion with low-latency processing
- **Multi-Strategy Framework**: Support for multiple concurrent trading strategies with the grid trading pattern
- **Automated Trading Engine**: Order placement, position management, and fill reconciliation
- **Exchange Integration**: Full support for OKX exchange with spot and swap trading
- **CPU-Pinned Threading**: Three dedicated CPU-affined threads for quotation, strategy, and trading
- **Mesh Strategy**: Lazy mesh activation with grid-based buy/sell operations
- **Graceful Shutdown**: Signal handling for clean termination and resource cleanup

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│           Crypto Trading System                              │
├─────────────────────────────────────────────────────────────┤
│                                                               │
│  ┌──────────────────┐  ┌──────────────────┐                 │
│  │ Quotation Engine │  │ Strategy Engine  │                 │
│  │   (WebSocket)    │  │  (Grid/Mesh)     │                 │
│  └────────┬─────────┘  └────────┬─────────┘                 │
│           │                     │                            │
│           └──────────┬──────────┘                            │
│                      │                                       │
│           ┌──────────▼─────────┐                            │
│           │  Trading Engine    │                            │
│           │ (Position Manager) │                            │
│           └──────────┬─────────┘                            │
│                      │                                       │
│           ┌──────────▼─────────┐                            │
│           │ Exchange Client    │                            │
│           │      (OKX API)     │                            │
│           └────────────────────┘                            │
│                                                               │
└─────────────────────────────────────────────────────────────┘
```

## System Modules

### Quotation Engine
- Establishes persistent WebSocket connection to exchange
- Real-time market data streaming and caching
- Data validation and error recovery

### Strategy Engine
- Multi-mesh strategy configuration and state management
- Position tracking with reserved balance accounting
- Lazy mesh activation based on market conditions
- Round-trip trade execution and rebalancing

### Trading Engine
- Order lifecycle management (pending, filled, rejected)
- Position state synchronization with exchange
- Partial fill handling and position reconciliation
- Cancel pending logic for market oscillations

### Exchange Client
- Standardized API abstraction for OKX exchange
- Authentication and request signing
- REST and WebSocket support
- Spot and swap trading interfaces

## Requirements

### System
- **OS**: Linux (Ubuntu 22.04 LTS or later)
- **CPU**: x86-64 with multi-core support
- **Memory**: Minimum 2GB RAM
- **Disk**: 500MB free space for logs and data

### Build Dependencies
- **Compiler**: Clang 18+ (with C++23 support)
- **Build System**: CMake 3.16+
- **Libraries**:
  - jsoncpp (JSON processing)
  - OpenSSL (cryptography)
  - pthread (thread support)

### Runtime Dependencies
- OKX API credentials (API key, secret, passphrase)
- Network connectivity to OKX exchange endpoints
- System clock synchronization (NTP)

## Installation

### 1. Install System Dependencies

```bash
sudo apt-get update
sudo apt-get install -y cmake clang-18 libjsoncpp-dev libssl-dev
```

### 2. Clone Repository

```bash
git clone https://github.com/Eurus-1011/crypto-trading-system.git
cd crypto-trading-system
```

### 3. Build

```bash
make build
```

The compiled binary will be at: `build/crypto-trading-system`

## Configuration

### Config File Format

Create `config/config.json` with your trading parameters:

```json
{
  "exchange_api": {
    "api_key": "your_api_key",
    "secret_key": "your_secret_key",
    "passphrase": "your_passphrase"
  },
  "trading": {
    "instrument_id": "BTC-USDT",
    "trading_mode": "cash",
    "strategies": [
      {
        "name": "grid_strategy_1",
        "type": "mesh",
        "grid_size": 100,
        "min_price": 40000,
        "max_price": 50000,
        "order_amount": 0.01
      }
    ]
  },
  "logging": {
    "level": "INFO",
    "path": "logs/trading.log"
  }
}
```

### Key Parameters

| Parameter | Description | Default |
|-----------|-------------|---------|
| `grid_size` | Number of grid levels | 100 |
| `min_price` | Minimum price level | - |
| `max_price` | Maximum price level | - |
| `order_amount` | Base order size per grid | - |
| `trading_mode` | `cash` or `margin` | `cash` |

## Usage

### Start Trading System

```bash
./build/crypto-trading-system config/config.json
```

### Monitor Output

```bash
tail -f logs/trading.log
```

### Graceful Shutdown

```bash
kill -SIGTERM <pid>
```

The system will:
1. Stop accepting new orders
2. Cancel pending orders
3. Close active connections
4. Flush logs and exit cleanly

## Build & Test

### Build Release

```bash
make build
```

### Clean Build Artifacts

```bash
make clean
```

### Status Checks

The following CI checks run on all PRs and commits:

- **build**: Compilation with `-O3` optimization
- **check-pr-title**: Validates commit message format

PR title format: `<Type>: <description>`

Valid types: `Feat`, `Fix`, `Chore`, `Refactor`, `Perf`, `Docs`, `Style`, `Test`, `Build`, `CI`

## Development

### Code Style

Conform to `.clang-format` configuration:

```bash
clang-format -i src/**/*.cpp
```

### Project Structure

```
.
├── src/
│   ├── main.cpp
│   ├── quotation_engine/          # Market data processing
│   ├── strategy_engine/           # Strategy logic and mesh
│   ├── trading_engine/            # Order and position management
│   └── clients/                   # Exchange API implementations
├── config/                        # Configuration templates
├── scripts/                       # Deployment and utility scripts
├── CMakeLists.txt                 # Build configuration
└── .github/workflows/             # CI/CD definitions
```

### Adding New Strategies

1. Extend `Strategy` base class in `strategy_engine/`
2. Implement required virtual methods
3. Register in `StrategyRegistry`
4. Configure in `config.json`

## Known Limitations

- Currently supports OKX exchange only
- Grid strategies designed for high-frequency, low-volatility trading
- Requires stable network connection (reconnection logic included)
- No built-in backtesting framework

## Contributing

1. Create feature branch: `git checkout -b feat/your-feature`
2. Make changes and commit: `git commit -m "Feat: description"`
3. Push and create pull request
4. Ensure all CI checks pass
5. Await review and merge

## Risk Disclaimer

This system performs automated cryptocurrency trading. Use with extreme caution:

- Test thoroughly on paper trading first
- Start with small position sizes
- Monitor positions regularly
- Set appropriate stop-loss levels
- Keep API keys secure

**The authors assume no liability for financial losses.**

## License

[Add your license here]

## References

- [OKX API Documentation](https://www.okx.com/docs-v5/en/)
- [C++23 Standard](https://en.cppreference.com/w/cpp/23)
- [Grid Trading Strategy Research](https://en.wikipedia.org/wiki/Grid_trading)

## Authors

- Eurus-1011 (@Eurus-1011)

## Contact

For issues, questions, or contributions, please open an issue on GitHub.
