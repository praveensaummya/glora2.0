/**
 * Alert System for Order Flow Trading
 * 
 * Provides alert functionality for:
 * - Stacked imbalances detection
 * - Delta flip detection
 * - Volume spike alerts
 * - POC break alerts
 * - Price cross alerts
 * - Custom alert conditions
 */

// ============ Alert Types ============

export const AlertType = {
  IMBALANCE_STACK: 'IMBALANCE_STACK',
  DELTA_FLIP: 'DELTA_FLIP',
  VOLUME_SPIKE: 'VOLUME_SPIKE',
  POC_BREAK: 'POC_BREAK',
  PRICE_CROSS: 'PRICE_CROSS',
  CUSTOM: 'CUSTOM'
};

// ============ Default Alert Configurations ============

export const DEFAULT_ALERT_CONFIGS = {
  [AlertType.IMBALANCE_STACK]: {
    name: 'Stacked Imbalance',
    description: 'Consecutive levels of one-sided aggression',
    enabled: true,
    params: {
      minLevels: 3,
      imbalanceRatio: 4,
      minVolume: 100
    }
  },
  [AlertType.DELTA_FLIP]: {
    name: 'Delta Flip',
    description: 'Buy to sell or vice versa reversal',
    enabled: true,
    params: {
      threshold: 0.7,
      lookbackCandles: 3
    }
  },
  [AlertType.VOLUME_SPIKE]: {
    name: 'Volume Spike',
    description: 'Volume exceeds 2x average',
    enabled: true,
    params: {
      multiplier: 2,
      lookbackPeriod: 20
    }
  },
  [AlertType.POC_BREAK]: {
    name: 'POC Break',
    description: 'Price crosses Point of Control',
    enabled: true,
    params: {
      confirmationBars: 1
    }
  },
  [AlertType.PRICE_CROSS]: {
    name: 'Price Cross',
    description: 'Price crosses specified level',
    enabled: false,
    params: {
      price: 0,
      direction: 'both'
    }
  }
};

// ============ Alert Manager Class ============

class AlertManager {
  constructor() {
    this.activeAlerts = new Map();
    this.alertHistory = [];
    this.listeners = new Set();
    this.audioContext = null;
    this.debounceTimers = new Map();
    this.debounceDelay = 1000;
    this.maxHistory = 100;
    this.paused = false;
    this.sessionStartTime = Date.now();
    
    this.sessionStats = {
      totalVolume: 0,
      buyVolume: 0,
      sellVolume: 0,
      highPrice: -Infinity,
      lowPrice: Infinity,
      closePrice: 0,
      cumulativeDelta: 0,
      candleCount: 0
    };
    
    this._initAudioContext();
  }

  _initAudioContext() {
    try {
      this.audioContext = new (window.AudioContext || window.webkitAudioContext)();
    } catch (err) {
      console.warn('Web Audio API not supported:', err);
    }
  }

  playAlertSound(type = 'default') {
    if (!this.audioContext) return;
    
    if (this.audioContext.state === 'suspended') {
      this.audioContext.resume();
    }

    const oscillator = this.audioContext.createOscillator();
    const gainNode = this.audioContext.createGain();
    
    oscillator.connect(gainNode);
    gainNode.connect(this.audioContext.destination);
    
    const frequencies = {
      [AlertType.IMBALANCE_STACK]: 880,
      [AlertType.DELTA_FLIP]: 660,
      [AlertType.VOLUME_SPIKE]: 550,
      [AlertType.POC_BREAK]: 440,
      [AlertType.PRICE_CROSS]: 770,
      'default': 440
    };
    
    oscillator.frequency.value = frequencies[type] || frequencies.default;
    oscillator.type = 'sine';
    
    gainNode.gain.setValueAtTime(0.3, this.audioContext.currentTime);
    gainNode.gain.exponentialRampToValueAtTime(0.01, this.audioContext.currentTime + 0.5);
    
    oscillator.start(this.audioContext.currentTime);
    oscillator.stop(this.audioContext.currentTime + 0.5);
  }

  subscribe(callback) {
    this.listeners.add(callback);
    return () => this.listeners.delete(callback);
  }

  _notifyListeners(alert) {
    this.listeners.forEach(callback => {
      try {
        callback(alert);
      } catch (err) {
        console.error('Alert listener error:', err);
      }
    });
  }

  _addToHistory(alert) {
    this.alertHistory.unshift({
      ...alert,
      timestamp: Date.now()
    });
    
    if (this.alertHistory.length > this.maxHistory) {
      this.alertHistory = this.alertHistory.slice(0, this.maxHistory);
    }
  }

