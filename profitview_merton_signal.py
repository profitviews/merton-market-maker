"""
ProfitView strategy: Merton theoretical price vs market price for BitMEX XBTUSDT.

1. Paste this into ProfitView's Trading Bots code editor.
2. Subscribe to XBTUSDT.
3. Create a Market Making Bot with the following settings:
   - Exchange: BitMEX
   - Symbol: XBTUSDT
   - Type: Market Maker
4. Run the strategy.
5. Run a XBTUSDT Merton Market Making bot in BitMEX.

This version requires the C++ module `merton_online_calibrator.so`.
Parameters are updated online from tick flow in the C++ calibrator.
"""
from dotenv import load_dotenv

load_dotenv()

from profitview import Link, logger, cron
import math
import os
import threading
import requests

import merton_online_calibrator as moc

# -----------------------------------------------------------------------------
# Initial parameters for C++ calibrator seed (from .env, fallback to defaults).
# For better cold-start behaviour, run offline MLE calibration in merton.ipynb
# and set these in .env from the calibration output.
# -----------------------------------------------------------------------------
SIGMA = float(os.getenv("MERTON_SIGMA", 0.44))
LAMBDA = float(os.getenv("MERTON_LAMBDA", 20.0))
MU_J = float(os.getenv("MERTON_MU_J", 0.003))
DELTA_J = float(os.getenv("MERTON_DELTA_J", 0.01))

# Horizon for theoretical price (8h = next funding window)
T_HOURS = 8
T_YEARS = T_HOURS / (365.25 * 24)

# Refresh funding/mark from BitMEX API every 60 seconds
FUNDING_REFRESH_SEC = 60
# Log QuantLib helper divergence every N quote updates (0 disables)
QL_MONITOR_EVERY_N_QUOTES = 120
# Minimum half-spread around theoretical fair value (in bps).
MIN_HALF_SPREAD_BPS = 2.0

SYM = "XBTUSDT"
BITMEX_API_URL = "https://www.bitmex.com/api/v1"

CPP_WINDOW_SIZE = 4096
CPP_MIN_POINTS_FOR_UPDATE = 512
CPP_UPDATE_EVERY_N_RETURNS = 128
CPP_N_MAX = 15
CPP_COORDINATE_STEPS = 3


def merton_theoretical(S0: float, sigma: float, lam: float, mu_j: float, delta_j: float,
                      q_annual: float, T_years: float, r: float = 0.0) -> float:
    """E[S_T] = S_0 * exp((r - q - λk) * T), k = exp(μ_J + δ_J²/2) - 1"""
    k = math.exp(mu_j + 0.5 * delta_j**2) - 1
    drift = r - q_annual - lam * k
    return S0 * math.exp(drift * T_years)


def funding_annual(rate_per_8h: float) -> float:
    """Convert BitMEX funding rate (per 8h) to annualized."""
    return rate_per_8h * (365.25 * 24 / 8)


