/**
 * chartWorker.js - Web Worker for OffscreenCanvas Chart Rendering
 * 
 * This worker handles:
 * - Background chart rendering using OffscreenCanvas
 * - Indicator calculations (SMA, EMA, RSI, MACD, Bollinger Bands)
 * - Data processing off the main thread
 * - Frame composition for smooth animations
 * 
 * Communication via postMessage:
 * - Main -> Worker: 'init', 'render', 'calculateIndicator', 'resize'
 * - Worker -> Main: 'ready', 'rendered', 'indicators', 'error'
 */

// Worker state
let offscreenCanvas = null;
let offscreenContext = null;
let width = 800;
let height = 600;
let dpr = 1;

// Chart data
let candles = [];
let trades = [];
let orderBook = null;

// Indicator configurations
let indicatorConfigs = {};

// Performance metrics
let lastRenderTime = 0;
let frameCount = 0;
let fps = 0;
let lastFPSUpdate = 0;

// Indicator calculation functions
const IndicatorCalculations = {
  /**
   * Simple Moving Average
   */
  SMA: (data, period) => {
    const result = [];
    for (let i = 0; i < data.length; i++) {
      if (i < period - 1) {
        result.push(null);
      } else {
        let sum = 0;
        for (let j = 0; j < period; j++) {
          sum += data[i - j];
        }
        result.push(sum / period);
      }
    }
    return result;
  },

  /**
   * Exponential Moving Average
   */
  EMA: (data, period) => {
    const result = [];
    const multiplier = 2 / (period + 1);
    
    // Start with SMA for first value
    let sum = 0;
    for (let i = 0; i < Math.min(period, data.length); i++) {
      sum += data[i];
    }
    let ema = sum / Math.min(period, data.length);
    
    for (let i = 0; i < data.length; i++) {
      if (i < period - 1) {
        result.push(null);
      } else if (i === period - 1) {
        result.push(ema);
      } else {
        ema = (data[i] - ema) * multiplier + ema;
        result.push(ema);
      }
    }
    return result;
  },

  /**
   * Relative Strength Index
   */
  RSI: (data, period = 14) => {
    const result = [];
    const gains = [];
    const losses = [];
    
    // Calculate price changes
    for (let i = 1; i < data.length; i++) {
      const change = data[i] - data[i - 1];
      gains.push(change > 0 ? change : 0);
      losses.push(change < 0 ? -change : 0);
    }
    
    // First RSI value
    let avgGain = gains.slice(0, period).reduce((a, b) => a + b, 0) / period;
    let avgLoss = losses.slice(0, period).reduce((a, b) => a + b, 0) / period;
    
    result.push(null); // No RSI for first candle
    
    for (let i = 1; i < data.length; i++) {
      if (i < period) {
        result.push(null);
      } else if (i === period) {
        const rs = avgGain / avgLoss;
        result.push(100 - (100 / (1 + rs)));
      } else {
        avgGain = (avgGain * (period - 1) + gains[i - 1]) / period;
        avgLoss = (avgLoss * (period - 1) + losses[i - 1]) / period;
        
        if (avgLoss === 0) {
          result.push(100);
        } else {
          const rs = avgGain / avgLoss;
          result.push(100 - (100 / (1 + rs)));
        }
      }
    }
    
    return result;
  },

  /**
   * MACD (Moving Average Convergence Divergence)
   */
  MACD: (data, fastPeriod = 12, slowPeriod = 26, signalPeriod = 9) => {
    const emaFast = IndicatorCalculations.EMA(data, fastPeriod);
    const emaSlow = IndicatorCalculations.EMA(data, slowPeriod);
    
    const macdLine = [];
    for (let i = 0; i < data.length; i++) {
      if (emaFast[i] === null || emaSlow[i] === null) {
        macdLine.push(null);
      } else {
        macdLine.push(emaFast[i] - emaSlow[i]);
      }
    }
    
    // Signal line is EMA of MACD
    const validMACD = macdLine.filter(v => v !== null);
    const signalEMA = IndicatorCalculations.EMA(validMACD, signalPeriod);
    
    const signalLine = [];
    const histogram = [];
    let signalIndex = 0;
    
    for (let i = 0; i < data.length; i++) {
      if (macdLine[i] === null) {
        signalLine.push(null);
        histogram.push(null);
      } else {
        if (signalIndex < signalEMA.length) {
          signalLine.push(signalEMA[signalIndex]);
          histogram.push(macdLine[i] - signalEMA[signalIndex]);
          signalIndex++;
        }
      }
    }
    
    return { macdLine, signalLine, histogram };
  },

  /**
   * Bollinger Bands
   */
  BollingerBands: (data, period = 20, stdDev = 2) => {
    const sma = IndicatorCalculations.SMA(data, period);
    const upperBand = [];
    const lowerBand = [];
    
    for (let i = 0; i < data.length; i++) {
      if (i < period - 1 || sma[i] === null) {
        upperBand.push(null);
        lowerBand.push(null);
      } else {
        // Calculate standard deviation
        let sumSquares = 0;
        for (let j = 0; j < period; j++) {
          const diff = data[i - j] - sma[i];
          sumSquares += diff * diff;
        }
        const std = Math.sqrt(sumSquares / period);
        
        upperBand.push(sma[i] + stdDev * std);
        lowerBand.push(sma[i] - stdDev * std);
      }
    }
    
    return { middleBand: sma, upperBand, lowerBand };
  },

  /**
   * Average True Range
   */
  ATR: (high, low, close, period = 14) => {
    const trueRanges = [];
    
    for (let i = 0; i < close.length; i++) {
      if (i === 0) {
        trueRanges.push(high[i] - low[i]);
      } else {
        const tr = Math.max(
          high[i] - low[i],
          Math.abs(high[i] - close[i - 1]),
          Math.abs(low[i] - close[i - 1])
        );
        trueRanges.push(tr);
      }
    }
    
    return IndicatorCalculations.SMA(trueRanges, period);
  },

  /**
   * VWAP (Volume Weighted Average Price)
   */
  VWAP: (high, low, close, volume) => {
    const result = [];
    let cumulativeTPV = 0;
    let cumulativeVolume = 0;
    
    for (let i = 0; i < close.length; i++) {
      const typicalPrice = (high[i] + low[i] + close[i]) / 3;
      cumulativeTPV += typicalPrice * volume[i];
      cumulativeVolume += volume[i];
      
      if (cumulativeVolume === 0) {
        result.push(0);
      } else {
        result.push(cumulativeTPV / cumulativeVolume);
      }
    }
    
    return result;
  }
};

