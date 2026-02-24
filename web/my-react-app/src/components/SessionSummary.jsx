/**
 * Session Summary Component
 * 
 * Displays cumulative session statistics in a table format:
 * - Volume metrics (total, buy, sell, delta)
 * - Price statistics (high, low, close, change %)
 * - Time-based aggregation
 * - Exportable data
 */

import { useState, useMemo, useCallback } from 'react';
import { alertManager } from '../utils/alerts';

// Format volume with K/M suffixes
const formatVolume = (volume) => {
  if (volume >= 1000000) {
    return (volume / 1000000).toFixed(2) + 'M';
  } else if (volume >= 1000) {
    return (volume / 1000).toFixed(2) + 'K';
  }
  return volume.toFixed(0);
};

// Format price with specified decimals
const formatPrice = (price, decimals = 2) => {
  if (price === null || price === undefined || price === -Infinity) return '-';
  return price.toFixed(decimals);
};

// Format percentage
const formatPercent = (value) => {
  const num = parseFloat(value);
  if (isNaN(num)) return '-';
  const sign = num > 0 ? '+' : '';
  return sign + num.toFixed(1) + '%';
};

// Format duration
const formatDuration = (ms) => {
  const seconds = Math.floor(ms / 1000);
  const minutes = Math.floor(seconds / 60);
  const hours = Math.floor(minutes / 60);
  
  if (hours > 0) {
    return `${hours}h ${minutes % 60}m`;
  } else if (minutes > 0) {
    return `${minutes}m ${seconds % 60}s`;
  }
  return `${seconds}s`;
};

