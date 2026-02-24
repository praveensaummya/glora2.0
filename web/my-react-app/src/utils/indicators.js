/**
 * Technical Indicators Utility Functions
 * 
 * Provides calculation functions for SMA, EMA, Bollinger Bands, RSI, and ATR
 * These are computed client-side for overlay on TradingView Lightweight Charts
 */

// ============ SMA (Simple Moving Average) ============

/**
 * Calculate Simple Moving Average
 * @param {Array} data - Array of {time, close} objects
 * @param {number} period - Number of periods (e.g., 20, 50, 200)
 * @returns {Array} - Array of {time, value} objects
 */
export function calculateSMA(data, period) {
  if (!data || data.length < period) return [];
  
  const result = [];
  
  for (let i = period - 1; i < data.length; i++) {
    let sum = 0;
    for (let j = 0; j < period; j++) {
      sum += data[i - j].close;
    }
    
    result.push({
      time: data[i].time,
      value: sum / period
    });
  }
  
  return result;
}

// ============ EMA (Exponential Moving Average) ============

/**
 * Calculate Exponential Moving Average
 * @param {Array} data - Array of {time, close} objects
 * @param {number} period - Number of periods (e.g., 12, 26)
 * @returns {Array} - Array of {time, value} objects
 */
export function calculateEMA(data, period) {
  if (!data || data.length < period) return [];
  
  const result = [];
  const multiplier = 2 / (period + 1);
  
  // Start with SMA for the first value
  let sum = 0;
  for (let i = 0; i < period; i++) {
    sum += data[i].close;
  }
  let ema = sum / period;
  
  result.push({
    time: data[period - 1].time,
    value: ema
  });
  
  // Calculate EMA for remaining data points
  for (let i = period; i < data.length; i++) {
    ema = (data[i].close - ema) * multiplier + ema;
    result.push({
      time: data[i].time,
      value: ema
    });
  }
  
  return result;
}

// ============ Bollinger Bands ============

/**
 * Calculate Bollinger Bands
 * @param {Array} data - Array of {time, close} objects
 * @param {number} period - Number of periods (default 20)
 * @param {number} stdDev - Number of standard deviations (default 2)
 * @returns {Object} - { upper, middle, lower } arrays of {time, value}
 */
export function calculateBollingerBands(data, period = 20, stdDev = 2) {
  if (!data || data.length < period) return { upper: [], middle: [], lower: [] };
  
  const upper = [];
  const middle = [];
  const lower = [];
  
  for (let i = period - 1; i < data.length; i++) {
    // Calculate SMA (middle band)
    let sum = 0;
    for (let j = 0; j < period; j++) {
      sum += data[i - j].close;
    }
    const sma = sum / period;
    
    // Calculate standard deviation
    let varianceSum = 0;
    for (let j = 0; j < period; j++) {
      varianceSum += Math.pow(data[i - j].close - sma, 2);
    }
    const std = Math.sqrt(varianceSum / period);
    
    const time = data[i].time;
    
    middle.push({ time, value: sma });
    upper.push({ time, value: sma + (std * stdDev) });
    lower.push({ time, value: sma - (std * stdDev) });
  }
  
  return { upper, middle, lower };
}

// ============ RSI (Relative Strength Index) ============

/**
 * Calculate RSI (Relative Strength Index)
 * @param {Array} data - Array of {time, close} objects
 * @param {number} period - Number of periods (default 14)
 * @returns {Array} - Array of {time, value} objects
 */
export function calculateRSI(data, period = 14) {
  if (!data || data.length < period + 1) return [];
  
  const result = [];
  const gains = [];
  const losses = [];
  
  // Calculate price changes
  for (let i = 1; i < data.length; i++) {
    const change = data[i].close - data[i - 1].close;
    gains.push(change > 0 ? change : 0);
    losses.push(change < 0 ? -change : 0);
  }
  
  // Calculate initial average gain and loss
  let avgGain = 0;
  let avgLoss = 0;
  
  for (let i = 0; i < period; i++) {
    avgGain += gains[i];
    avgLoss += losses[i];
  }
  
  avgGain /= period;
  avgLoss /= period;
  
  // First RSI value
  if (avgLoss === 0) {
    result.push({ time: data[period].time, value: 100 });
  } else {
    const rs = avgGain / avgLoss;
    result.push({ time: data[period].time, value: 100 - (100 / (1 + rs)) });
  }
  
  // Calculate RSI for remaining data points using smoothed averages
  for (let i = period; i < gains.length; i++) {
    avgGain = (avgGain * (period - 1) + gains[i]) / period;
    avgLoss = (avgLoss * (period - 1) + losses[i]) / period;
    
    if (avgLoss === 0) {
      result.push({ time: data[i + 1].time, value: 100 });
    } else {
      const rs = avgGain / avgLoss;
      result.push({ time: data[i + 1].time, value: 100 - (100 / (1 + rs)) });
    }
  }
  
  return result;
}

