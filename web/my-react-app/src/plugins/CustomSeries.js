/**
 * Custom Series Plugin for TradingView
 * 
 * Provides plugin architecture for custom indicators and series:
 * - ICustomSeriesPaneView implementation
 * - Custom rendering logic
 * - User-configurable parameters
 * - Custom drawing tools
 */

// ============ Custom Series Base Class ============

/**
 * Base class for custom series that implements TradingView's ICustomSeriesPaneView
 */
export class CustomSeriesBase {
  constructor() {
    this._renderer = null;
    this._options = {};
    this._data = [];
    this._lastTagId = 0;
  }

  /**
   * Initialize the custom series
   * @param {Object} options - Series options
   */
  init(options = {}) {
    this._options = {
      ...this.getDefaultOptions(),
      ...options
    };
    this._data = [];
  }

  /**
   * Get default options - override in subclasses
   */
  getDefaultOptions() {
    return {
      color: '#2196F3',
      lineWidth: 2,
      lineStyle: 0, // 0 = solid, 1 = dotted, 2 = dashed
      priceLineVisible: true,
      lastValueVisible: true,
      priceFormat: {
        type: 'custom',
        formatter: null
      }
    };
  }

  /**
   * Get renderer for the series
   */
  renderer() {
    return this._renderer;
  }

  /**
   * Set renderer instance
   * @param {Object} renderer - Renderer instance
   */
  setRenderer(renderer) {
    this._renderer = renderer;
  }

  /**
   * Update data
   * @param {Array} data - New data
   */
  updateAllData(data) {
    this._data = data;
    if (this._renderer) {
      this._renderer.setData(this._data);
    }
  }

  /**
   * Append data point
   * @param {Object} point - Data point
   */
  appendData(point) {
    this._data.push(point);
    if (this._renderer) {
      this._renderer.updateData(this._data);
    }
  }

  /**
   * Get data at index
   * @param {number} index - Data index
   */
  dataAtIndex(index) {
    return this._data[index];
  }

  /**
   * Get current data
   */
  getData() {
    return this._data;
  }

  /**
   * Get options
   */
  getOptions() {
    return { ...this._options };
  }

  /**
   * Update option
   * @param {string} key - Option key
   * @param {*} value - Option value
   */
  updateOption(key, value) {
    this._options[key] = value;
    if (this._renderer) {
      this._renderer.updateOption(key, value);
    }
  }

  /**
   * Get price scale ID
   */
  priceScaleId() {
    return this._options.priceScaleId || 'right';
  }

  /**
   * Get legend
   * @param {number} priceScale - Price scale
   * @param {number} appliedIndex - Applied index
   * @param {Object} context - Chart context
   */
  getLegend() {
    return [];
  }
}

// ============ Custom Series Renderer ============

/**
 * Base renderer for custom series
 */
export class CustomSeriesRenderer {
  constructor() {
    this._data = [];
    this._options = {};
    this._chartContext = null;
  }

  /**
   * Initialize renderer
   * @param {Object} options - Renderer options
   */
  init(options = {}) {
    this._options = options;
    this._data = [];
  }

  /**
   * Set data
   * @param {Array} data - Data array
   */
  setData(data) {
    this._data = data || [];
  }

  /**
   * Update data
   * @param {Array} data - New data
   */
  updateData(data) {
    this._data = data || [];
  }

  /**
   * Update single option
   * @param {string} key - Option key
   * @param {*} value - Option value
   */
  updateOption(key, value) {
    this._options[key] = value;
  }

  /**
   * Draw method - override in subclasses
   * @param {Object} context - Canvas context
   * @param {Object} props - Draw properties
   */
  draw() {
    // Override in subclasses
  }

  /**
   * Get background color
   */
  get backgroundColor() {
    return 'transparent';
  }

  /**
   * Get visible range
   */
  get visibleRange() {
    return null;
  }

  /**
   * Set visible range
   * @param {Object} range - Visible range
   */
  setVisibleRange() {
    // Override in subclasses if needed
  }
}

// ============ Volume Profile Custom Series ============

/**
 * Volume Profile custom series for TradingView
 */
