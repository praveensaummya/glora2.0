/**
 * Order Flow & Volume Analysis Utilities
 * 
 * Provides calculation functions for:
 * - Footprint charts (bid/ask volume at each price level)
 * - POC (Point of Control) - highest volume price
 * - Value Area (VA) - price range containing specified % of volume
 * - Imbalance Detection - buying/selling pressure anomalies
 */

// ============ Footprint Chart Calculations ============

/**
 * Calculate footprint data from candles
 * Aggregates volume by price level, separating buy and sell volume
 * 
 * @param {Array} candles - Array of {time, open, high, low, close, volume} objects
 * @param {Object} tickData - Optional tick data with buy/sell info {price, volume, isBuyerMaker}
 * @returns {Object} - Footprint data with bid/ask volumes per price level
 */
export function calculateFootprint(candles, tickData = null) {
  if (!candles || candles.length === 0) return [];

  const footprintByCandle = [];

  for (const candle of candles) {
    const { time, high, low, close, volume } = candle;
    
    // Create price levels for this candle
    const priceLevels = {};
    
    // Determine price step based on candle range
    const range = high - low;
    const priceStep = range > 0 ? range / 10 : 0.01;
    
    // Generate price levels within candle range
    let currentPrice = low;
    while (currentPrice <= high) {
      const priceKey = Number(currentPrice.toFixed(2));
      priceLevels[priceKey] = {
        price: priceKey,
        bidVolume: 0,
        askVolume: 0,
        delta: 0,
        totalVolume: 0
      };
      currentPrice += priceStep;
    }

    // If we have tick data, distribute it to price levels
    if (tickData && tickData.length > 0) {
      const candleTicks = tickData.filter(t => 
        t.time >= time && t.time < time + (candles[1]?.time - candles[0]?.time || 60000)
      );
      
      for (const tick of candleTicks) {
        const priceKey = Number(tick.price.toFixed(2));
        if (priceLevels[priceKey]) {
          if (tick.isBuyerMaker) {
            // Seller initiated - adds to ask volume
            priceLevels[priceKey].askVolume += tick.volume;
          } else {
            // Buyer initiated - adds to bid volume
            priceLevels[priceKey].bidVolume += tick.volume;
          }
          priceLevels[priceKey].totalVolume += tick.volume;
          priceLevels[priceKey].delta = priceLevels[priceKey].bidVolume - priceLevels[priceKey].askVolume;
        }
      }
    } else {
      // Fallback: Estimate based on close vs open
      const isBullish = close > open;
      const volumePerLevel = volume / Object.keys(priceLevels).length;
      
      for (const key in priceLevels) {
        const level = priceLevels[key];
        const pricePosition = (level.price - low) / range;
        
        if (isBullish) {
          // In bullish candles, more buying at lower prices
          level.bidVolume = volumePerLevel * (1 - pricePosition * 0.3);
          level.askVolume = volumePerLevel * (0.3 + pricePosition * 0.4);
        } else {
          // In bearish candles, more selling at higher prices
          level.bidVolume = volumePerLevel * (0.3 + pricePosition * 0.4);
          level.askVolume = volumePerLevel * (1 - pricePosition * 0.3);
        }
        
        level.totalVolume = level.bidVolume + level.askVolume;
        level.delta = level.bidVolume - level.askVolume;
      }
    }

    footprintByCandle.push({
      time,
      open,
      high,
      low,
      close,
      priceLevels,
      totalVolume: volume
    });
  }

  return footprintByCandle;
}

/**
 * Calculate volume profile across all candles
 * @param {Array} footprintData - Output from calculateFootprint
 * @returns {Object} - Volume profile by price level
 */
export function calculateVolumeProfile(footprintData) {
  const profile = {};

  for (const candle of footprintData) {
    for (const [priceKey, level] of Object.entries(candle.priceLevels)) {
      if (!profile[priceKey]) {
        profile[priceKey] = {
          price: level.price,
          totalVolume: 0,
          buyVolume: 0,
          sellVolume: 0,
          delta: 0,
          timeSpent: 0
        };
      }

      profile[priceKey].totalVolume += level.totalVolume;
      profile[priceKey].buyVolume += level.bidVolume;
      profile[priceKey].sellVolume += level.askVolume;
      profile[priceKey].delta += level.delta;
      profile[priceKey].timeSpent += 1;
    }
  }

  return profile;
}

// ============ POC (Point of Control) Calculations ============

