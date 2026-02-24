/**
 * BinanceWebSocket.js - Enhanced WebSocket Manager for Real-Time Market Data
 * 
 * Features:
 * - Exponential backoff reconnection
 * - Connection state management
 * - Heartbeat/ping-pong for connection health
 * - Message queuing during reconnection
 * - Binary message support (FlatBuffers ready)
 * - Performance monitoring
 */

import { 
  EventEmitter 
} from 'events';

// Connection states
export const ConnectionState = {
  DISCONNECTED: 'disconnected',
  CONNECTING: 'connecting',
  CONNECTED: 'connected',
  RECONNECTING: 'reconnecting',
  ERROR: 'error'
};

// Default configuration
const DEFAULT_CONFIG = {
  // Binance WebSocket endpoints
  baseUrl: 'wss://stream.binance.com:9443/ws',
  
  // Reconnection settings
  maxReconnectAttempts: 10,
  initialReconnectDelay: 1000,
  maxReconnectDelay: 30000,
  reconnectBackoffMultiplier: 2,
  
  // Heartbeat settings
  pingInterval: 30000,
  pingTimeout: 5000,
  
  // Message queue settings
  maxQueueSize: 1000,
  queueFlushInterval: 100,
  
  // Performance settings
  enableMetrics: true,
  messageBatchSize: 10
};

class BinanceWebSocket extends EventEmitter {
  constructor(config = {}) {
    super();
    
    this.config = { ...DEFAULT_CONFIG, ...config };
    this.ws = null;
    this.state = ConnectionState.DISCONNECTED;
    
    // Subscriptions
    this.subscriptions = new Map(); // stream -> callback
    
    // Message queue for offline buffering
    this.messageQueue = [];
    this.isProcessingQueue = false;
    
    // Reconnection state
    this.reconnectAttempts = 0;
    this.reconnectTimer = null;
    this.shouldReconnect = true;
    
    // Heartbeat
    this.pingTimer = null;
    this.lastPongTime = 0;
    
    // Performance metrics
    this.metrics = {
      messagesReceived: 0,
      messagesSent: 0,
      errors: 0,
      reconnects: 0,
      lastMessageTime: 0,
      avgLatency: 0,
      connectionStartTime: 0
    };
    
    // Latency tracking
    this.latencySamples = [];
    this.maxLatencySamples = 100;
    
    // Binary parser (will be set if FlatBuffers enabled)
    this.binaryParser = null;
  }

  /**
   * Connect to Binance WebSocket
   * @param {string[]} streams - Array of stream names (e.g., ['btcusdt@aggTrade'])
   */
  connect(streams = []) {
    if (this.state === ConnectionState.CONNECTED || 
        this.state === ConnectionState.CONNECTING) {
      console.warn('[WS] Already connected or connecting');
      return;
    }

    this.shouldReconnect = true;
    this.setState(ConnectionState.CONNECTING);
    
    // Build URL with streams
    const streamParam = streams.map(s => encodeURIComponent(s)).join('/');
    const url = streams.length > 0 
      ? `${this.config.baseUrl}/${streamParam}`
      : this.config.baseUrl;
    
    console.log('[WS] Connecting to:', url);
    
    try {
      this.ws = new WebSocket(url);
      
      // Set up event handlers
      this.ws.onopen = () => this.handleOpen();
      this.ws.onclose = (event) => this.handleClose(event);
      this.ws.onerror = (error) => this.handleError(error);
      this.ws.onmessage = (event) => this.handleMessage(event);
      
    } catch (error) {
      console.error('[WS] Connection error:', error);
      this.handleError(error);
    }
  }

  /**
   * Disconnect from WebSocket
   */
  disconnect() {
    this.shouldReconnect = false;
    this.clearTimers();
    
    if (this.ws) {
      this.ws.onopen = null;
      this.ws.onclose = null;
      this.ws.onerror = null;
      this.ws.onmessage = null;
      
      if (this.ws.readyState === WebSocket.OPEN || 
          this.ws.readyState === WebSocket.CONNECTING) {
        this.ws.close(1000, 'Client disconnect');
      }
      
      this.ws = null;
    }
    
    this.setState(ConnectionState.DISCONNECTED);
    this.subscriptions.clear();
  }

  /**
   * Subscribe to a stream
   * @param {string} stream - Stream name (e.g., 'btcusdt@aggTrade')
   * @param {Function} callback - Callback for messages
   */
  subscribe(stream, callback) {
    const streamLower = stream.toLowerCase();
    
    if (this.subscriptions.has(streamLower)) {
      console.warn(`[WS] Already subscribed to ${stream}`);
      return;
    }
    
    this.subscriptions.set(streamLower, callback);
    
    // Send subscription message if connected
    if (this.state === ConnectionState.CONNECTED) {
      this.send({
        method: 'SUBSCRIBE',
        params: [streamLower],
        id: Date.now()
      });
    }
  }

