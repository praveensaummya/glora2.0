/**
 * useIPC.js - IPC Communication Hook for C++ Backend
 * 
 * This hook provides bidirectional communication between React frontend
 * and C++ backend via WebView IPC or WebSocket.
 * 
 * Message Protocol:
 * - React -> C++: { type: "subscribe"|"unsubscribe"|"history", symbol: string, interval: string }
 * - C++ -> React: { type: "candle"|"tick"|"error"|"history", ...data }
 */

import { useState, useEffect, useCallback, useRef } from 'react';
import { 
  getWebSocket, 
  createWebSocket, 
  ConnectionState, 
  StreamBuilder 
} from '../utils/BinanceWebSocket';

// Import backend WebSocket for direct connection to C++ backend
import backendWS from '../utils/BackendWebSocket';

// Global message handlers registry
const messageHandlers = new Set();
const messageListeners = new Set();

// Historical data cache
const historyCache = new Map();
const CACHE_EXPIRY = 5 * 60 * 1000; // 5 minutes

// Generate cache key for symbol/interval
const getCacheKey = (symbol, interval) => `${symbol.toUpperCase()}_${interval}`;

// Initialize the global IPC bridge
if (typeof window !== 'undefined') {
  window.glora = window.glora || {};
  
  // Connect to backend WebSocket for direct communication
  // This works when running outside the C++ app (e.g., Vite dev server)
  backendWS.onConnectionChange((connected) => {
    console.log('[IPC] Backend WebSocket:', connected ? 'connected' : 'disconnected');
  });
  
  backendWS.onMessage((message) => {
    console.log('[IPC] Received from backend WS:', message);
    // Notify all listeners
    messageListeners.forEach(listener => {
      try {
        listener(message);
      } catch (e) {
        console.error('[IPC] Listener error:', e);
      }
    });
  });
  
  // Start the WebSocket connection
  backendWS.connect();
  
  // Handler for messages from C++
  window.glora.onMessage = (jsonString) => {
    try {
      const message = JSON.parse(jsonString);
      console.log('[IPC] Received from C++:', message);
      
      // Notify all listeners
      messageListeners.forEach(listener => {
        try {
          listener(message);
        } catch (e) {
          console.error('[IPC] Listener error:', e);
        }
      });
    } catch (e) {
      console.error('[IPC] Failed to parse message:', e);
    }
  };
  
  // Method to send messages to C++
  window.glora.send = (message) => {
    const jsonString = JSON.stringify(message);
    console.log('[IPC] Sending to C++:', message);
    
    // Check if we're in an environment that supports postMessage
    if (window.chrome && window.chrome.webview) {
      // WebView2 environment
      window.chrome.webview.postMessage(jsonString);
    } else if (window.cefQuery) {
      // CEF environment
      window.cefQuery({
        request: jsonString,
        onSuccess: () => {},
        onFailure: () => {}
      });
    } else if (backendWS.isConnected) {
      // Use backend WebSocket
      backendWS.send(message);
    } else {
      // Fallback: try to find the handler
      console.log('[IPC] No native IPC available, message queued');
    }
  };
}

/**
 * Main IPC hook for sending/receiving messages
 */
export function useIPC() {
  const [isConnected, setIsConnected] = useState(false);
  const [lastMessage, setLastMessage] = useState(null);
  const pendingCallbacks = useRef(new Map());
  const messageIdCounter = useRef(0);

  useEffect(() => {
    // Mark as connected once the hook is initialized
    // In a real implementation, we'd wait for the C++ side to acknowledge
    const timer = setTimeout(() => {
      setIsConnected(true);
    }, 100);

    // Listen for messages
    const listener = (message) => {
      setLastMessage(message);
      
      // Handle response callbacks
      if (message.requestId && pendingCallbacks.current.has(message.requestId)) {
        const callback = pendingCallbacks.current.get(message.requestId);
        pendingCallbacks.current.delete(message.requestId);
        if (callback) callback(message);
      }
    };
    
    messageListeners.add(listener);

    return () => {
      clearTimeout(timer);
      messageListeners.delete(listener);
      pendingCallbacks.current.clear();
    };
  }, []);

  /**
   * Send a message to C++ backend
   */
  const sendMessage = useCallback((message) => {
    const messageWithId = {
      ...message,
      id: ++messageIdCounter.current,
      timestamp: Date.now()
    };
    
    // Try native IPC first
    if (window.glora && window.glora.send) {
      window.glora.send(messageWithId);
    }
    
    // Also dispatch a custom event for internal components
    window.dispatchEvent(new CustomEvent('glora-ipc-send', {
      detail: messageWithId
    }));
    
    return messageWithId.id;
  }, []);

  /**
   * Send a message and wait for response
   */
  const sendAndWait = useCallback((message, timeout = 5000) => {
    return new Promise((resolve, reject) => {
      const requestId = ++messageIdCounter.current;
      const messageWithId = { ...message, requestId };
      
      const timer = setTimeout(() => {
        pendingCallbacks.current.delete(requestId);
        reject(new Error('IPC request timeout'));
      }, timeout);
      
      pendingCallbacks.current.set(requestId, (response) => {
        clearTimeout(timer);
        resolve(response);
      });
      
      sendMessage(messageWithId);
    });
  }, [sendMessage]);

  return {
    isConnected,
    lastMessage,
    sendMessage,
    sendAndWait,
    addMessageHandler: (handler) => {
      messageHandlers.add(handler);
      return () => messageHandlers.delete(handler);
    }
  };
}