const SessionSummary = ({ 
  sessionStats = null, 
  onResetSession,
  showExport = true,
  compact = false 
}) => {
  const [isExpanded, setIsExpanded] = useState(!compact);
  const [exportFormat, setExportFormat] = useState('csv');
  
  // Get stats from props or use default
  const stats = useMemo(() => {
    if (sessionStats) {
      return sessionStats;
    }
    return alertManager.getSessionStats();
  }, [sessionStats]);

  // Export data handler
  const handleExport = useCallback(() => {
    const data = {
      timestamp: new Date().toISOString(),
      sessionDuration: stats.sessionDuration,
      volume: {
        total: stats.totalVolume,
        buy: stats.buyVolume,
        sell: stats.sellVolume,
        delta: stats.cumulativeDelta,
        buyPercent: stats.buyPercent,
        sellPercent: stats.sellPercent,
        volumeRatio: stats.volumeRatio
      },
      price: {
        high: stats.highPrice,
        low: stats.lowPrice,
        close: stats.closePrice,
        open: stats.openPrice,
        change: stats.closePrice - stats.openPrice,
        changePercent: stats.openPrice > 0 
          ? ((stats.closePrice - stats.openPrice) / stats.openPrice * 100) 
          : 0
      },
      candles: stats.candleCount
    };

    let content;
    let filename;
    let mimeType;

    if (exportFormat === 'csv') {
      // CSV format
      const rows = [
        ['Metric', 'Value'],
        ['Session Duration', formatDuration(stats.sessionDuration)],
        ['Total Volume', formatVolume(stats.totalVolume)],
        ['Buy Volume', formatVolume(stats.buyVolume)],
        ['Sell Volume', formatVolume(stats.sellVolume)],
        ['Cumulative Delta', formatVolume(stats.cumulativeDelta)],
        ['Buy %', `${stats.buyPercent}%`],
        ['Sell %', `${stats.sellPercent}%`],
        ['Volume Ratio', stats.volumeRatio],
        ['High Price', formatPrice(stats.highPrice)],
        ['Low Price', formatPrice(stats.lowPrice)],
        ['Close Price', formatPrice(stats.closePrice)],
        ['Open Price', formatPrice(stats.openPrice)],
        ['Price Change', formatPrice(stats.closePrice - stats.openPrice)],
        ['Price Change %', formatPercent((stats.closePrice - stats.openPrice) / (stats.openPrice || 1) * 100)],
        ['Candle Count', stats.candleCount]
      ];
      content = rows.map(row => row.join(',')).join('\n');
      filename = `session_summary_${Date.now()}.csv`;
      mimeType = 'text/csv';
    } else {
      // JSON format
      content = JSON.stringify(data, null, 2);
      filename = `session_summary_${Date.now()}.json`;
      mimeType = 'application/json';
    }

    // Create download link
    const blob = new Blob([content], { type: mimeType });
    const url = URL.createObjectURL(blob);
    const link = document.createElement('a');
    link.href = url;
    link.download = filename;
    document.body.appendChild(link);
    link.click();
    document.body.removeChild(link);
    URL.revokeObjectURL(url);
  }, [stats, exportFormat]);

  const handleReset = useCallback(() => {
    alertManager.resetSession();
    if (onResetSession) {
      onResetSession();
    }
  }, [onResetSession]);

  // Calculate derived values
  const priceChange = stats.closePrice - stats.openPrice;
  const priceChangePercent = stats.openPrice > 0 
    ? (priceChange / stats.openPrice * 100) 
    : 0;
  const isPositive = priceChange >= 0;

  if (compact) {
    return (
      <div className="session-summary-compact">
        <div className="flex items-center justify-between text-sm">
          <div className="flex gap-4">
            <span className="text-gray-400">Vol:</span>
            <span className="font-mono">{formatVolume(stats.totalVolume)}</span>
            <span className="text-gray-400">Δ:</span>
            <span className={`font-mono ${stats.cumulativeDelta >= 0 ? 'text-green-500' : 'text-red-500'}`}>
              {stats.cumulativeDelta >= 0 ? '+' : ''}{formatVolume(stats.cumulativeDelta)}
            </span>
            <span className="text-gray-400">H:</span>
            <span className="font-mono">{formatPrice(stats.highPrice)}</span>
            <span className="text-gray-400">L:</span>
            <span className="font-mono">{formatPrice(stats.lowPrice)}</span>
          </div>
          <button
            onClick={() => setIsExpanded(!isExpanded)}
            className="text-gray-400 hover:text-white text-xs"
          >
            {isExpanded ? '▼' : '▶'}
          </button>
        </div>
        {isExpanded && (
          <div className="mt-2">
            <SessionTable stats={stats} />
          </div>
        )}
      </div>
    );
  }

  return (
    <div className="session-summary bg-gray-900 rounded-lg p-4 text-white">
      <div className="flex items-center justify-between mb-4">
        <h3 className="text-lg font-semibold">Session Summary</h3>
        <div className="flex gap-2">
          <select
            value={exportFormat}
            onChange={(e) => setExportFormat(e.target.value)}
            className="bg-gray-800 text-white text-sm px-2 py-1 rounded border border-gray-700"
          >
            <option value="csv">CSV</option>
            <option value="json">JSON</option>
          </select>
          {showExport && (
            <button
              onClick={handleExport}
              className="bg-blue-600 hover:bg-blue-700 text-white text-sm px-3 py-1 rounded transition-colors"
            >
              Export
            </button>
          )}
          <button
            onClick={handleReset}
            className="bg-gray-700 hover:bg-gray-600 text-white text-sm px-3 py-1 rounded transition-colors"
          >
            Reset
          </button>
        </div>
      </div>

      <SessionTable stats={stats} priceChange={priceChange} priceChangePercent={priceChangePercent} isPositive={isPositive} />
    </div>
  );
};