  /**
   * Unsubscribe from a stream
   * @param {string} stream - Stream name
   */
  unsubscribe(stream) {
    const streamLower = stream.toLowerCase();
    
    if (!this.subscriptions.has(streamLower)) {
      return;
    }
    
    this.subscriptions.delete(streamLower);
    
    if (this.state === ConnectionState.CONNECTED) {
      this.send({
        method: 'UNSUBSCRIBE',
        params: [streamLower],
        id: Date.now()
      });
    }
  }

  /**
   * Send message through WebSocket
   * @param {Object} message - Message object
   */
  send(message) {
    if (this.state !== ConnectionState.CONNECTED || !this.ws) {
      // Queue message if not connected
      this.queueMessage(message);
      return false;
    }
    
    try {
      this.ws.send(JSON.stringify(message));
      this.metrics.messagesSent++;
      return true;
    } catch (error) {
      console.error('[WS] Send error:', error);
      this.metrics.errors++;
      this.queueMessage(message);
      return false;
    }
  }

  /**
   * Queue message for later delivery
   */
  queueMessage(message) {
    if (this.messageQueue.length >= this.config.maxQueueSize) {
      // Remove oldest message
      this.messageQueue.shift();
    }
    this.messageQueue.push({
      data: message,
      timestamp: Date.now()
    });
  }

  /**
   * Process queued messages
   */
  async processQueue() {
    if (this.isProcessingQueue || this.messageQueue.length === 0) {
      return;
    }
    
    this.isProcessingQueue = true;
    
    try {
      const messages = [...this.messageQueue];
      this.messageQueue = [];
      
      for (const { data } of messages) {
        if (this.state === ConnectionState.CONNECTED && this.ws) {
          this.ws.send(JSON.stringify(data));
          this.metrics.messagesSent++;
        }
      }
    } catch (error) {
      console.error('[WS] Queue processing error:', error);
    } finally {
      this.isProcessingQueue = false;
    }
  }

  /**
   * Handle WebSocket open
   */
  handleOpen() {
    console.log('[WS] Connected');
    this.setState(ConnectionState.CONNECTED);
    this.metrics.connectionStartTime = Date.now();
    this.reconnectAttempts = 0;
    
    // Re-subscribe to all streams
    if (this.subscriptions.size > 0) {
      const streams = Array.from(this.subscriptions.keys());
      this.send({
        method: 'SUBSCRIBE',
        params: streams,
        id: Date.now()
      });
    }
    
    // Process queued messages
    this.processQueue();
    
    // Start heartbeat
    this.startHeartbeat();
    
    this.emit('open');
  }

  /**
   * Handle WebSocket close
   */
  handleClose(event) {
    console.log('[WS] Disconnected:', event.code, event.reason);
    this.clearTimers();
    
    if (this.shouldReconnect) {
      this.scheduleReconnect();
    } else {
      this.setState(ConnectionState.DISCONNECTED);
    }
    
    this.emit('close', event);
  }

  /**
   * Handle WebSocket error
   */
  handleError(error) {
    console.error('[WS] Error:', error);
    this.metrics.errors++;
    this.setState(ConnectionState.ERROR);
    this.emit('error', error);
  }

  /**
   * Handle incoming message
   */
  handleMessage(event) {
    const startTime = performance.now();
    
    try {
      // Handle binary messages
      if (event.data instanceof ArrayBuffer || event.data instanceof Blob) {
        this.handleBinaryMessage(event.data);
        return;
      }
      
      const message = JSON.parse(event.data);
      
      // Track latency for ping/pong
      if (message.e === 'ping') {
        this.send({ method: 'pong', timestamp: message.timestamp });
        return;
      }
      
      // Update metrics
      this.metrics.messagesReceived++;
      this.metrics.lastMessageTime = Date.now();
      
      // Calculate processing latency
      const latency = performance.now() - startTime;
      this.trackLatency(latency);
      
      // Determine stream and notify subscribers
      const stream = message.stream || this.inferStream(message);
      if (stream && this.subscriptions.has(stream)) {
        const callback = this.subscriptions.get(stream);
        try {
          callback(message, latency);
        } catch (error) {
          console.error('[WS] Callback error:', error);
        }
      }
      
      // Also emit for general listeners
      this.emit('message', message, latency);
      
    } catch (error) {
      console.error('[WS] Message parse error:', error);
      this.metrics.errors++;
    }
  }

  /**
   * Handle binary message
   */
  handleBinaryMessage(data) {
    if (this.binaryParser) {
      const message = this.binaryParser(data);
      this.metrics.messagesReceived++;
      this.emit('binaryMessage', message);
    } else {
      console.warn('[WS] Binary parser not configured');
    }
  }

  /**
   * Infer stream name from message
   */
  inferStream(message) {
    // Try to infer from message properties
    if (message.s) {
      const symbol = message.s.toLowerCase();
      if (message.e === 'aggTrade') return `${symbol}@aggTrade`;
      if (message.e === '24hrMiniTicker') return `${symbol}@miniTicker`;
      if (message.e === '24hrTicker') return `${symbol}@ticker`;
      if (message.k) return `${symbol}@kline_${message.k.i}`;
    }
    return null;
  }