// ============ ATR (Average True Range) ============

/**
 * Calculate ATR (Average True Range)
 * @param {Array} data - Array of {time, high, low, close} objects
 * @param {number} period - Number of periods (default 14)
 * @returns {Array} - Array of {time, value} objects
 */
export function calculateATR(data, period = 14) {
  if (!data || data.length < period + 1) return [];
  
  const result = [];
  const trueRanges = [];
  
  // Calculate True Range for each candle
  for (let i = 1; i < data.length; i++) {
    const high = data[i].high;
    const low = data[i].low;
    const prevClose = data[i - 1].close;
    
    const tr = Math.max(
      high - low,
      Math.abs(high - prevClose),
      Math.abs(low - prevClose)
    );
    
    trueRanges.push(tr);
  }
  
  // First ATR is simple average of first 'period' true ranges
  let atr = 0;
  for (let i = 0; i < period; i++) {
    atr += trueRanges[i];
  }
  atr /= period;
  
  result.push({ time: data[period].time, value: atr });
  
  // Calculate ATR for remaining data points using smoothed average
  for (let i = period; i < trueRanges.length; i++) {
    atr = (atr * (period - 1) + trueRanges[i]) / period;
    result.push({ time: data[i + 1].time, value: atr });
  }
  
  return result;
}

// ============ Helper Functions ============

/**
 * Map indicator names to their default periods
 */
export const INDICATOR_PRESETS = {
  sma: {
    20: { name: 'SMA 20', period: 20 },
    50: { name: 'SMA 50', period: 50 },
    200: { name: 'SMA 200', period: 200 }
  },
  ema: {
    12: { name: 'EMA 12', period: 12 },
    26: { name: 'EMA 26', period: 26 }
  },
  bollinger: {
    '20,2': { name: 'Bollinger Bands (20,2)', period: 20, stdDev: 2 }
  },
  rsi: {
    14: { name: 'RSI (14)', period: 14 }
  },
  atr: {
    14: { name: 'ATR (14)', period: 14 }
  }
};

/**
 * Color schemes for indicators
 */
export const INDICATOR_COLORS = {
  sma20: '#22d3ee',    // cyan
  sma50: '#a855f7',    // purple
  sma200: '#f59e0b',   // amber
  ema12: '#ec4899',    // pink
  ema26: '#8b5cf6',    // violet
  bollingerUpper: '#22c55e',  // green
  bollingerMiddle: '#eab308', // yellow
  bollingerLower: '#22c55e',  // green
  rsi: '#06b6d4',      // cyan
  atr: '#f97316'       // orange
};

/**
 * Binance interval mappings
 */
export const BINANCE_INTERVALS = {
  '1s': '1s',
  '1m': '1m',
  '5m': '5m',
  '15m': '15m',
  '1h': '1h',
  '4h': '4h',
  '1D': '1d',
  '1W': '1w',
  '1M': '1M'
};

/**
 * Timeframe display names
 */
export const TIMEFRAME_OPTIONS = [
  { value: '1s', label: '1s', description: '1 Second' },
  { value: '1m', label: '1m', description: '1 Minute' },
  { value: '5m', label: '5m', description: '5 Minutes' },
  { value: '15m', label: '15m', description: '15 Minutes' },
  { value: '1h', label: '1h', description: '1 Hour' },
  { value: '4h', label: '4h', description: '4 Hours' },
  { value: '1D', label: '1D', description: '1 Day' },
  { value: '1W', label: '1W', description: '1 Week' },
  { value: '1M', label: '1M', description: '1 Month' }
];
