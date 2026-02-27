// Symbol Search Web Worker
// Handles filtering large symbol lists off the main thread

// Fuzzy matching function - calculates similarity score
function fuzzyMatch(text, pattern) {
  if (!pattern) return { matches: true, score: 0 };
  
  const textLower = text.toLowerCase();
  const patternLower = pattern.toLowerCase();
  
  // Exact match gets highest score
  if (textLower === patternLower) {
    return { matches: true, score: 1000 };
  }
  
  // Starts with pattern gets high score
  if (textLower.startsWith(patternLower)) {
    return { matches: true, score: 900 + (patternLower.length / textLower.length) * 100 };
  }
  
  // Contains pattern
  if (textLower.includes(patternLower)) {
    const position = textLower.indexOf(patternLower);
    return { matches: true, score: 700 - position + (patternLower.length / textLower.length) * 100 };
  }
  
  // Fuzzy character matching
  let patternIndex = 0;
  let score = 0;
  let consecutiveMatches = 0;
  let prevMatchIndex = -1;
  
  for (let i = 0; i < textLower.length && patternIndex < patternLower.length; i++) {
    if (textLower[i] === patternLower[patternIndex]) {
      score += 10;
      
      // Bonus for consecutive matches
      if (prevMatchIndex === i - 1) {
        consecutiveMatches++;
        score += consecutiveMatches * 5;
      } else {
        consecutiveMatches = 0;
      }
      
      // Bonus for matches at word boundaries
      if (i === 0 || !textLower[i - 1].match(/[a-z0-9]/i)) {
        score += 20;
      }
      
      prevMatchIndex = i;
      patternIndex++;
    }
  }
  
  // All pattern characters must be found
  if (patternIndex === patternLower.length) {
    // Normalize score
    score = Math.min(score, 600);
    return { matches: true, score };
  }
  
  return { matches: false, score: 0 };
}

// Filter symbols based on search query
function filterSymbols(symbols, query, options = {}) {
  const {
    quoteAssets = [],
    baseAssets = [],
    minVolume = 0,
    maxResults = 100,
    fuzzy = true
  } = options;
  
  if (!query && quoteAssets.length === 0 && baseAssets.length === 0 && minVolume === 0) {
    // No filters, return top by volume
    return symbols
      .slice(0, maxResults)
      .map(s => ({ ...s, score: s.quoteVolume24h || 0 }));
  }
  
  const results = [];
  
  for (const symbol of symbols) {
    let matches = true;
    let score = 0;
    
    // Filter by quote asset
    if (quoteAssets.length > 0) {
      if (!quoteAssets.includes(symbol.quoteAsset)) {
        continue;
      }
      score += 50;
    }
    
    // Filter by base asset
    if (baseAssets.length > 0) {
      if (!baseAssets.includes(symbol.baseAsset)) {
        continue;
      }
      score += 50;
    }
    
    // Filter by minimum volume
    if (minVolume > 0) {
      if ((symbol.quoteVolume24h || 0) < minVolume) {
        continue;
      }
      score += 30;
    }
    
    // Search query matching
    if (query) {
      if (fuzzy) {
        const symbolMatch = fuzzyMatch(symbol.symbol, query);
        const baseMatch = fuzzyMatch(symbol.baseAsset, query);
        const quoteMatch = fuzzyMatch(symbol.quoteAsset, query);
        
        // Use best match score
        const bestScore = Math.max(symbolMatch.score, baseMatch.score, quoteMatch.score);
        
        if (!symbolMatch.matches && !baseMatch.matches && !quoteMatch.matches) {
          continue;
        }
        
        score += bestScore;
      } else {
        // Exact/substring matching only
        const queryLower = query.toLowerCase();
        const matchesSymbol = symbol.symbol.toLowerCase().includes(queryLower);
        const matchesBase = symbol.baseAsset.toLowerCase().includes(queryLower);
        const matchesQuote = symbol.quoteAsset.toLowerCase().includes(queryLower);
        
        if (!matchesSymbol && !matchesBase && !matchesQuote) {
          continue;
        }
        
        if (matchesSymbol) score += 500;
        if (matchesBase) score += 300;
        if (matchesQuote) score += 300;
      }
    } else {
      // No query, add volume-based score
      score += (symbol.quoteVolume24h || 0) / 1000;
    }
    
    results.push({ ...symbol, score });
  }
  
  // Sort by score descending
  results.sort((a, b) => b.score - a.score);
  
  return results.slice(0, maxResults);
}

// Message handler
self.onmessage = function(e) {
  const { type, data } = e.data;
  
  switch (type) {
    case 'filter': {
      const { symbols, query, options } = data;
      const filtered = filterSymbols(symbols, query, options);
      self.postMessage({ type: 'filtered', data: filtered });
      break;
    }
    
    case 'getTopByVolume': {
      const { symbols, quoteAsset, limit } = data;
      let filtered = symbols
        .filter(s => !quoteAsset || s.quoteAsset === quoteAsset)
        .sort((a, b) => (b.quoteVolume24h || 0) - (a.quoteVolume24h || 0))
        .slice(0, limit || 50);
      self.postMessage({ type: 'filtered', data: filtered });
      break;
    }
    
    case 'getQuoteAssets': {
      const { symbols } = data;
      const quoteAssets = [...new Set(symbols.map(s => s.quoteAsset))].sort();
      self.postMessage({ type: 'quoteAssets', data: quoteAssets });
      break;
    }
    
    default:
      console.warn('Unknown message type:', type);
  }
};
