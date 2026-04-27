# Crypto Trading System

A high-performance C++23 trading framework for cryptocurrency markets. Built with a modular architecture that separates quotation processing, strategy execution, and order management, enabling developers to build custom trading strategies through a clean abstraction layer.

## Core Features

- **Pluggable Strategies**: Custom strategies as runtime-loaded `.so` plugins via the `sdk` interface
- **Real-time Quotation Engine**: WebSocket-based market data ingestion with low-latency processing
- **Automated Trading Engine**: Order placement, position management, and fill reconciliation
- **Exchange Integration**: OKX exchange support with spot and swap trading
- **CPU-Pinned Threading**: Three dedicated threads for quotation, strategy, and trading
- **Graceful Shutdown**: Signal handling for clean termination and resource cleanup
- **Example Strategy**: Multi-mesh grid strategy included as reference implementation



## System Components

### Quotation Engine
- Persistent WebSocket connection to exchange
- Real-time market data streaming and caching
- Data validation and error recovery

### Strategy Engine
- Strategy lifecycle management and concurrent execution
- Position tracking with reserved balance accounting
- Runtime plugin loading via the `sdk` shared library

### Trading Engine
- Order lifecycle management (pending, filled, rejected)
- Position state synchronization with exchange
- Partial fill handling and position reconciliation

### Exchange Client
- API abstraction for OKX exchange
- Authentication and request signing
- REST and WebSocket support

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
sudo apt-get install -y cmake clang-18 libssl-dev
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

1. Copy the configuration template:
   ```bash
   cp config/config.json.template config/config.json
   ```

2. Edit `config/config.json` with your trading parameters:
   - Exchange API credentials (API key, secret, passphrase)
   - Instrument ID (e.g., `BTC-USDT`)
   - Strategy configuration (inline `params` or external `params_path`, optional `plugin_paths`)
   - Logging preferences

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

The system will gracefully stop, cancel pending orders, and close connections.

## Build & Test

### Build Release

```bash
make build
```

### Clean Build Artifacts

```bash
make clean
```

### Continuous Integration

The following CI checks run on all PRs and commits:

- **build**: Compilation with `-O3` optimization and C++23 standard
- **check-pr-title**: Validates commit message format

PR title format: `<Type>: <description>`

Valid types: `Feat`, `Fix`, `Chore`, `Refactor`, `Perf`, `Docs`, `Style`, `Test`, `Build`, `CI`

## Development

### Building Custom Strategies

Strategies are runtime-loaded `.so` plugins built against the `sdk` shared library.

1. Install the SDK: `cmake --install build --prefix ~/.local`
2. In your plugin source, inherit `Strategy` from `<strategy.hpp>`, register via `REGISTER_STRATEGY(MyStrategy)`, and add `DECLARE_PLUGIN_ABI()` once per plugin.
3. Build as `SHARED` linking `sdk::sdk`; reference the `.so` path from `config.json` `strategy_engine.plugin_paths`.

### Code Style

Conform to `.clang-format` configuration:

```bash
clang-format -i src/**/*.cpp
```

### Project Structure

```
.
├── sdk/                          # Shared library exposing the strategy ABI
├── src/
│   ├── main.cpp
│   ├── quotation_engine/         # Market data processing
│   ├── strategy_engine/          # Strategy execution + plugin loader
│   ├── trading_engine/           # Order and position management
│   └── clients/                  # Exchange API implementations
├── config/                       # Configuration templates
├── scripts/                      # Deployment and utility scripts
├── CMakeLists.txt                # Build configuration
└── .github/workflows/            # CI/CD definitions
```

## Example Strategies

The repository includes a multi-mesh grid strategy as a reference implementation. See `strategy_engine/multi_mesh/` for details.

## Known Limitations

- Currently supports OKX exchange only
- Requires stable network connection (reconnection logic included)
- No built-in backtesting framework

## Risk Disclaimer

This system performs automated cryptocurrency trading. Use with extreme caution:

- Test thoroughly on paper trading first
- Start with small position sizes
- Monitor positions regularly
- Set appropriate stop-loss levels
- Keep API keys secure

**The authors assume no liability for financial losses.**

## License

MIT License - see [LICENSE](LICENSE) file for details.

## References

- [OKX API Documentation](https://www.okx.com/docs-v5/en/)
- [C++23 Standard](https://en.cppreference.com/w/cpp/23)

## Authors

- Eurus-1011 (@Eurus-1011)

## Contact

For issues, questions, or contributions, please open an issue on GitHub.