/**
 * Market Data Hook - Subscribe to real-time market data
 * 
 * @param {string} symbol - Trading symbol (e.g., "BTCUSDT")
 * @param {string} interval - Time interval (e.g., "1m", "5m", "1h")
 */
export function useMarketData(symbol = 'BTCUSDT', interval = '1m') {
  const [candles, setCandles] = useState([]);
  const [latestCandle, setLatestCandle] = useState(null);
  const [isSubscribed, setIsSubscribed] = useState(false);
  const [error, setError] = useState(null);
  const { sendMessage, isConnected } = useIPC();

  // Handle incoming messages
  useEffect(() => {
    const handleMessage = (message) => {
      if (message.type === 'candle') {
        const candle = {
          time: message.time,
          open: message.open,
          high: message.high,
          low: message.low,
          close: message.close,
        };
        
        setLatestCandle(candle);
        
        setCandles(prev => {
          // Check if this is an update to the last candle or a new one
          if (prev.length > 0 && prev[prev.length - 1].time === candle.time) {
            // Update existing candle
            const updated = [...prev];
            updated[updated.length - 1] = candle;
            return updated;
          } else if (candle.time > (prev[prev.length - 1]?.time || 0)) {
            // Add new candle
            return [...prev, candle].slice(-500); // Keep last 500 candles
          }
          return prev;
        });
      } else if (message.type === 'error') {
        setError(message.errorMessage);
      } else if (message.type === 'history') {
        // Received historical data
        const historicalCandles = message.candles.map(c => ({
          time: c.time,
          open: c.open,
          high: c.high,
          low: c.low,
          close: c.close,
          volume: c.volume,
          footprint: c.footprint
        }));
        
        // Cache the historical data
        const cacheKey = getCacheKey(symbol, interval);
        historyCache.set(cacheKey, {
          data: historicalCandles,
          timestamp: Date.now()
        });
        
        setCandles(historicalCandles);
      } else if (message.type === 'footprint') {
        // Handle footprint response
        console.log('[IPC] Received footprint data:', message.profile);
      } else if (message.type === 'tick') {
        // Handle real-time tick
        console.log('[IPC] Received tick:', message);
      } else if (message.type === 'subscribed') {
        console.log('[IPC] Subscribed to:', message.symbol);
      } else if (message.type === 'config') {
        console.log('[IPC] Config updated:', message);
      }
    };
    
    messageListeners.add(handleMessage);
    return () => messageListeners.delete(handleMessage);
  }, []);

  /**
   * Request historical data with caching
   * @param {number} days - Number of days to fetch (1-30, default 7)
   * @param {number} limit - Maximum number of candles
   */
  const requestHistory = useCallback((days = 7, limit = 500) => {
    const cacheKey = getCacheKey(symbol, interval);
    const cached = historyCache.get(cacheKey);
    
    // Return cached data if still valid
    if (cached && Date.now() - cached.timestamp < CACHE_EXPIRY) {
      console.log(`[IPC] Using cached data for ${symbol} ${interval}`);
      setCandles(cached.data);
      return cached.data;
    }
    
    // Request new data from backend
    const message = {
      type: 'getHistory',
      symbol: symbol.toUpperCase(),
      interval: interval,
      days: days, // 5-7 days configurable
      limit: limit
    };
    
    sendMessage(message);
    console.log(`[IPC] Requesting ${days} days of historical data for ${symbol} ${interval}`);
    return null;
  }, [symbol, interval, sendMessage]);

  /**
   * Request historical data for a specific time range
   * @param {number} startTime - Start timestamp in milliseconds
   * @param {number} endTime - End timestamp in milliseconds
   */
  const requestHistoryRange = useCallback((startTime, endTime) => {
    const message = {
      type: 'getHistory',
      symbol: symbol.toUpperCase(),
      interval: interval,
      startTime: startTime,
      endTime: endTime
    };
    
    sendMessage(message);
    console.log(`[IPC] Requesting historical data for ${symbol} from ${startTime} to ${endTime}`);
    return null;
  }, [symbol, interval, sendMessage]);

  /**
   * Get footprint data for a specific candle
   * @param {number} candleTime - Candle start time in milliseconds
   */
  const requestFootprint = useCallback((candleTime) => {
    const message = {
      type: 'getFootprint',
      symbol: symbol.toUpperCase(),
      candleTime: candleTime
    };
    
    sendMessage(message);
    console.log(`[IPC] Requesting footprint for ${symbol} at ${candleTime}`);
    return null;
  }, [symbol, sendMessage]);

  /**
   * Clear cache for a specific symbol/interval
   */
  const clearCache = useCallback(() => {
    const cacheKey = getCacheKey(symbol, interval);
    historyCache.delete(cacheKey);
  }, [symbol, interval]);

  /**
   * Subscribe to market data
   */
  const subscribe = useCallback(() => {
    if (!symbol || !interval) return;
    
    const message = {
      type: 'subscribe',
      symbol: symbol.toUpperCase(),
      interval: interval
    };
    
    sendMessage(message);
    setIsSubscribed(true);
    console.log(`[IPC] Subscribed to ${symbol} ${interval}`);
  }, [symbol, interval, sendMessage]);

  /**
   * Unsubscribe from market data
   */
  const unsubscribe = useCallback(() => {
    const message = {
      type: 'unsubscribe',
      symbol: symbol.toUpperCase(),
      interval: interval
    };
    
    sendMessage(message);
    setIsSubscribed(false);
    console.log(`[IPC] Unsubscribed from ${symbol} ${interval}`);
  }, [symbol, interval, sendMessage]);

  /**
   * Fetch historical data (legacy method)
   */
  const fetchHistory = useCallback((limit = 100) => {
    return requestHistory(limit);
  }, [requestHistory]);

  // Request historical data on mount or when symbol/interval changes
  useEffect(() => {
    if (isConnected && symbol && interval) {
      // Request 7 days of history by default
      requestHistory(7, 500);
      // Then subscribe for live updates
      subscribe();
    }
    
    return () => {
      if (isSubscribed) {
        unsubscribe();
      }
    };
  }, [isConnected, symbol, interval]);

  return {
    candles,
    latestCandle,
    isSubscribed,
    error,
    subscribe,
    unsubscribe,
    fetchHistory,
    requestHistory,
    requestHistoryRange,
    requestFootprint,
    clearCache
  };
}