  /**
   * Schedule reconnection with exponential backoff
   */
  scheduleReconnect() {
    if (!this.shouldReconnect || this.reconnectTimer) {
      return;
    }
    
    this.setState(ConnectionState.RECONNECTING);
    this.reconnectAttempts++;
    this.metrics.reconnects++;
    
    // Calculate delay with exponential backoff
    const delay = Math.min(
      this.config.initialReconnectDelay * 
        Math.pow(this.config.reconnectBackoffMultiplier, this.reconnectAttempts - 1),
      this.config.maxReconnectDelay
    );
    
    console.log(`[WS] Reconnecting in ${delay}ms (attempt ${this.reconnectAttempts})`);
    
    this.reconnectTimer = setTimeout(() => {
      this.reconnectTimer = null;
      
      if (this.shouldReconnect && this.reconnectAttempts < this.config.maxReconnectAttempts) {
        const streams = Array.from(this.subscriptions.keys());
        this.connect(streams);
      } else if (this.reconnectAttempts >= this.config.maxReconnectAttempts) {
        console.error('[WS] Max reconnection attempts reached');
        this.setState(ConnectionState.ERROR);
        this.emit('error', new Error('Max reconnection attempts reached'));
      }
    }, delay);
  }

  /**
   * Start heartbeat/ping-pong
   */
  startHeartbeat() {
    this.clearPingTimer();
    
    this.pingTimer = setInterval(() => {
      if (this.state !== ConnectionState.CONNECTED) {
        return;
      }
      
      const _pingTime = Date.now();
      
      try {
        this.ws.send(JSON.stringify({
          method: 'ping'
        }));
        
        // Set up pong timeout
        setTimeout(() => {
          const timeSincePong = Date.now() - this.lastPongTime;
          if (timeSincePong > this.config.pingTimeout) {
            console.warn('[WS] Pong timeout, reconnecting...');
            this.ws.close();
          }
        }, this.config.pingTimeout);
        
      } catch (error) {
        console.error('[WS] Ping error:', error);
      }
    }, this.config.pingInterval);
  }

  /**
   * Clear ping timer
   */
  clearPingTimer() {
    if (this.pingTimer) {
      clearInterval(this.pingTimer);
      this.pingTimer = null;
    }
  }

  /**
   * Clear all timers
   */
  clearTimers() {
    this.clearPingTimer();
    
    if (this.reconnectTimer) {
      clearTimeout(this.reconnectTimer);
      this.reconnectTimer = null;
    }
  }

  /**
   * Set connection state
   */
  setState(newState) {
    const oldState = this.state;
    this.state = newState;
    
    if (oldState !== newState) {
      console.log(`[WS] State: ${oldState} -> ${newState}`);
      this.emit('stateChange', newState, oldState);
    }
  }

  /**
   * Track message latency
   */
  trackLatency(latency) {
    this.latencySamples.push(latency);
    
    if (this.latencySamples.length > this.maxLatencySamples) {
      this.latencySamples.shift();
    }
    
    // Calculate average
    const sum = this.latencySamples.reduce((a, b) => a + b, 0);
    this.metrics.avgLatency = sum / this.latencySamples.length;
  }

  /**
   * Get performance metrics
   */
  getMetrics() {
    return {
      ...this.metrics,
      state: this.state,
      subscriptions: this.subscriptions.size,
      queueSize: this.messageQueue.length,
      avgLatency: this.metrics.avgLatency.toFixed(2)
    };
  }

  /**
   * Set binary parser (for FlatBuffers)
   */
  setBinaryParser(parser) {
    this.binaryParser = parser;
  }

  /**
   * Get connection state
   */
  getState() {
    return this.state;
  }

  /**
   * Check if connected
   */
  isConnected() {
    return this.state === ConnectionState.CONNECTED;
  }
}

// Singleton instance
let wsInstance = null;

/**
 * Get or create WebSocket instance
 */
export function getWebSocket(config) {
  if (!wsInstance) {
    wsInstance = new BinanceWebSocket(config);
  }
  return wsInstance;
}

/**
 * Create new WebSocket instance
 */
export function createWebSocket(config) {
  return new BinanceWebSocket(config);
}

/**
 * Pre-defined stream builders for Binance
 */
export const StreamBuilder = {
  aggTrade: (symbol) => `${symbol.toLowerCase()}@aggTrade`,
  trade: (symbol) => `${symbol.toLowerCase()}@trade`,
  ticker: (symbol) => `${symbol.toLowerCase()}@ticker`,
  miniTicker: (symbol) => `${symbol.toLowerCase()}@miniTicker`,
  kline: (symbol, interval) => `${symbol.toLowerCase()}@kline_${interval}`,
  depth: (symbol, levels = 20) => `${symbol.toLowerCase()}@depth${levels}`,
  depthLevel: (symbol, level = '@100ms') => `${symbol.toLowerCase()}${level}`,
  orderBook: (symbol) => `${symbol.toLowerCase()}@depth@100ms`,
  
  // Combined streams
  combine: (streams) => streams.join('/')
};

export default BinanceWebSocket;