  _debounce(key, callback) {
    if (this.debounceTimers.has(key)) {
      clearTimeout(this.debounceTimers.get(key));
    }
    
    const timer = setTimeout(() => {
      this.debounceTimers.delete(key);
      callback();
    }, this.debounceDelay);
    
    this.debounceTimers.set(key, timer);
  }

  detectImbalanceStack(footprintData, params = {}) {
    const { minLevels = 3, imbalanceRatio = 4, minVolume = 100 } = params;
    
    if (!footprintData || footprintData.length < minLevels) return null;
    
    const recentCandles = footprintData.slice(-minLevels);
    let consecutiveImbalance = 0;
    let alertPrice = null;
    
    for (const candle of recentCandles) {
      const levels = Object.values(candle.priceLevels || {});
      if (levels.length === 0) continue;
      
      let maxImbalance = 0;
      let imbalanceLevel = null;
      
      for (const level of levels) {
        if (level.totalVolume < minVolume) continue;
        
        const ratio = level.bidVolume > level.askVolume 
          ? level.bidVolume / (level.askVolume || 1)
          : level.askVolume / (level.bidVolume || 1);
        
        if (ratio > maxImbalance) {
          maxImbalance = ratio;
          imbalanceLevel = level;
        }
      }
      
      if (maxImbalance >= imbalanceRatio && imbalanceLevel) {
        consecutiveImbalance++;
        alertPrice = imbalanceLevel.price;
      } else {
        consecutiveImbalance = 0;
      }
    }
    
    if (consecutiveImbalance >= minLevels && alertPrice) {
      const lastCandle = footprintData[footprintData.length - 1];
      const direction = lastCandle.close > lastCandle.open ? 'buy' : 'sell';
      
      return {
        type: AlertType.IMBALANCE_STACK,
        message: `Stacked ${direction} imbalance detected at ${alertPrice}`,
        data: {
          price: alertPrice,
          direction,
          levels: consecutiveImbalance,
          ratio: imbalanceRatio
        },
        priority: 'high'
      };
    }
    
    return null;
  }

  detectDeltaFlip(footprintData, params = {}) {
    const { threshold = 0.7, lookbackCandles = 3 } = params;
    
    if (!footprintData || footprintData.length < lookbackCandles + 1) return null;
    
    const recentCandles = footprintData.slice(-lookbackCandles - 1);
    
    const deltas = recentCandles.map(candle => {
      const levels = Object.values(candle.priceLevels || {});
      return levels.reduce((sum, level) => sum + (level.delta || 0), 0);
    });
    
    const currentDelta = deltas[deltas.length - 1];
    const previousDelta = deltas.slice(0, -1).reduce((sum, d) => sum + d, 0);
    
    const currentSign = Math.sign(currentDelta);
    const previousSign = Math.sign(previousDelta);
    
    if (currentSign !== 0 && previousSign !== 0 && currentSign !== previousSign) {
      const flipMagnitude = Math.abs(currentDelta) / (Math.abs(previousDelta) || 1);
      
      if (flipMagnitude >= threshold) {
        const direction = currentDelta > 0 ? 'buy' : 'sell';
        const candle = recentCandles[recentCandles.length - 1];
        
        return {
          type: AlertType.DELTA_FLIP,
          message: `Delta flip to ${direction} side detected`,
          data: {
            previousDelta,
            currentDelta,
            direction,
            magnitude: flipMagnitude,
            price: candle.close
          },
          priority: 'high'
        };
      }
    }
    
    return null;
  }

  detectVolumeSpike(candles, params = {}) {
    const { multiplier = 2, lookbackPeriod = 20 } = params;
    
    if (!candles || candles.length < lookbackPeriod + 1) return null;
    
    const recentCandles = candles.slice(-lookbackPeriod - 1);
    const currentVolume = recentCandles[recentCandles.length - 1].volume;
    
    const previousVolumes = recentCandles.slice(0, -1).map(c => c.volume);
    const avgVolume = previousVolumes.reduce((a, b) => a + b, 0) / previousVolumes.length;
    
    if (currentVolume > avgVolume * multiplier) {
      const candle = recentCandles[recentCandles.length - 1];
      const percentIncrease = ((currentVolume - avgVolume) / avgVolume * 100).toFixed(1);
      
      return {
        type: AlertType.VOLUME_SPIKE,
        message: `Volume spike: ${percentIncrease}% above average`,
        data: {
          currentVolume,
          avgVolume,
          multiplier: currentVolume / avgVolume,
          price: candle.close,
          time: candle.time
        },
        priority: 'medium'
      };
    }
    
    return null;
  }