export class VolumeProfileSeries extends CustomSeriesBase {
  constructor() {
    super();
    this._renderer = new VolumeProfileRenderer();
  }

  getDefaultOptions() {
    return {
      ...super.getDefaultOptions(),
      color: '#2196F3',
      rowHeight: 3,
      barsSpacing: 1,
      maxRows: 20,
      priceLineVisible: false,
      lastValueVisible: false,
      volumeProfileMode: 'volume', // 'volume' or 'trades'
      showValueArea: true,
      valueAreaColor: 'rgba(33, 150, 243, 0.3)',
      valueAreaPercent: 70
    };
  }

  getLegend() {
    const data = this.getData();
    if (!data || data.length === 0) return [];
    
    const lastPoint = data[data.length - 1];
    if (!lastPoint) return [];
    
    const totalVolume = lastPoint.totalVolume || 0;
    const poc = lastPoint.poc || 0;
    const formattedVolume = this._formatVolume(totalVolume);
    
    return [
      { text: 'VP: Total', color: this._options.color, value: formattedVolume },
      { text: 'VP: POC', color: '#FF9800', value: poc.toFixed(2) }
    ];
  }

  _formatVolume(volume) {
    if (volume >= 1000000) {
      return (volume / 1000000).toFixed(2) + 'M';
    } else if (volume >= 1000) {
      return (volume / 1000).toFixed(2) + 'K';
    }
    return volume.toFixed(0);
  }
}

/**
 * Volume Profile Renderer
 */
class VolumeProfileRenderer extends CustomSeriesRenderer {
  init(options = {}) {
    super.init(options);
    this._valueAreaPercent = options.valueAreaPercent || 70;
    this._rowHeight = options.rowHeight || 3;
    this._showValueArea = options.showValueArea !== false;
    this._valueAreaColor = options.valueAreaColor || 'rgba(33, 150, 243, 0.3)';
  }

  draw(context, props) {
    const { timeScale, priceScale } = props;
    if (!timeScale || !priceScale) return;

    const data = this._data;
    if (!data || data.length === 0) return;

    const lastCandle = data[data.length - 1];
    if (!lastCandle || !lastCandle.profile) return;

    const { profile, poc, high, low } = lastCandle;
    
    const canvasWidth = context.canvas.width;
    const canvasHeight = context.canvas.height;
    
    const priceRange = high - low;
    if (priceRange <= 0) return;

    const barWidth = Math.max(20, canvasWidth * 0.15);
    
    // Find max volume for scaling
    let maxVolume = 0;
    for (const row of profile) {
      if (row.volume > maxVolume) maxVolume = row.volume;
    }

    // Calculate value area
    const sortedByVolume = [...profile].sort((a, b) => b.volume - a.volume);
    const valueAreaVolume = maxVolume * (this._valueAreaPercent / 100);
    let cumVolume = 0;
    const valueAreaPrices = new Set();
    
    for (const row of sortedByVolume) {
      if (cumVolume < valueAreaVolume) {
        valueAreaPrices.add(row.price);
        cumVolume += row.volume;
      }
    }

    // Draw volume profile bars
    context.save();
    
    for (let i = 0; i < profile.length; i++) {
      const row = profile[i];
      const normalizedPrice = (row.price - low) / priceRange;
      const y = canvasHeight - (normalizedPrice * canvasHeight);
      const barHeight = Math.max(1, (row.volume / maxVolume) * barWidth);
      
      const isValueArea = valueAreaPrices.has(row.price);
      const isPOC = row.price === poc;
      
      context.fillStyle = isPOC ? '#FF9800' : (isValueArea ? this._valueAreaColor : this._options.color || '#2196F3');
      context.globalAlpha = 0.7;
      
      context.fillRect(
        canvasWidth - barWidth - 10,
        y - this._rowHeight / 2,
        barHeight,
        this._rowHeight
      );
    }
    
    context.restore();
  }
}

// ============ Delta Chart Custom Series ============

/**
 * Cumulative Volume Delta custom series
 */
export class DeltaChartSeries extends CustomSeriesBase {
  constructor() {
    super();
    this._renderer = new DeltaChartRenderer();
  }