class Signals(Link):
    def __init__(self, *args, **kwargs):
        # Initialize state before Link.__init__ wires callbacks.
        self._lock = threading.Lock()
        self._funding_rate = 0.0
        self._mark_price = None
        self._sigma, self._lam, self._mu_j, self._delta_j = SIGMA, LAMBDA, MU_J, DELTA_J
        self._cpp_calibrator = self._init_cpp_calibrator()
        self._quote_count = 0
        super().__init__(*args, **kwargs)

    def on_start(self):
        self.refresh_funding()

    def _init_cpp_calibrator(self):
        """Initialize required C++ online calibrator."""
        p = moc.MertonParams()
        p.sigma = self._sigma
        setattr(p, "lambda", self._lam)  # lambda is a Python keyword
        p.mu_j = self._mu_j
        p.delta_j = self._delta_j

        cfg = moc.CalibratorConfig()
        cfg.window_size = CPP_WINDOW_SIZE
        cfg.min_points_for_update = CPP_MIN_POINTS_FOR_UPDATE
        cfg.update_every_n_returns = CPP_UPDATE_EVERY_N_RETURNS
        cfg.n_max = CPP_N_MAX
        cfg.coordinate_steps = CPP_COORDINATE_STEPS

        logger.info("Using required C++ online Merton calibrator")
        return moc.OnlineMertonCalibrator(p, cfg)

    def _tick_cpp_calibrator(self, price: float, epoch_ms: int):
        """Push tick to C++ calibrator and pull updated params when available."""
        if not price:
            return
        try:
            epoch_us = int(epoch_ms) * 1000
            accepted = self._cpp_calibrator.update_tick(float(price), epoch_us)
            if accepted and self._cpp_calibrator.maybe_update_params():
                p = self._cpp_calibrator.params()
                with self._lock:
                    self._sigma = float(p.sigma)
                    self._lam = float(getattr(p, "lambda"))
                    self._mu_j = float(p.mu_j)
                    self._delta_j = float(p.delta_j)
        except Exception as e:
            logger.error(f"C++ calibrator tick failed: {e}")

    @cron.run(every=FUNDING_REFRESH_SEC)
    def refresh_funding(self):
        """Fetch mark price and funding rate from BitMEX instrument (runs every 60s)."""
        try:
            resp = requests.get(f"{BITMEX_API_URL}/instrument", params={"symbol": SYM}, timeout=5)
            resp.raise_for_status()
            data = resp.json()
            if not data:
                return
            inst = data[0]
            with self._lock:
                self._funding_rate = float(inst.get("fundingRate", 0))
                self._mark_price = float(inst.get("markPrice", 0)) if inst.get("markPrice") else None
            logger.info(f"Funding refreshed: rate={self._funding_rate:.6f}, mark={self._mark_price}")
        except Exception as e:
            logger.error(f"Funding refresh failed: {e}")

    def quote_update(self, src: str, sym: str, data: dict):
        """Real-time bid/ask updates: compute theoretical vs market mid."""
        if sym != SYM:
            return
        mkt_bid, mkt_ask = data.get("bid", [0, 0])[0], data.get("ask", [0, 0])[0]
        if not mkt_bid or not mkt_ask:
            return
        mid = (mkt_bid + mkt_ask) / 2
        self._tick_cpp_calibrator(mid, int(data.get("time", self.epoch_now)))
        with self._lock:
            q_annual = funding_annual(self._funding_rate)
        theo = self._cpp_calibrator.fair_value(mid, q_annual, T_YEARS, 0.0)
        diff = theo - mid
        diff_bps = (diff / mid) * 10000 if mid else 0
        min_half = theo * (MIN_HALF_SPREAD_BPS / 10000.0)
        mkt_half = max((mkt_ask - mkt_bid) / 2.0, 0.0)
        half_spread = max(min_half, mkt_half)
        quote_bid = theo - half_spread
        quote_ask = theo + half_spread
        logger.info(
            f"{sym} mid={mid:.2f} theo={theo:.2f} diff={diff:.2f} ({diff_bps:.1f} bps) "
            f"quote=[{quote_bid:.2f}, {quote_ask:.2f}]"
        )

        # Monitoring: compare fast fair_value() with QuantLib helper periodically.
        self._quote_count += 1
        if QL_MONITOR_EVERY_N_QUOTES > 0 and (self._quote_count % QL_MONITOR_EVERY_N_QUOTES == 0):
            try:
                theo_ql = self._cpp_calibrator.fair_value_quantlib(mid, q_annual, T_YEARS, 0.0)
                gap_bps = ((theo_ql - theo) / mid) * 10000 if mid else 0.0
                logger.info(
                    f"{sym} ql_monitor fast={theo:.2f} ql={theo_ql:.2f} gap={theo_ql-theo:.2f} ({gap_bps:.2f} bps)"
                )
            except Exception as e:
                logger.warning(f"QuantLib monitor failed: {e}")

        # Publish two-sided quote around fair value for neutral/market-making behavior.
        self.signal("bitmex", SYM, quote=[quote_bid, quote_ask])
        # Stream to websocket for dashboards
        self.publish(
            "merton_theo",
            {
                "sym": sym,
                "market": mid,
                "theo": theo,
                "diff_bps": diff_bps,
                "quote_bid": quote_bid,
                "quote_ask": quote_ask,
            },
        )