/**
 * Initialize OffscreenCanvas
 */
function initCanvas(canvas, canvasWidth, canvasHeight, devicePixelRatio) {
  try {
    offscreenCanvas = canvas;
    width = canvasWidth;
    height = canvasHeight;
    dpr = devicePixelRatio || 1;
    
    offscreenContext = offscreenCanvas.getContext('2d', {
      alpha: false,
      desynchronized: true
    });
    
    // Scale for high DPI
    offscreenContext.scale(dpr, dpr);
    
    console.log('[Worker] Canvas initialized:', width, 'x', height, '@', dpr, 'x');
    
    return { success: true, width, height };
  } catch (error) {
    console.error('[Worker] Canvas init error:', error);
    return { success: false, error: error.message };
  }
}

/**
 * Render chart to OffscreenCanvas
 */
function renderChart(data) {
  if (!offscreenContext) {
    return { success: false, error: 'Canvas not initialized' };
  }
  
  const ctx = offscreenContext;
  const startTime = performance.now();
  
  // Clear canvas
  ctx.fillStyle = '#1a1a2e';
  ctx.fillRect(0, 0, width, height);
  
  if (!data || data.candles.length === 0) {
    // Draw placeholder
    ctx.fillStyle = '#888';
    ctx.font = '16px sans-serif';
    ctx.textAlign = 'center';
    ctx.fillText('Waiting for data...', width / 2, height / 2);
    
    return { success: true, renderTime: performance.now() - startTime };
  }
  
  // Calculate price range
  const prices = [];
  for (const candle of data.candles) {
    prices.push(candle.high, candle.low);
  }
  
  for (const indicator of Object.values(data.indicators || {})) {
    if (indicator && indicator.upperBand) {
      prices.push(...indicator.upperBand.filter(v => v !== null));
    }
    if (indicator && indicator.lowerBand) {
      prices.push(...indicator.lowerBand.filter(v => v !== null));
    }
  }
  
  const minPrice = Math.min(...prices.filter(p => p !== null && !isNaN(p)));
  const maxPrice = Math.max(...prices.filter(p => p !== null && !isNaN(p)));
  const priceRange = maxPrice - minPrice || 1;
  
  // Padding
  const padding = { top: 40, right: 60, bottom: 30, left: 10 };
  const chartWidth = width - padding.left - padding.right;
  const chartHeight = height - padding.top - padding.bottom;
  
  // Price scale
  const priceToY = (price) => {
    return padding.top + chartHeight - ((price - minPrice) / priceRange) * chartHeight;
  };
  
  // Time scale
  const timeStart = data.candles[0].time;
  const timeEnd = data.candles[data.candles.length - 1].time;
  const timeRange = timeEnd - timeStart || 1;
  
  const timeToX = (time) => {
    return padding.left + ((time - timeStart) / timeRange) * chartWidth;
  };
  
  // Draw grid
  ctx.strokeStyle = '#2a2a3e';
  ctx.lineWidth = 1;
  
  // Horizontal grid lines
  const gridLines = 5;
  for (let i = 0; i <= gridLines; i++) {
    const price = minPrice + (priceRange / gridLines) * i;
    const y = priceToY(price);
    
    ctx.beginPath();
    ctx.moveTo(padding.left, y);
    ctx.lineTo(width - padding.right, y);
    ctx.stroke();
    
    // Price labels
    ctx.fillStyle = '#666';
    ctx.font = '11px monospace';
    ctx.textAlign = 'left';
    ctx.fillText(price.toFixed(2), width - padding.right + 5, y + 4);
  }
  
  // Draw candles
  const candleWidth = Math.max(1, (chartWidth / data.candles.length) * 0.8);
  
  for (const candle of data.candles) {
    const x = timeToX(candle.time);
    
    // Wick
    ctx.strokeStyle = candle.close >= candle.open ? '#00d4aa' : '#ff4757';
    ctx.lineWidth = 1;
    ctx.beginPath();
    ctx.moveTo(x, priceToY(candle.high));
    ctx.lineTo(x, priceToY(candle.low));
    ctx.stroke();
    
    // Body
    const bodyTop = priceToY(Math.max(candle.open, candle.close));
    const bodyBottom = priceToY(Math.min(candle.open, candle.close));
    const bodyHeight = Math.max(1, bodyBottom - bodyTop);
    
    ctx.fillStyle = candle.close >= candle.open ? '#00d4aa' : '#ff4757';
    ctx.fillRect(x - candleWidth / 2, bodyTop, candleWidth, bodyHeight);
  }
  
  // Draw indicators
  if (data.indicators) {
    // Bollinger Bands
    if (data.indicators.upperBand && data.indicators.lowerBand) {
      ctx.strokeStyle = 'rgba(255, 215, 0, 0.5)';
      ctx.lineWidth = 1;
      
      // Upper band
      ctx.beginPath();
      let started = false;
      for (let i = 0; i < data.indicators.upperBand.length; i++) {
        if (data.indicators.upperBand[i] !== null) {
          const x = timeToX(data.candles[i].time);
          const y = priceToY(data.indicators.upperBand[i]);
          if (!started) {
            ctx.moveTo(x, y);
            started = true;
          } else {
            ctx.lineTo(x, y);
          }
        }
      }
      ctx.stroke();
      
      // Lower band
      ctx.beginPath();
      started = false;
      for (let i = 0; i < data.indicators.lowerBand.length; i++) {
        if (data.indicators.lowerBand[i] !== null) {
          const x = timeToX(data.candles[i].time);
          const y = priceToY(data.indicators.lowerBand[i]);
          if (!started) {
            ctx.moveTo(x, y);
            started = true;
          } else {
            ctx.lineTo(x, y);
          }
        }
      }
      ctx.stroke();
    }
    
    // SMA/EMA lines
    if (data.indicators.sma) {
      ctx.strokeStyle = 'rgba(0, 150, 255, 0.7)';
      ctx.lineWidth = 1.5;
      ctx.beginPath();
      let started = false;
      for (let i = 0; i < data.indicators.sma.length; i++) {
        if (data.indicators.sma[i] !== null) {
          const x = timeToX(data.candles[i].time);
          const y = priceToY(data.indicators.sma[i]);
          if (!started) {
            ctx.moveTo(x, y);
            started = true;
          } else {
            ctx.lineTo(x, y);
          }
        }
      }
      ctx.stroke();
    }
    
    if (data.indicators.ema) {
      ctx.strokeStyle = 'rgba(255, 100, 0, 0.7)';
      ctx.lineWidth = 1.5;
      ctx.beginPath();
      let started = false;
      for (let i = 0; i < data.indicators.ema.length; i++) {
        if (data.indicators.ema[i] !== null) {
          const x = timeToX(data.candles[i].time);
          const y = priceToY(data.indicators.ema[i]);
          if (!started) {
            ctx.moveTo(x, y);
            started = true;
          } else {
            ctx.lineTo(x, y);
          }
        }
      }
      ctx.stroke();
    }
  }
  
  // Draw crosshair and price info
  if (data.mousePosition) {
    const { x, y } = data.mousePosition;
    
    // Crosshair lines
    ctx.strokeStyle = 'rgba(255, 255, 255, 0.3)';
    ctx.setLineDash([5, 5]);
    ctx.beginPath();
    ctx.moveTo(x, padding.top);
    ctx.lineTo(x, height - padding.bottom);
    ctx.stroke();
    
    ctx.beginPath();
    ctx.moveTo(padding.left, y);
    ctx.lineTo(width - padding.right, y);
    ctx.stroke();
    ctx.setLineDash([]);
    
    // Price at cursor
    const cursorPrice = minPrice + ((height - padding.bottom - y) / chartHeight) * priceRange;
    ctx.fillStyle = '#fff';
    ctx.font = '12px monospace';
    ctx.fillText(cursorPrice.toFixed(2), width - padding.right + 5, y + 4);
  }
  
  // Update metrics
  const renderTime = performance.now() - startTime;
  frameCount++;
  
  const now = performance.now();
  if (now - lastFPSUpdate >= 1000) {
    fps = frameCount;
    frameCount = 0;
    lastFPSUpdate = now;
  }
  
  lastRenderTime = renderTime;
  
  return {
    success: true,
    renderTime,
    fps,
    candlesRendered: data.candles.length
  };
}