  getDefaultOptions() {
    return {
      ...super.getDefaultOptions(),
      color: '#4CAF50',
      negativeColor: '#F44336',
      lineWidth: 2,
      priceLineVisible: true,
      lastValueVisible: true,
      fillArea: true,
      areaOpacity: 0.3
    };
  }

  getLegend() {
    const data = this.getData();
    if (!data || data.length === 0) return [];
    
    const lastPoint = data[data.length - 1];
    if (!lastPoint) return [];
    
    const delta = lastPoint.delta || 0;
    const color = delta >= 0 ? this._options.color : this._options.negativeColor;
    
    return [
      { text: 'CVD', color: color, value: delta.toFixed(2) }
    ];
  }
}

/**
 * Delta Chart Renderer
 */
class DeltaChartRenderer extends CustomSeriesRenderer {
  init(options = {}) {
    super.init(options);
    this._negativeColor = options.negativeColor || '#F44336';
    this._fillArea = options.fillArea !== false;
    this._areaOpacity = options.areaOpacity || 0.3;
  }

  draw(context, props) {
    const { timeScale, priceScale } = props;
    if (!timeScale || !priceScale) return;

    const data = this._data;
    if (!data || data.length < 2) return;

    const canvasHeight = context.canvas.height;

    // Calculate price range
    let minDelta = Infinity;
    let maxDelta = -Infinity;
    for (const point of data) {
      const delta = point.delta || 0;
      if (delta < minDelta) minDelta = delta;
      if (delta > maxDelta) maxDelta = delta;
    }

    const deltaRange = maxDelta - minDelta || 1;
    const firstIndex = timeScale.firstIndex();
    const lastIndex = timeScale.lastIndex();

    context.save();
    context.lineWidth = this._options.lineWidth || 2;

    // Draw the delta line
    context.beginPath();
    let isFirst = true;

    for (let i = firstIndex; i <= lastIndex && i < data.length; i++) {
      const point = data[i];
      if (!point) continue;

      const x = timeScale.indexToCoordinate(i);
      const normalizedDelta = (point.delta - minDelta) / deltaRange;
      const y = canvasHeight - (normalizedDelta * canvasHeight);

      if (isFirst) {
        context.moveTo(x, y);
        isFirst = false;
      } else {
        context.lineTo(x, y);
      }
    }

    const gradient = context.createLinearGradient(0, 0, 0, canvasHeight);
    gradient.addColorStop(0, this._options.color || '#4CAF50');
    gradient.addColorStop(1, this._negativeColor);

    context.strokeStyle = gradient;
    context.stroke();

    // Fill area under curve
    if (this._fillArea) {
      context.lineTo(timeScale.indexToCoordinate(Math.min(lastIndex, data.length - 1)), canvasHeight);
      context.lineTo(timeScale.indexToCoordinate(firstIndex), canvasHeight);
      context.closePath();
      context.globalAlpha = this._areaOpacity;
      context.fillStyle = gradient;
      context.fill();
    }

    context.restore();
  }
}

// ============ Imbalance Highlight Custom Series ============

/**
 * Imbalance Highlight series for showing order flow imbalances
 */
export class ImbalanceSeries extends CustomSeriesBase {
  constructor() {
    super();
    this._renderer = new ImbalanceRenderer();
  }

  getDefaultOptions() {
    return {
      ...super.getDefaultOptions(),
      buyColor: 'rgba(76, 175, 80, 0.5)',
      sellColor: 'rgba(244, 67, 54, 0.5)',
      minRatio: 3,
      showLabels: true
    };
  }
}

/**
 * Imbalance Renderer
 */
class ImbalanceRenderer extends CustomSeriesRenderer {
  init(options = {}) {
    super.init(options);
    this._buyColor = options.buyColor || 'rgba(76, 175, 80, 0.5)';
    this._sellColor = options.sellColor || 'rgba(244, 67, 54, 0.5)';
    this._minRatio = options.minRatio || 3;
    this._showLabels = options.showLabels !== false;
  }