  detectPOCBreak(pocData, candles, params = {}) {
    const { confirmationBars = 1 } = params;
    
    if (!pocData || !candles || candles.length < confirmationBars + 1) return null;
    
    const recentCandles = candles.slice(-confirmationBars - 1);
    const pocPrice = pocData.price;
    
    let brokenUp = false;
    let brokenDown = false;
    
    for (let i = recentCandles.length - confirmationBars; i < recentCandles.length; i++) {
      const candle = recentCandles[i];
      
      if (candle.high > pocPrice) brokenUp = true;
      if (candle.low < pocPrice) brokenDown = true;
    }
    
    if (brokenUp || brokenDown) {
      const direction = brokenUp ? 'up' : 'down';
      const currentCandle = recentCandles[recentCandles.length - 1];
      
      return {
        type: AlertType.POC_BREAK,
        message: `POC broken to the ${direction} at ${pocPrice}`,
        data: {
          pocPrice,
          direction,
          currentPrice: currentCandle.close,
          time: currentCandle.time
        },
        priority: 'high'
      };
    }
    
    return null;
  }

  detectPriceCross(candles, params = {}) {
    const { price: targetPrice = 0, direction = 'both' } = params;
    
    if (!candles || targetPrice === 0 || candles.length < 2) return null;
    
    const currentCandle = candles[candles.length - 1];
    const previousCandle = candles[candles.length - 2];
    
    const crossedUp = previousCandle.close < targetPrice && currentCandle.close > targetPrice;
    const crossedDown = previousCandle.close > targetPrice && currentCandle.close < targetPrice;
    
    if ((crossedUp && (direction === 'up' || direction === 'both')) ||
        (crossedDown && (direction === 'down' || direction === 'both'))) {
      const crossDirection = crossedUp ? 'up' : 'down';
      
      return {
        type: AlertType.PRICE_CROSS,
        message: `Price crossed ${crossDirection} ${targetPrice}`,
        data: {
          targetPrice,
          direction: crossDirection,
          price: currentCandle.close,
          time: currentCandle.time
        },
        priority: 'medium'
      };
    }
    
    return null;
  }

  checkAlerts(marketData, alertConfigs = DEFAULT_ALERT_CONFIGS) {
    if (this.paused || !marketData) return;
    
    const { candles, footprint, poc } = marketData;
    
    for (const [alertType, config] of Object.entries(alertConfigs)) {
      if (!config.enabled) continue;
      
      let alert = null;
      
      if (alertType === AlertType.IMBALANCE_STACK) {
        alert = this.detectImbalanceStack(footprint, config.params);
      } else if (alertType === AlertType.DELTA_FLIP) {
        alert = this.detectDeltaFlip(footprint, config.params);
      } else if (alertType === AlertType.VOLUME_SPIKE) {
        alert = this.detectVolumeSpike(candles, config.params);
      } else if (alertType === AlertType.POC_BREAK) {
        alert = this.detectPOCBreak(poc, candles, config.params);
      } else if (alertType === AlertType.PRICE_CROSS) {
        alert = this.detectPriceCross(candles, config.params);
      }
      
      if (alert) {
        const debounceKey = alertType;
        this._debounce(debounceKey, () => {
          this._addToHistory(alert);
          this._notifyListeners(alert);
          this.playAlertSound(alertType);
        });
      }
    }
  }

  updateSessionStats(candleData, deltaData = null) {
    if (!candleData) return;
    
    const { volume, high, low, close } = candleData;
    
    this.sessionStats.totalVolume += volume;
    this.sessionStats.candleCount++;
    
    if (deltaData) {
      this.sessionStats.buyVolume += deltaData.buyVolume || 0;
      this.sessionStats.sellVolume += deltaData.sellVolume || 0;
      this.sessionStats.cumulativeDelta += deltaData.delta || 0;
    } else {
      const isBullish = close > candleData.open;
      if (isBullish) {
        this.sessionStats.buyVolume += volume;
      } else {
        this.sessionStats.sellVolume += volume;
      }
      this.sessionStats.cumulativeDelta += isBullish ? volume : -volume;
    }
    
    if (high > this.sessionStats.highPrice) {
      this.sessionStats.highPrice = high;
    }
    if (low < this.sessionStats.lowPrice) {
      this.sessionStats.lowPrice = low;
    }
    this.sessionStats.closePrice = close;
  }