/**
 * Hook for sending commands to the C++ backend
 */
export function useIPCCommand() {
  const { sendMessage, sendAndWait } = useIPC();

  const setSymbol = useCallback((symbol) => {
    return sendMessage({ type: 'setSymbol', symbol });
  }, [sendMessage]);

  const setInterval = useCallback((interval) => {
    return sendMessage({ type: 'setInterval', interval });
  }, [sendMessage]);

  const getStatus = useCallback(async () => {
    try {
      return await sendAndWait({ type: 'getStatus' }, 3000);
    } catch (e) {
      return { status: 'unknown', error: e.message };
    }
  }, [sendAndWait]);

  const requestMarketData = useCallback((symbol, interval) => {
    return sendMessage({ 
      type: 'requestMarketData', 
      symbol, 
      interval 
    });
  }, [sendMessage]);

  return {
    setSymbol,
    setInterval,
    getStatus,
    requestMarketData
  };
}

/**
 * useBinanceWebSocket - Hook for direct Binance WebSocket connection
 * 
 * Provides real-time market data with automatic reconnection
 * and performance monitoring.
 * 
 * @param {string} symbol - Trading symbol (e.g., "BTCUSDT")
 * @param {string} interval - Time interval (e.g., "1m", "5m", "1h")
 * @param {Object} options - WebSocket options
 */