/**
 * Calculate indicators from candles
 */
function calculateIndicators(candleData, indicatorTypes) {
  const results = {};
  
  // Prepare price arrays
  const closes = candleData.map(c => c.close);
  const highs = candleData.map(c => c.high);
  const lows = candleData.map(c => c.low);
  const volumes = candleData.map(c => c.volume || 0);
  
  for (const config of indicatorTypes) {
    const { type, params = {} } = config;
    
    switch (type.toUpperCase()) {
      case 'SMA':
        results.sma = IndicatorCalculations.SMA(closes, params.period || 20);
        break;
        
      case 'EMA':
        results.ema = IndicatorCalculations.EMA(closes, params.period || 20);
        break;
        
      case 'RSI':
        results.rsi = IndicatorCalculations.RSI(closes, params.period || 14);
        break;
        
      case 'MACD': {
        const macd = IndicatorCalculations.MACD(
          closes,
          params.fastPeriod || 12,
          params.slowPeriod || 26,
          params.signalPeriod || 9
        );
        results.macd = macd.macdLine;
        results.macdSignal = macd.signalLine;
        results.macdHistogram = macd.histogram;
        break;
      }
      
      case 'BB':
      case 'BOLLINGER': {
        const bb = IndicatorCalculations.BollingerBands(
          closes,
          params.period || 20,
          params.stdDev || 2
        );
        results.upperBand = bb.upperBand;
        results.middleBand = bb.middleBand;
        results.lowerBand = bb.lowerBand;
        break;
      }
        
      case 'ATR':
        results.atr = IndicatorCalculations.ATR(highs, lows, closes, params.period || 14);
        break;
        
      case 'VWAP':
        results.vwap = IndicatorCalculations.VWAP(highs, lows, closes, volumes);
        break;
    }
  }
  
  return results;
}