/**
 * Find the POC (Point of Control) - price level with highest volume
 * 
 * @param {Object} volumeProfile - Output from calculateVolumeProfile
 * @returns {Object} - { price, volume, type } or null if not found
 */
export function findPOC(volumeProfile) {
  if (!volumeProfile || Object.keys(volumeProfile).length === 0) {
    return null;
  }

  let maxVolume = 0;
  let pocPrice = null;

  for (const data of Object.values(volumeProfile)) {
    if (data.totalVolume > maxVolume) {
      maxVolume = data.totalVolume;
      pocPrice = data.price;
    }
  }

  return {
    price: pocPrice,
    volume: maxVolume,
    type: 'poc'
  };
}

/**
 * Find multiple POC levels (for profile analysis)
 * 
 * @param {Object} volumeProfile - Output from calculateVolumeProfile
 * @param {number} count - Number of POC levels to find (default 3)
 * @returns {Array} - Array of { price, volume, type } sorted by volume descending
 */
export function findMultiplePOCs(volumeProfile, count = 3) {
  if (!volumeProfile || Object.keys(volumeProfile).length === 0) {
    return [];
  }

  const sortedLevels = Object.values(volumeProfile)
    .sort((a, b) => b.totalVolume - a.totalVolume);

  return sortedLevels.slice(0, count).map(level => ({
    price: level.price,
    volume: level.totalVolume,
    type: level.delta > 0 ? 'buyPOC' : 'sellPOC'
  }));
}

// ============ Value Area Calculations ============

/**
 * Calculate Value Area (VA) - price range containing specified % of volume
 * 
 * @param {Object} volumeProfile - Output from calculateVolumeProfile
 * @param {number} percent - Percentage of volume to include (default 70)
 * @returns {Object} - { low, high, poc, volume, percent }
 */
export function calculateValueArea(volumeProfile, percent = 70) {
  if (!volumeProfile || Object.keys(volumeProfile).length === 0) {
    return null;
  }

  // Get total volume
  let totalVolume = 0;
  for (const data of Object.values(volumeProfile)) {
    totalVolume += data.totalVolume;
  }

  if (totalVolume === 0) return null;

  // Sort prices ascending
  const sortedPrices = Object.values(volumeProfile).sort((a, b) => a.price - b.price);
  
  // Find POC
  const poc = findPOC(volumeProfile);
  if (!poc) return null;

  // Start from POC and expand outward
  const targetVolume = totalVolume * (percent / 100);
  
  let currentVolume = poc.volume;
  let low = poc.price;
  let high = poc.price;
  
  // Create index map for quick lookup
  const priceIndexMap = {};
  sortedPrices.forEach((p, i) => priceIndexMap[p.price] = i);
  
  const pocIndex = priceIndexMap[poc.price];
  
  // Expand both directions simultaneously
  let leftIndex = pocIndex - 1;
  let rightIndex = pocIndex + 1;
  
  while (currentVolume < targetVolume && (leftIndex >= 0 || rightIndex < sortedPrices.length)) {
    const leftVol = leftIndex >= 0 ? sortedPrices[leftIndex].totalVolume : 0;
    const rightVol = rightIndex < sortedPrices.length ? sortedPrices[rightIndex].totalVolume : 0;
    
    if (leftVol >= rightVol) {
      currentVolume += leftVol;
      low = sortedPrices[leftIndex].price;
      leftIndex--;
    } else {
      currentVolume += rightVol;
      high = sortedPrices[rightIndex].price;
      rightIndex++;
    }
  }

  return {
    low,
    high,
    poc: poc.price,
    volume: currentVolume,
    totalVolume,
    percent: (currentVolume / totalVolume) * 100
  };
}

/**
 * Calculate Value Area High (VAH)
 * @param {Object} volumeProfile - Output from calculateVolumeProfile
 * @param {number} percent - Percentage of volume (default 70)
 * @returns {number} - VAH price level
 */
export function calculateVAH(volumeProfile, percent = 70) {
  const va = calculateValueArea(volumeProfile, percent);
  return va ? va.high : null;
}

/**
 * Calculate Value Area Low (VAL)
 * @param {Object} volumeProfile - Output from calculateVolumeProfile
 * @param {number} percent - Percentage of volume (default 70)
 * @returns {number} - VAL price level
 */
export function calculateVAL(volumeProfile, percent = 70) {
  const va = calculateValueArea(volumeProfile, percent);
  return va ? va.low : null;
}

