# Polymarket arb bot (C++)

Scans Polymarket binary markets for arbitrage using the Gamma + CLOB APIs. Runs in scan-only mode by default (no orders placed).

## What it detects

- **Buy-both arb**: `best_ask_yes + best_ask_no < 1` (buy both outcomes, redeem for $1)
- **Sell-both arb**: `best_bid_yes + best_bid_no > 1` (split $1, sell both outcomes)

Taker fees are estimated with `fee = shares * rate * p * (1 - p)`. `max_size` is capped by top-of-book depth on both sides and `PM_MAX_TRADE_USD`.

## Build

Requires CMake 3.20+, a C++20 compiler, libcurl, and OpenSSL.

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build
```

On Ubuntu/Debian:

```bash
sudo apt install cmake g++ libcurl4-openssl-dev libssl-dev zlib1g-dev git
```

On macOS:

```bash
brew install cmake openssl
```

## Run

Discover all active LoL matches (moneyline + game winners, silent until arb):

```bash
PM_DISABLE_WS=1 ./build/polymarket-arb --lol
```

Watch one match (moneyline + Game 1–5):

```bash
PM_DISABLE_WS=1 ./build/polymarket-arb --event lol-kt-dk-2026-09-04
```

Watch a single game market:

```bash
PM_DISABLE_WS=1 ./build/polymarket-arb lol-kt-dk-2026-09-04-game4
```

Dry-run trade latency benchmark (no orders execute):

```bash
PM_BENCHMARK_ITERATIONS=10 ./build/polymarket-arb --benchmark <market-slug>
```

Copy `.env.example` to `.env` and adjust settings as needed.

## Environment variables

| Variable | Default | Description |
|---|---|---|
| `PM_MIN_NET_EDGE` | `0.002` | Minimum net edge per share after fees |
| `PM_MAX_TRADE_USD` | `100` | Max notional per opportunity |
| `PM_POLL_INTERVAL_MS` | `2000` | REST polling interval |
| `PM_DISABLE_WS` | unset | Set to `1` to disable WebSocket mode |
| `PM_LOL_DISCOVER` | auto | Scan all active LoL events |
| `PM_SERIES_SLUG` | `league-of-legends` | Gamma series to discover |
| `PM_EVENT_SLUG` | unset | Watch one match (moneyline + games) |
| `PM_EVENT_LIMIT` | `30` | Max events in discover mode |
| `PM_LOL_LIVE_ONLY` | unset | Only scan live events |
| `PM_SKIP_MONEYLINE` | unset | Skip match winner markets |
| `PM_SKIP_GAME_WINNERS` | unset | Skip game winner markets |
| `PM_WATCH_STATUS` | unset | Set to `1` to print periodic price updates |
| `PM_VERBOSE` | unset | Set to `1` to print startup banner |
| `PM_BENCHMARK` | unset | Set to `1` or pass `--benchmark` for latency test |
| `PM_BENCHMARK_ITERATIONS` | `10` | Benchmark run count |

## Deploy on a Linux machine

```bash
git clone https://github.com/ilyalerma/polymarket-arb.git
cd polymarket-arb
cp .env.example .env
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build

# run under systemd (example)
sudo tee /etc/systemd/system/polymarket-arb.service <<'EOF'
[Unit]
Description=Polymarket arb watcher
After=network-online.target

[Service]
Type=simple
User=ubuntu
WorkingDirectory=/home/ubuntu/polymarket-arb
EnvironmentFile=/home/ubuntu/polymarket-arb/.env
ExecStart=/home/ubuntu/polymarket-arb/build/polymarket-arb --lol
Restart=always
RestartSec=5

[Install]
WantedBy=multi-user.target
EOF

sudo systemctl daemon-reload
sudo systemctl enable --now polymarket-arb
```

## Live trading

Live order placement requires CLOB V2 EIP-712 signing and API credentials. The project currently ships with:

- REST book scanning
- WebSocket market feed
- L2 HMAC auth header builder
- Dry-run benchmark mode

Set `PM_LIVE_TRADING=1` only after wiring order signing for your wallet type (EOA or proxy).

## Risk notes

- Arb opportunities are often fleeting and may disappear before both legs fill.
- Taker fees can erase edge on both legs.
- This is not financial advice. Use at your own risk.