  draw(context, props) {
    const { timeScale, priceScale } = props;
    if (!timeScale || !priceScale) return;

    const data = this._data;
    if (!data || data.length === 0) return;

    const canvasHeight = context.canvas.height;
    const candleWidth = Math.max(1, timeScale.width() / (timeScale.lastIndex() - timeScale.firstIndex() + 1));

    context.save();

    for (let i = 0; i < data.length; i++) {
      const point = data[i];
      if (!point || !point.imbalances) continue;

      const x = timeScale.indexToCoordinate(i);
      
      for (const imb of point.imbalances) {
        if (imb.ratio < this._minRatio) continue;

        const highY = priceScale.coordinateToPrice(imb.high);
        const lowY = priceScale.coordinateToPrice(imb.low);
        
        if (highY === null || lowY === null) continue;

        const y1 = priceScale.coordinateToPrice(canvasHeight);
        const y2 = priceScale.coordinateToPrice(0);
        
        const highNorm = (imb.high - y1) / (y2 - y1);
        const lowNorm = (imb.low - y1) / (y2 - y1);
        
        const yTop = canvasHeight - (highNorm * canvasHeight);
        const yBottom = canvasHeight - (lowNorm * canvasHeight);
        
        context.fillStyle = imb.type === 'buy' ? this._buyColor : this._sellColor;
        context.fillRect(
          x - candleWidth / 2,
          yTop,
          candleWidth,
          yBottom - yTop
        );
      }
    }

    context.restore();
  }
}

// ============ Plugin Manager ============

/**
 * Plugin Manager for registering and managing custom series
 */
class PluginManager {
  constructor() {
    this._registeredSeries = new Map();
    this._registeredDrawings = new Map();
    this._customIndicators = new Map();
  }

  /**
   * Register a custom series
   * @param {string} name - Series name
   * @param {Function} seriesClass - Series class constructor
   */
  registerSeries(name, seriesClass) {
    this._registeredSeries.set(name, seriesClass);
  }

  /**
   * Get registered series
   * @param {string} name - Series name
   */
  getSeries(name) {
    return this._registeredSeries.get(name);
  }

  /**
   * Get all registered series
   */
  getAllSeries() {
    return Object.fromEntries(this._registeredSeries);
  }

  /**
   * Register a custom drawing tool
   * @param {string} name - Drawing name
   * @param {Object} drawing - Drawing configuration
   */
  registerDrawing(name, drawing) {
    this._registeredDrawings.set(name, drawing);
  }

  /**
   * Get custom drawings
   */
  getDrawings() {
    return Object.fromEntries(this._registeredDrawings);
  }

  /**
   * Add custom indicator formula
   * @param {string} name - Indicator name
   * @param {Function} formula - Formula function
   * @param {Object} params - Default parameters
   */
  addCustomIndicator(name, formula, params = {}) {
    this._customIndicators.set(name, { formula, params });
  }

  /**
   * Get custom indicator
   * @param {string} name - Indicator name
   */
  getCustomIndicator(name) {
    return this._customIndicators.get(name);
  }

  /**
   * Get all custom indicators
   */
  getAllCustomIndicators() {
    return Object.fromEntries(this._customIndicators);
  }

  /**
   * Calculate custom indicator
   * @param {string} name - Indicator name
   * @param {Array} data - Input data
   * @param {Object} params - Parameters
   */
  calculateIndicator(name, data, params = {}) {
    const indicator = this._customIndicators.get(name);
    if (!indicator) return null;
    
    try {
      return indicator.formula(data, { ...indicator.params, ...params });
    } catch (err) {
      console.error(`Error calculating indicator ${name}:`, err);
      return null;
    }
  }
}

// ============ Default Registrations ============

export const pluginManager = new PluginManager();

// Register default series
pluginManager.registerSeries('VolumeProfile', VolumeProfileSeries);
pluginManager.registerSeries('DeltaChart', DeltaChartSeries);
pluginManager.registerSeries('Imbalance', ImbalanceSeries);

// ============ Export ============

export default {
  CustomSeriesBase,
  CustomSeriesRenderer,
  VolumeProfileSeries,
  DeltaChartSeries,
  ImbalanceSeries,
  pluginManager
};