// ============ Imbalance Detection ============

/**
 * Detect imbalances in the order flow
 * Compares buy volume at price N vs sell volume at price N-1
 * 
 * @param {Array} footprintData - Output from calculateFootprint
 * @param {number} ratioThreshold - Minimum ratio to flag as imbalance (default 3:1)
 * @returns {Array} - Array of imbalance objects
 */
export function detectImbalances(footprintData, ratioThreshold = 3) {
  if (!footprintData || footprintData.length === 0) return [];

  const imbalances = [];

  for (let i = 1; i < footprintData.length; i++) {
    const currentCandle = footprintData[i];
    const previousCandle = footprintData[i - 1];

    // Get sorted price levels
    const currentPrices = Object.values(currentCandle.priceLevels)
      .sort((a, b) => a.price - b.price);
    
    const previousPrices = Object.values(previousCandle.priceLevels)
      .sort((a, b) => a.price - b.price);

    // Check each price level for imbalances
    for (const currentLevel of currentPrices) {
      // Find corresponding level in previous candle
      const prevLevel = previousPrices.find(p => 
        Math.abs(p.price - currentLevel.price) < 0.5
      );

      if (prevLevel) {
        // Buy Imbalance: More buying at this level than selling at level below
        if (currentLevel.bidVolume > 0 && prevLevel.askVolume > 0) {
          const ratio = currentLevel.bidVolume / prevLevel.askVolume;
          
          if (ratio >= ratioThreshold) {
            imbalances.push({
              time: currentCandle.time,
              price: currentLevel.price,
              type: 'buy',
              ratio,
              volume: currentLevel.bidVolume,
              oppositeVolume: prevLevel.askVolume,
              strength: ratio >= 5 ? 'strong' : ratio >= 4 ? 'moderate' : 'weak'
            });
          }
        }

        // Sell Imbalance: More selling at this level than buying at level below
        if (currentLevel.askVolume > 0 && prevLevel.bidVolume > 0) {
          const ratio = currentLevel.askVolume / prevLevel.bidVolume;
          
          if (ratio >= ratioThreshold) {
            imbalances.push({
              time: currentCandle.time,
              price: currentLevel.price,
              type: 'sell',
              ratio,
              volume: currentLevel.askVolume,
              oppositeVolume: prevLevel.bidVolume,
              strength: ratio >= 5 ? 'strong' : ratio >= 4 ? 'moderate' : 'weak'
            });
          }
        }
      }
    }
  }

  return imbalances;
}

/**
 * Detect order block imbalances (larger timeframe absorption)
 * 
 * @param {Array} footprintData - Output from calculateFootprint
 * @param {number} absorptionThreshold - Minimum total volume for absorption (default 10)
 * @returns {Array} - Array of order block objects
 */
export function detectOrderBlocks(footprintData, absorptionThreshold = 10) {
  if (!footprintData || footprintData.length < 2) return [];

  const orderBlocks = [];

  for (let i = 1; i < footprintData.length; i++) {
    const currentCandle = footprintData[i];
    const previousCandle = footprintData[i - 1];

    // Check if previous candle had significant imbalance
    const prevPrices = Object.values(previousCandle.priceLevels);
    let totalBidVol = 0;
    let totalAskVol = 0;

    for (const level of prevPrices) {
      totalBidVol += level.bidVolume;
      totalAskVol += level.askVolume;
    }

    // Strong buy order block (significant selling absorbed)
    if (totalAskVol > absorptionThreshold) {
      const absorptionRatio = totalBidVol / totalAskVol;
      if (absorptionRatio > 0.6) {
        orderBlocks.push({
          time: previousCandle.time,
          type: 'buy',
          price: previousCandle.close,
          absorbedVolume: totalAskVol,
          absorptionRatio,
          target: currentCandle.high
        });
      }
    }

    // Strong sell order block (significant buying absorbed)
    if (totalBidVol > absorptionThreshold) {
      const absorptionRatio = totalAskVol / totalBidVol;
      if (absorptionRatio > 0.6) {
        orderBlocks.push({
          time: previousCandle.time,
          type: 'sell',
          price: previousCandle.close,
          absorbedVolume: totalBidVol,
          absorptionRatio,
          target: currentCandle.low
        });
      }
    }
  }

  return orderBlocks;
}

// ============ Delta Calculations ============