export function useBinanceWebSocket(symbol = 'BTCUSDT', interval = '1m', options = {}) {
  const [connectionState, setConnectionState] = useState(ConnectionState.DISCONNECTED);
  const [candles, setCandles] = useState([]);
  const [latestCandle, setLatestCandle] = useState(null);
  const [trades, setTrades] = useState([]);
  const [metrics, setMetrics] = useState(null);
  const wsRef = useRef(null);
  const maxTrades = useRef(1000);

  // Initialize WebSocket
  useEffect(() => {
    const ws = createWebSocket(options);
    wsRef.current = ws;

    // Set up state change handler
    const handleStateChange = (newState) => {
      setConnectionState(newState);
    };

    // Set up metrics update
    const updateMetrics = () => {
      setMetrics(ws.getMetrics());
    };

    ws.on('stateChange', handleStateChange);
    ws.on('message', updateMetrics);

    // Connect
    const streams = [
      StreamBuilder.kline(symbol, interval),
      StreamBuilder.aggTrade(symbol)
    ];
    ws.connect(streams);

    // Start metrics interval
    const metricsInterval = setInterval(updateMetrics, 1000);

    return () => {
      clearInterval(metricsInterval);
      ws.off('stateChange', handleStateChange);
      ws.off('message', updateMetrics);
      ws.disconnect();
      wsRef.current = null;
    };
  }, [symbol, interval]);

  // Handle kline (candle) updates
  useEffect(() => {
    if (!wsRef.current) return;

    const stream = StreamBuilder.kline(symbol, interval);
    
    const handleKline = (message) => {
      if (message.k) {
        const candle = {
          time: message.k.t,
          open: parseFloat(message.k.o),
          high: parseFloat(message.k.h),
          low: parseFloat(message.k.l),
          close: parseFloat(message.k.c),
          volume: parseFloat(message.k.v),
          closed: message.k.x
        };

        setLatestCandle(candle);
        
        setCandles(prev => {
          if (prev.length > 0 && prev[prev.length - 1].time === candle.time) {
            // Update existing candle
            const updated = [...prev];
            updated[updated.length - 1] = candle;
            return updated;
          } else if (candle.time > (prev[prev.length - 1]?.time || 0)) {
            // Add new candle
            return [...prev, candle].slice(-500);
          }
          return prev;
        });
      }
    };

    wsRef.current.subscribe(stream, handleKline);

    return () => {
      if (wsRef.current) {
        wsRef.current.unsubscribe(stream);
      }
    };
  }, [symbol, interval]);

  // Handle aggregate trade updates
  useEffect(() => {
    if (!wsRef.current) return;

    const stream = StreamBuilder.aggTrade(symbol);
    
    const handleTrade = (message) => {
      if (message.a) {
        const trade = {
          id: message.a,
          price: parseFloat(message.p),
          quantity: parseFloat(message.q),
          time: message.T,
          isBuyerMaker: message.m
        };

        setTrades(prev => {
          const updated = [...prev, trade];
          return updated.slice(-maxTrades.current);
        });
      }
    };

    wsRef.current.subscribe(stream, handleTrade);

    return () => {
      if (wsRef.current) {
        wsRef.current.unsubscribe(stream);
      }
    };
  }, [symbol]);

  // Manual reconnect
  const reconnect = useCallback(() => {
    if (wsRef.current) {
      wsRef.current.disconnect();
      const streams = [
        StreamBuilder.kline(symbol, interval),
        StreamBuilder.aggTrade(symbol)
      ];
      wsRef.current.connect(streams);
    }
  }, [symbol, interval]);

  // Manual disconnect
  const disconnect = useCallback(() => {
    if (wsRef.current) {
      wsRef.current.disconnect();
    }
  }, []);

  return {
    connectionState,
    isConnected: connectionState === ConnectionState.CONNECTED,
    candles,
    latestCandle,
    trades,
    metrics,
    reconnect,
    disconnect
  };
}

/**
 * usePerformanceMonitor - Hook for tracking performance metrics
 * 
 * @param {Object} ws - WebSocket instance to monitor
 */
export function usePerformanceMonitor(ws) {
  const [perfMetrics, setPerfMetrics] = useState({
    fps: 0,
    messagesPerSecond: 0,
    avgLatency: 0,
    memoryUsage: 0
  });

  const frameCount = useRef(0);
  const messageCount = useRef(0);

  useEffect(() => {
    let animationId;
    let lastTime = performance.now();

    const measureFPS = () => {
      const now = performance.now();
      const delta = now - lastTime;
      
      frameCount.current++;
      
      if (delta >= 1000) {
        const fps = Math.round((frameCount.current * 1000) / delta);
        const mps = Math.round((messageCount.current * 1000) / delta);
        
        // Get memory usage if available
        let memoryUsage = 0;
        if (performance.memory) {
          memoryUsage = Math.round(performance.memory.usedJSHeapSize / 1024 / 1024);
        }
        
        setPerfMetrics({
          fps,
          messagesPerSecond: mps,
          avgLatency: ws?.getMetrics?.()?.avgLatency || 0,
          memoryUsage
        });
        
        frameCount.current = 0;
        messageCount.current = 0;
        lastTime = now;
      }
      
      animationId = requestAnimationFrame(measureFPS);
    };

    animationId = requestAnimationFrame(measureFPS);

    return () => {
      cancelAnimationFrame(animationId);
    };
  }, [ws]);

  const recordMessage = useCallback(() => {
    messageCount.current++;
  }, []);

  return {
    ...perfMetrics,
    recordMessage
  };
}

export default useIPC;