// Session Table Component
const SessionTable = ({ stats, priceChange = 0, priceChangePercent = 0, isPositive = true }) => {
  return (
    <div className="grid grid-cols-2 gap-4">
      {/* Volume Section */}
      <div className="bg-gray-800 rounded p-3">
        <h4 className="text-sm font-medium text-gray-400 mb-2">Volume</h4>
        <div className="space-y-1 text-sm">
          <div className="flex justify-between">
            <span className="text-gray-400">Total</span>
            <span className="font-mono">{formatVolume(stats.totalVolume)}</span>
          </div>
          <div className="flex justify-between">
            <span className="text-gray-400">Buy</span>
            <span className="font-mono text-green-500">{formatVolume(stats.buyVolume)}</span>
          </div>
          <div className="flex justify-between">
            <span className="text-gray-400">Sell</span>
            <span className="font-mono text-red-500">{formatVolume(stats.sellVolume)}</span>
          </div>
          <div className="flex justify-between">
            <span className="text-gray-400">Delta (CVD)</span>
            <span className={`font-mono ${stats.cumulativeDelta >= 0 ? 'text-green-500' : 'text-red-500'}`}>
              {stats.cumulativeDelta >= 0 ? '+' : ''}{formatVolume(stats.cumulativeDelta)}
            </span>
          </div>
          <div className="flex justify-between border-t border-gray-700 pt-1 mt-1">
            <span className="text-gray-400">Buy %</span>
            <span className="font-mono">{stats.buyPercent}%</span>
          </div>
          <div className="flex justify-between">
            <span className="text-gray-400">Sell %</span>
            <span className="font-mono">{stats.sellPercent}%</span>
          </div>
          <div className="flex justify-between">
            <span className="text-gray-400">Volume Ratio</span>
            <span className="font-mono">{stats.volumeRatio}</span>
          </div>
        </div>
      </div>

      {/* Price Section */}
      <div className="bg-gray-800 rounded p-3">
        <h4 className="text-sm font-medium text-gray-400 mb-2">Price</h4>
        <div className="space-y-1 text-sm">
          <div className="flex justify-between">
            <span className="text-gray-400">High</span>
            <span className="font-mono text-green-500">{formatPrice(stats.highPrice)}</span>
          </div>
          <div className="flex justify-between">
            <span className="text-gray-400">Low</span>
            <span className="font-mono text-red-500">{formatPrice(stats.lowPrice)}</span>
          </div>
          <div className="flex justify-between">
            <span className="text-gray-400">Close</span>
            <span className="font-mono">{formatPrice(stats.closePrice)}</span>
          </div>
          <div className="flex justify-between">
            <span className="text-gray-400">Open</span>
            <span className="font-mono">{formatPrice(stats.openPrice)}</span>
          </div>
          <div className="flex justify-between border-t border-gray-700 pt-1 mt-1">
            <span className="text-gray-400">Change</span>
            <span className={`font-mono ${isPositive ? 'text-green-500' : 'text-red-500'}`}>
              {isPositive ? '+' : ''}{formatPrice(priceChange)}
            </span>
          </div>
          <div className="flex justify-between">
            <span className="text-gray-400">Change %</span>
            <span className={`font-mono ${isPositive ? 'text-green-500' : 'text-red-500'}`}>
              {isPositive ? '+' : ''}{priceChangePercent.toFixed(2)}%
            </span>
          </div>
          <div className="flex justify-between">
            <span className="text-gray-400">Range</span>
            <span className="font-mono">{formatPrice(stats.highPrice - stats.lowPrice)}</span>
          </div>
        </div>
      </div>

      {/* Session Info */}
      <div className="bg-gray-800 rounded p-3 col-span-2">
        <h4 className="text-sm font-medium text-gray-400 mb-2">Session Info</h4>
        <div className="grid grid-cols-3 gap-4 text-sm">
          <div className="flex justify-between">
            <span className="text-gray-400">Duration</span>
            <span className="font-mono">{formatDuration(stats.sessionDuration)}</span>
          </div>
          <div className="flex justify-between">
            <span className="text-gray-400">Candles</span>
            <span className="font-mono">{stats.candleCount}</span>
          </div>
          <div className="flex justify-between">
            <span className="text-gray-400">Avg Volume</span>
            <span className="font-mono">
              {stats.candleCount > 0 ? formatVolume(stats.totalVolume / stats.candleCount) : '-'}
            </span>
          </div>
        </div>
      </div>
    </div>
  );
};

export default SessionSummary;