/**
 * Calculate cumulative delta for a series of candles
 * 
 * @param {Array} footprintData - Output from calculateFootprint
 * @returns {Array} - Array of {time, delta, cumulativeDelta}
 */
export function calculateDelta(footprintData) {
  if (!footprintData || footprintData.length === 0) return [];

  const result = [];
  let cumulativeDelta = 0;

  for (const candle of footprintData) {
    let candleDelta = 0;
    
    for (const level of Object.values(candle.priceLevels)) {
      candleDelta += level.delta;
    }

    cumulativeDelta += candleDelta;

    result.push({
      time: candle.time,
      open: candle.open,
      high: candle.high,
      low: candle.low,
      close: candle.close,
      delta: candleDelta,
      cumulativeDelta
    });
  }

  return result;
}

/**
 * Calculate buying/selling pressure ratio
 * 
 * @param {Object} volumeProfile - Output from calculateVolumeProfile
 * @returns {Object} - { ratio, trend, interpretation }
 */
export function calculateBuyingPressure(volumeProfile) {
  if (!volumeProfile || Object.keys(volumeProfile).length === 0) {
    return { ratio: 1, trend: 'neutral', interpretation: 'No data' };
  }

  let totalBuy = 0;
  let totalSell = 0;

  for (const data of Object.values(volumeProfile)) {
    totalBuy += data.buyVolume;
    totalSell += data.sellVolume;
  }

  if (totalSell === 0) {
    return { ratio: 999, trend: 'extreme_buy', interpretation: 'Extreme buying pressure' };
  }

  const ratio = totalBuy / totalSell;
  
  let trend, interpretation;
  if (ratio > 1.5) {
    trend = 'buy';
    interpretation = 'Strong buying pressure';
  } else if (ratio > 1.1) {
    trend = 'slight_buy';
    interpretation = 'Slight buying pressure';
  } else if (ratio < 0.67) {
    trend = 'sell';
    interpretation = 'Strong selling pressure';
  } else if (ratio < 0.9) {
    trend = 'slight_sell';
    interpretation = 'Slight selling pressure';
  } else {
    trend = 'neutral';
    interpretation = 'Balanced buying/selling';
  }

  return { ratio, trend, interpretation };
}

// ============ Helper Functions ============

/**
 * Format volume for display
 * @param {number} volume - Volume value
 * @returns {string} - Formatted volume string
 */
export function formatVolume(volume) {
  if (volume >= 1000000) {
    return (volume / 1000000).toFixed(2) + 'M';
  } else if (volume >= 1000) {
    return (volume / 1000).toFixed(2) + 'K';
  }
  return volume.toFixed(2);
}

/**
 * Generate mock tick data for demonstration
 * This simulates the granular trade data needed for accurate footprint
 * 
 * @param {Array} candles - Candle data
 * @returns {Array} - Mock tick data
 */
export function generateMockTickData(candles) {
  const ticks = [];
  
  for (const candle of candles) {
    const tickCount = Math.floor(Math.random() * 50) + 20;
    const priceRange = candle.high - candle.low;
    
    for (let i = 0; i < tickCount; i++) {
      ticks.push({
        time: candle.time + (i * 1000),
        price: candle.low + (Math.random() * priceRange),
        volume: Math.random() * 2,
        isBuyerMaker: Math.random() > 0.5
      });
    }
  }
  
  return ticks;
}

/**
 * Color utilities for order flow visualization
 */
export const ORDER_FLOW_COLORS = {
  buyDelta: '#22c55e',      // Green for buying
  sellDelta: '#ef4444',     // Red for selling
  balanced: '#6b7280',      // Gray for balanced
  poc: '#f59e0b',           // Amber for POC
  valueArea: '#3b82f6',     // Blue for value area
  imbalance: '#8b5cf6',     // Purple for imbalances
  orderBlock: '#ec4899'    // Pink for order blocks
};

/**
 * Default configuration for order flow analysis
 */
export const ORDER_FLOW_CONFIG = {
  imbalanceRatio: 3,           // Minimum ratio for imbalance detection
  valueAreaPercent: 70,         // Default VA percentage
  absorptionThreshold: 10,     // Minimum volume for order block detection
  domDepth: 20,                // Default DOM depth levels
  footprintTickSize: 0.01,     // Default price tick size
  showDelta: true,             // Show delta coloring
  showVolumeProfile: true,     // Show volume profile
  showPOC: true,               // Show POC line
  showVA: true                 // Show Value Area
};