/**
 * Process data batch for high-frequency updates
 */
function processDataBatch(batch) {
  // Merge batch data into main data
  for (const item of batch) {
    if (item.type === 'candle') {
      // Update or add candle
      const existingIndex = candles.findIndex(c => c.time === item.data.time);
      if (existingIndex >= 0) {
        candles[existingIndex] = item.data;
      } else {
        candles.push(item.data);
        // Keep only last 1000 candles
        if (candles.length > 1000) {
          candles.shift();
        }
      }
    } else if (item.type === 'trade') {
      trades.push(item.data);
      // Keep only last 5000 trades
      if (trades.length > 5000) {
        trades.shift();
      }
    } else if (item.type === 'orderbook') {
      orderBook = item.data;
    }
  }
  
  return {
    candles: candles.length,
    trades: trades.length,
    hasOrderBook: !!orderBook
  };
}

// Message handler
self.onmessage = function(event) {
  const { type, data, id } = event.data;
  
  try {
    switch (type) {
      case 'init': {
        const { canvas, width, height, dpr } = data;
        const result = initCanvas(canvas, width, height, dpr);
        self.postMessage({ type: 'ready', id, data: result });
        break;
      }
      
      case 'render': {
        const result = renderChart(data);
        self.postMessage({ type: 'rendered', id, data: result });
        break;
      }
      
      case 'calculateIndicators': {
        const { candles: candleData, indicators: indicatorTypes } = data;
        const results = calculateIndicators(candleData, indicatorTypes);
        self.postMessage({ type: 'indicators', id, data: results });
        break;
      }
      
      case 'processBatch': {
        const result = processDataBatch(data);
        self.postMessage({ type: 'batchProcessed', id, data: result });
        break;
      }
      
      case 'resize': {
        width = data.width;
        height = data.height;
        dpr = data.dpr || 1;
        if (offscreenContext) {
          offscreenContext.scale(dpr, dpr);
        }
        self.postMessage({ type: 'resized', id, data: { width, height } });
        break;
      }
      
      case 'setData': {
        if (data.candles) candles = data.candles;
        if (data.trades) trades = data.trades;
        if (data.orderBook) orderBook = data.orderBook;
        self.postMessage({ type: 'dataSet', id, data: { success: true } });
        break;
      }
      
      case 'getMetrics': {
        self.postMessage({
          type: 'metrics',
          id,
          data: {
            fps,
            lastRenderTime,
            candlesCount: candles.length,
            tradesCount: trades.length
          }
        });
        break;
      }
      
      default:
        console.warn('[Worker] Unknown message type:', type);
        self.postMessage({ type: 'error', id, data: { error: 'Unknown message type' } });
    }
  } catch (error) {
    console.error('[Worker] Error:', error);
    self.postMessage({ type: 'error', id, data: { error: error.message } });
  }
};

// Signal ready
self.postMessage({ type: 'initialized' });