  getSessionStats() {
    const { buyVolume, sellVolume, totalVolume } = this.sessionStats;
    const buyPercent = totalVolume > 0 ? (buyVolume / totalVolume * 100) : 50;
    const sellPercent = totalVolume > 0 ? (sellVolume / totalVolume * 100) : 50;
    
    return {
      ...this.sessionStats,
      buyPercent: buyPercent.toFixed(1),
      sellPercent: sellPercent.toFixed(1),
      volumeRatio: (buyVolume / (sellVolume || 1)).toFixed(2),
      startTime: this.sessionStartTime,
      sessionDuration: Date.now() - this.sessionStartTime
    };
  }

  resetSession() {
    this.sessionStartTime = Date.now();
    this.sessionStats = {
      totalVolume: 0,
      buyVolume: 0,
      sellVolume: 0,
      highPrice: -Infinity,
      lowPrice: Infinity,
      closePrice: 0,
      cumulativeDelta: 0,
      candleCount: 0
    };
  }

  pause() {
    this.paused = true;
  }

  resume() {
    this.paused = false;
  }

  clearHistory() {
    this.alertHistory = [];
  }

  getHistory() {
    return [...this.alertHistory];
  }

  setDebounceDelay(delay) {
    this.debounceDelay = delay;
  }

  createCustomAlert(name, condition, params = {}) {
    const customAlert = {
      type: AlertType.CUSTOM,
      name,
      condition,
      params,
      enabled: true
    };
    
    this.activeAlerts.set(name, customAlert);
    return customAlert;
  }

  removeCustomAlert(name) {
    this.activeAlerts.delete(name);
  }

  checkCustomAlerts(data) {
    for (const [name, alert] of this.activeAlerts) {
      if (!alert.enabled || alert.type !== AlertType.CUSTOM) continue;
      
      try {
        const result = alert.condition(data, alert.params);
        if (result) {
          const customAlert = {
            type: AlertType.CUSTOM,
            name: alert.name,
            message: typeof result === 'string' ? result : `${alert.name} triggered`,
            data: result,
            priority: alert.params.priority || 'medium'
          };
          
          this._debounce(name, () => {
            this._addToHistory(customAlert);
            this._notifyListeners(customAlert);
            this.playAlertSound('default');
          });
        }
      } catch (err) {
        console.error(`Error in custom alert ${name}:`, err);
      }
    }
  }
}

// ============ Local Storage Helpers ============

const ALERT_STORAGE_KEY = 'glora_alerts_config';
const HISTORY_STORAGE_KEY = 'glora_alert_history';
const PREFERENCES_STORAGE_KEY = 'glora_preferences';

export function saveAlertConfigs(configs) {
  try {
    localStorage.setItem(ALERT_STORAGE_KEY, JSON.stringify(configs));
  } catch (err) {
    console.error('Failed to save alert configs:', err);
  }
}

export function loadAlertConfigs() {
  try {
    const saved = localStorage.getItem(ALERT_STORAGE_KEY);
    if (saved) {
      return JSON.parse(saved);
    }
  } catch (err) {
    console.error('Failed to load alert configs:', err);
  }
  return DEFAULT_ALERT_CONFIGS;
}

export function saveAlertHistory(history) {
  try {
    localStorage.setItem(HISTORY_STORAGE_KEY, JSON.stringify(history.slice(0, 50)));
  } catch (err) {
    console.error('Failed to save alert history:', err);
  }
}

export function loadAlertHistory() {
  try {
    const saved = localStorage.getItem(HISTORY_STORAGE_KEY);
    if (saved) {
      return JSON.parse(saved);
    }
  } catch (err) {
    console.error('Failed to load alert history:', err);
  }
  return [];
}

export function savePreferences(preferences) {
  try {
    localStorage.setItem(PREFERENCES_STORAGE_KEY, JSON.stringify(preferences));
  } catch (err) {
    console.error('Failed to save preferences:', err);
  }
}

export function loadPreferences() {
  try {
    const saved = localStorage.getItem(PREFERENCES_STORAGE_KEY);
    if (saved) {
      return JSON.parse(saved);
    }
  } catch (err) {
    console.error('Failed to load preferences:', err);
  }
  return {
    soundEnabled: true,
    visualEnabled: true,
    debounceDelay: 1000,
    maxHistory: 100,
    showNotifications: true
  };
}

// ============ Export Singleton Instance ============

export const alertManager = new AlertManager();

export default alertManager;
