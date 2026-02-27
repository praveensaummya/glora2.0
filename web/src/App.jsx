import { createSignal, onMount, Show, For, onCleanup, createEffect } from 'solid-js';
import { createChart, ColorType } from 'lightweight-charts';

// Import symbol search worker
import SymbolWorker from './workers/symbolWorker.js?worker';

const SYMBOLS = ['BTCUSDT', 'ETHUSDT', 'SOLUSDT', 'BNBUSDT'];
const TIMEFRAMES = ['1m', '5m', '15m', '1h', '4h', '1D'];

// --- UI Batching: Pending updates queue for requestAnimationFrame ---
let pendingUpdates = [];
let frameCallbackId = null;
let isBatchingEnabled = true;

// Process batched updates on next animation frame
function processBatch() {
  frameCallbackId = null;
  
  if (pendingUpdates.length === 0) return;
  
  // Process all pending updates
  const updates = [...pendingUpdates];
  pendingUpdates = [];
  
  for (const update of updates) {
    update();
  }
  
  if (pendingUpdates.length > 0) {
    // Schedule next frame if more updates came in
    frameCallbackId = requestAnimationFrame(processBatch);
  }
}

// Queue an update to be processed on next animation frame
function batchUpdate(update) {
  if (!isBatchingEnabled) {
    update();
    return;
  }
  
  pendingUpdates.push(update);
  
  if (!frameCallbackId) {
    frameCallbackId = requestAnimationFrame(processBatch);
  }
}

// Force process all pending updates immediately
function flushUpdates() {
  if (frameCallbackId) {
    cancelAnimationFrame(frameCallbackId);
    frameCallbackId = null;
  }
  
  while (pendingUpdates.length > 0) {
    const updates = [...pendingUpdates];
    pendingUpdates = [];
    for (const update of updates) {
      update();
    }
  }
}

function App() {
  const [symbol, setSymbol] = createSignal('BTCUSDT');
  const [timeframe, setTimeframe] = createSignal('1m');
  const [price, setPrice] = createSignal(0);
  const [priceChange, setPriceChange] = createSignal(0);
  const [priceChangePercent, setPriceChangePercent] = createSignal(0);
  const [connectionStatus, setConnectionStatus] = createSignal('disconnected');
  const [showOrderBook, setShowOrderBook] = createSignal(true);
  const [showTradeHistory, setShowTradeHistory] = createSignal(true);
  const [statusMessage, setStatusMessage] = createSignal('');
  const [showSymbolSearch, setShowSymbolSearch] = createSignal(false);
  const [searchQuery, setSearchQuery] = createSignal('');
  const [searchResults, setSearchResults] = createSignal([]);
  const [symbols, setSymbols] = createSignal([]);
  const [quoteAssets, setQuoteAssets] = createSignal([]);
  const [selectedQuoteAsset, setSelectedQuoteAsset] = createSignal('');
  const [isSearching, setIsSearching] = createSignal(false);
  const [historyLoaded, setHistoryLoaded] = createSignal(false);  // Track if history has been loaded
  const [pendingCandleUpdates, setPendingCandleUpdates] = createSignal([]);  // Buffer for live updates before history
  
  // === Smart DOM State ===
  const [orderBookBids, setOrderBookBids] = createSignal([]);
  const [orderBookAsks, setOrderBookAsks] = createSignal([]);
  const [trades, setTrades] = createSignal([]);  // Recent trades for Smart DOM
  const [smartDOMData, setSmartDOMData] = createSignal([]);  // Aggregated Smart DOM data
  const [pocPrice, setPocPrice] = createSignal(0);  // Point of Control

  let chartContainer;
  let chart;
  let candleSeries;
  let volumeSeries;
  let ws;
  let reconnectTimer;
  let connectionStartTime = 0;  // Track connection start for 24-hour reconnection
  let symbolWorker;  // Web Worker for symbol filtering
  let debounceTimer;  // Debounce timer for search
  const MAX_CONNECTION_DURATION = 24 * 60 * 60 * 1000;  // 24 hours in ms
  const RECONNECT_DELAY = 3000;  // 3 seconds
  const DEBOUNCE_DELAY = 150;  // Debounce delay for search

  onMount(() => {
    initChart();
    connectWebSocket();
    connectBinanceWS();  // Connect to Binance for Smart DOM
    initSymbolWorker();
  });

  onCleanup(() => {
    if (ws) ws.close();
    if (binanceWs) binanceWs.close();  // Clean up Binance WebSocket
    if (reconnectTimer) clearTimeout(reconnectTimer);
    if (chart) chart.remove();
    // Flush pending batched updates
    flushUpdates();
    if (frameCallbackId) cancelAnimationFrame(frameCallbackId);
  });

  function initChart() {
    chart = createChart(chartContainer, {
      layout: { background: { type: ColorType.Solid, color: '#131722' }, textColor: '#787b86' },
      grid: { vertLines: { color: '#2a2e39' }, horzLines: { color: '#2a2e39' } },
      width: chartContainer.clientWidth,
      height: chartContainer.clientHeight,
      timeScale: { timeVisible: true, secondsVisible: false },
      rightPriceScale: { borderColor: '#2a2e39' },
    });

    candleSeries = chart.addCandlestickSeries({
      upColor: '#089981', downColor: '#f23645',
      borderUpColor: '#089981', borderDownColor: '#f23645',
      wickUpColor: '#089981', wickDownColor: '#f23645',
    });

    volumeSeries = chart.addHistogramSeries({ color: '#26a69a', priceFormat: { type: 'volume' }, priceScaleId: '' });
    volumeSeries.priceScale().applyOptions({ scaleMargins: { top: 0.8, bottom: 0 } });

    const handleResize = () => {
      if (chart && chartContainer) chart.applyOptions({ width: chartContainer.clientWidth, height: chartContainer.clientHeight });
    };
    window.addEventListener('resize', handleResize);
    onCleanup(() => window.removeEventListener('resize', handleResize));
  }

  // Initialize Symbol Search Web Worker
  function initSymbolWorker() {
    try {
      symbolWorker = new Worker(
        new URL('./workers/symbolWorker.js', import.meta.url),
        { type: 'module' }
      );
      
      symbolWorker.onmessage = (e) => {
        const { type, data } = e.data;
        if (type === 'filtered') {
          setSearchResults(data);
          setIsSearching(false);
        } else if (type === 'quoteAssets') {
          setQuoteAssets(data);
        }
      };
      
      symbolWorker.onerror = (err) => {
        console.error('Symbol Worker error:', err);
        setIsSearching(false);
      };
      
      console.log('Symbol Worker initialized');
      
      // Request exchange info from backend
      requestExchangeInfo();
      
    } catch (err) {
      console.error('Failed to initialize symbol worker:', err);
    }
  }

  // Request exchange info from backend
  function requestExchangeInfo() {
    if (ws && ws.readyState === WebSocket.OPEN) {
      sendMessage({ type: 'getExchangeInfo' });
    }
  }

  // Debounced search function
  function debouncedSearch(query) {
    clearTimeout(debounceTimer);
    setIsSearching(true);
    
    debounceTimer = setTimeout(() => {
      if (symbolWorker && symbols().length > 0) {
        symbolWorker.postMessage({
          type: 'filter',
          data: {
            symbols: symbols(),
            query: query,
            options: {
              quoteAssets: selectedQuoteAsset() ? [selectedQuoteAsset()] : [],
              maxResults: 50,
              fuzzy: true
            }
          }
        });
      } else {
        setIsSearching(false);
      }
    }, DEBOUNCE_DELAY);
  }

  // Handle search input change
  function handleSearchInput(e) {
    const query = e.target.value;
    setSearchQuery(query);
    debouncedSearch(query);
  }

  // Handle symbol selection from search
  function handleSymbolSelect(selectedSymbol) {
    handleSymbolChange(selectedSymbol);
    setShowSymbolSearch(false);
    setSearchQuery('');
  }

  // Handle quote asset filter
  function handleQuoteAssetChange(quoteAsset) {
    setSelectedQuoteAsset(quoteAsset);
    if (symbolWorker && symbols().length > 0) {
      symbolWorker.postMessage({
        type: 'filter',
        data: {
          symbols: symbols(),
          query: searchQuery(),
          options: {
            quoteAssets: quoteAsset ? [quoteAsset] : [],
            maxResults: 50,
            fuzzy: true
          }
        }
      });
    }
  }

  // === Binance WebSocket for Smart DOM ===
  let binanceWs;  // Separate WebSocket for Binance data
  
  function connectBinanceWS() {
    if (binanceWs) binanceWs.close();
    
    const sym = symbol().toLowerCase();
    binanceWs = new WebSocket(`wss://stream.binance.com:9443/stream?streams=${sym}@depth20@100ms/${sym}@aggTrade`);
    
    binanceWs.onmessage = (event) => {
      try {
        const msg = JSON.parse(event.data);
        const data = msg.data;
        
        if (data.e === 'depthUpdate') {
          // Order book update
          const bids = (data.b || []).map(([p, q]) => ({ price: parseFloat(p), qty: parseFloat(q) }));
          const asks = (data.a || []).map(([p, q]) => ({ price: parseFloat(p), qty: parseFloat(q) }));
          
          batchUpdate(() => {
            setOrderBookBids(bids);
            setOrderBookAsks(asks);
          });
        } else if (data.e === 'aggTrade') {
          // Aggregated trade - for Smart DOM
          const trade = {
            price: parseFloat(data.p),
            qty: parseFloat(data.q),
            time: data.T,
            isBuyerMaker: data.m  // true = seller initiated (aggressive sell)
          };
          
          batchUpdate(() => {
            setTrades(prev => {
              const updated = [trade, ...prev].slice(0, 100);  // Keep last 100 trades
              return updated;
            });
            
            // Update Smart DOM data
            updateSmartDOM(trade);
          });
        }
      } catch (e) {
        console.error('[Binance WS] Parse error:', e);
      }
    };
    
    binanceWs.onerror = (e) => {
      console.error('[Binance WS] Error:', e);
    };
    
    binanceWs.onclose = () => {
      console.log('[Binance WS] Disconnected, reconnecting...');
      setTimeout(connectBinanceWS, 5000);
    };
  }
  
  // === Smart DOM Data Processing ===
  // Map to track price buckets for Smart DOM
  let priceBuckets = new Map();
  
  function updateSmartDOM(trade) {
    const price = trade.price;
    const qty = trade.qty;
    const isBuy = !trade.isBuyerMaker;  // !m means buyer was aggressor
    
    // Round price to tick size (0.01 for BTCUSDT)
    const tickSize = 0.01;
    const roundedPrice = Math.round(price / tickSize) * tickSize;
    
    // Get or create bucket
    let bucket = priceBuckets.get(roundedPrice);
    if (!bucket) {
      bucket = {
        price: roundedPrice,
        restingBid: 0,
        restingAsk: 0,
        aggBuy: 0,
        aggSell: 0
      };
      priceBuckets.set(roundedPrice, bucket);
    }
    
    // Update aggressive volume
    if (isBuy) {
      bucket.aggBuy += qty;
    } else {
      bucket.aggSell += qty;
    }
    
    // Calculate delta
    bucket.delta = bucket.aggBuy - bucket.aggSell;
    
    // Find POC (price with highest total volume)
    let maxVol = 0;
    let poc = 0;
    for (const [p, b] of priceBuckets) {
      const totalVol = b.restingBid + b.restingAsk + b.aggBuy + b.aggSell;
      if (totalVol > maxVol) {
        maxVol = totalVol;
        poc = p;
      }
    }
    setPocPrice(poc);
    
    // Convert to sorted array
    const sortedPrices = [...priceBuckets.keys()].sort((a, b) => b - a);
    const domData = sortedPrices.slice(0, 25).map(p => priceBuckets.get(p));
    setSmartDOMData(domData);
  }
  
  // Update order book resting quantities in Smart DOM
  function updateRestingLiquidity() {
    const bids = orderBookBids();
    const asks = orderBookAsks();
    const tickSize = 0.01;
    
    // Update bid resting
    for (const b of bids) {
      const price = Math.round(b.price / tickSize) * tickSize;
      let bucket = priceBuckets.get(price);
      if (!bucket) {
        bucket = { price, restingBid: 0, restingAsk: 0, aggBuy: 0, aggSell: 0, delta: 0 };
        priceBuckets.set(price, bucket);
      }
      bucket.restingBid = b.qty;
    }
    
    // Update ask resting
    for (const a of asks) {
      const price = Math.round(a.price / tickSize) * tickSize;
      let bucket = priceBuckets.get(price);
      if (!bucket) {
        bucket = { price, restingBid: 0, restingAsk: 0, aggBuy: 0, aggSell: 0, delta: 0 };
        priceBuckets.set(price, bucket);
      }
      bucket.restingAsk = a.qty;
    }
  }
  
  // Listen for order book changes
  createEffect(() => {
    if (orderBookBids().length > 0 || orderBookAsks().length > 0) {
      updateRestingLiquidity();
    }
  });
  
  function connectWebSocket() {
    if (ws) ws.close();
    
    ws = new WebSocket('ws://localhost:8080');
    
    // Enable binary messages for BinarySerialization protocol
    ws.binaryType = 'arraybuffer';
    
    ws.onopen = () => {
      setConnectionStatus('connected');
      setStatusMessage('Connected - Loading data...');
      connectionStartTime = Date.now();  // Record connection start time
      console.log('[Frontend] Connected to backend');
      console.log('[Frontend] Connection started at:', new Date(connectionStartTime).toISOString());
      
      // Subscribe to symbol - this triggers bootstrap (history fetch + live stream)
      // History is now fetched by backend as part of bootstrap
      sendMessage({ type: 'subscribe', symbol: symbol(), interval: timeframe() });
    };
    
    ws.onclose = (e) => {
      setConnectionStatus('disconnected');
      // Reset history state on disconnect
      setHistoryLoaded(false);
      setPendingCandleUpdates([]);
      console.log('[Frontend] Disconnected:', e.code, e.reason);
      
      // Check if we should reconnect (24-hour strategy)
      const sessionDuration = Date.now() - connectionStartTime;
      const shouldReconnect24hr = connectionStartTime > 0 && sessionDuration >= MAX_CONNECTION_DURATION;
      
      if (shouldReconnect24hr) {
        console.log('[Frontend] 24-hour session completed, reconnecting...');
        setStatusMessage('24-hour session refresh - Reconnecting...');
      } else {
        setStatusMessage('Disconnected - Reconnecting...');
      }
      
      // Schedule reconnection
      reconnectTimer = setTimeout(() => connectWebSocket(), RECONNECT_DELAY);
    };
    
    ws.onerror = (e) => {
      console.error('[Frontend] WebSocket error:', e);
      setStatusMessage('Connection error');
    };
    
    ws.onmessage = (event) => {
      // Check if message is binary or text
      if (event.data instanceof ArrayBuffer) {
        // Binary message - use BinarySerialization protocol
        handleBinaryMessage(new Uint8Array(event.data));
      } else {
        // Text/JSON message
        try {
          const msg = JSON.parse(event.data);
          // Use batched message handling
          batchUpdate(() => handleMessage(msg));
        } catch (e) {
          console.error('[Frontend] Parse error:', e, event.data);
        }
      }
    };
  }

  function handleMessage(msg) {
    console.log('[Frontend] Received:', msg.type, msg);
    
    switch (msg.type) {
      case 'history':
        // Handle historical data response
        if (msg.candles && Array.isArray(msg.candles)) {
          setStatusMessage(`Loaded ${msg.candles.length} candles`);
          msg.candles.forEach(c => {
            // Convert milliseconds to seconds for Lightweight Charts
            const timeSeconds = Math.floor((c.time || c.t) / 1000);
            const candle = {
              time: timeSeconds,
              open: parseFloat(c.open || c.o),
              high: parseFloat(c.high || c.h),
              low: parseFloat(c.low || c.l),
              close: parseFloat(c.close || c.c),
            };
            candleSeries.update(candle);
            
            const vol = {
              time: timeSeconds,
              value: parseFloat(c.volume || c.v || 0),
              color: parseFloat(c.close || c.c) >= parseFloat(c.open || c.o) ? 'rgba(8, 153, 129, 0.5)' : 'rgba(242, 54, 69, 0.5)',
            };
            volumeSeries.update(vol);
          });
          
          // Mark history as loaded
          setHistoryLoaded(true);
          console.log('[Frontend] History loaded, processing pending updates...');
          
          // Process any pending live updates that arrived before history
          const pending = pendingCandleUpdates();
          if (pending.length > 0) {
            console.log(`[Frontend] Processing ${pending.length} pending live updates`);
            pending.forEach(candleData => {
              if (candleSeries) candleSeries.update(candleData.candle);
              if (volumeSeries) volumeSeries.update(candleData.volume);
            });
            setPendingCandleUpdates([]);
          }
        }
        break;
        
      case 'tick':
        // Handle real-time tick - use batched update for high frequency
        if (msg.data) {
          setPrice(parseFloat(msg.data.price || msg.data.c || 0));
          setPriceChange(parseFloat(msg.data.priceChange || 0));
          setPriceChangePercent(parseFloat(msg.data.priceChangePercent || 0));
        }
        break;
        
      case 'candle':
        // Handle real-time candle update - batched
        if (msg.time && msg.open !== undefined) {
          const timeSeconds = Math.floor((msg.time || msg.t) / 1000);
          const candle = {
            time: timeSeconds,
            open: parseFloat(msg.open || msg.o),
            high: parseFloat(msg.high || msg.h),
            low: parseFloat(msg.low || msg.l),
            close: parseFloat(msg.close || msg.c),
          };
          
          const vol = {
            time: timeSeconds,
            value: parseFloat(msg.volume || msg.v || 0),
            color: parseFloat(msg.close || msg.c) >= parseFloat(msg.open || msg.o) ? 'rgba(8, 153, 129, 0.5)' : 'rgba(242, 54, 69, 0.5)',
          };
          
          // Only update chart if history has been loaded
          if (historyLoaded()) {
            if (candleSeries) candleSeries.update(candle);
            if (volumeSeries) volumeSeries.update(vol);
          } else {
            // Buffer the update for later
            console.log('[Frontend] Buffering live update (history not yet loaded)');
            setPendingCandleUpdates(prev => [...prev, { candle, volume: vol }]);
          }
        }
        break;
        
      case 'subscribed':
        setStatusMessage('Subscribed to ' + msg.symbol);
        break;
        
      case 'exchangeInfo':
        // Handle exchange info response with symbol metadata
        if (msg.symbols && Array.isArray(msg.symbols)) {
          console.log('[Frontend] Received exchange info with', msg.symbols.length, 'symbols');
          setSymbols(msg.symbols);
          
          // Initialize worker with symbols and get quote assets
          if (symbolWorker) {
            // Get unique quote assets
            const uniqueQuotes = [...new Set(msg.symbols.map(s => s.quoteAsset))].sort();
            setQuoteAssets(uniqueQuotes);
            
            // Get top symbols by volume
            symbolWorker.postMessage({
              type: 'getTopByVolume',
              data: { symbols: msg.symbols, limit: 50 }
            });
          }
          setStatusMessage(`Loaded ${msg.symbols.length} symbols`);
        }
        break;
        
      case 'miniTicker':
        // Handle real-time mini ticker updates for all symbols
        if (msg.data && Array.isArray(msg.data)) {
          // Update search results with new prices
          if (showSymbolSearch() && symbolWorker) {
            // Update prices in search results
            const updatedSymbols = symbols().map(s => {
              const ticker = msg.data.find(t => t.s === s.symbol);
              if (ticker) {
                return {
                  ...s,
                  lastPrice: parseFloat(ticker.c || 0),
                  priceChange: parseFloat(ticker.c || 0) - parseFloat(ticker.o || 0),
                  priceChangePercent: ((parseFloat(ticker.c || 0) - parseFloat(ticker.o || 0)) / parseFloat(ticker.o || 1)) * 100,
                  high24h: parseFloat(ticker.h || 0),
                  low24h: parseFloat(ticker.l || 0),
                  volume24h: parseFloat(ticker.v || 0),
                  quoteVolume24h: parseFloat(ticker.q || 0)
                };
              }
              return s;
            });
            setSymbols(updatedSymbols);
          }
        }
        break;
        
      case 'error':
        setStatusMessage('Error: ' + msg.error);
        console.error('[Frontend] Server error:', msg.error);
        break;
        
      default:
        console.log('[Frontend] Unknown message type:', msg.type);
    }
  }

  // --- Binary Message Handler for BinarySerialization ---
  // Handle binary messages from backend (GLRD protocol)
  function handleBinaryMessage(data) {
    // Parse binary header (24 bytes)
    if (data.byteLength < 24) return;
    
    const view = new DataView(data.buffer, data.byteOffset);
    const magic = view.getUint32(0, true);  // Little endian
    const version = view.getUint8(4);
    const type = view.getUint8(5);
    const flags = view.getUint8(6);
    const payloadSize = view.getUint32(8, true);
    const timestamp = view.getUint64(12, true);
    const sequence = view.getUint64(20, true);
    
    // Check magic number "GLRD"
    if (magic !== 0x474C5244) {
      console.warn('[Frontend] Invalid binary message magic:', magic);
      return;
    }
    
    // Handle by message type
    switch (type) {
      case 0x01:  // Candle
        handleBinaryCandle(data, 24);
        break;
      case 0x02:  // Trade
        handleBinaryTrade(data, 24);
        break;
      case 0x03:  // Order Book
        handleBinaryOrderBook(data, 24);
        break;
      default:
        console.log('[Frontend] Unknown binary message type:', type);
    }
  }
  
  function handleBinaryCandle(data, offset) {
    // BinaryCandle is 49 bytes
    if (data.byteLength < offset + 49) return;
    
    const view = new DataView(data.buffer, data.byteOffset + offset);
    const openTime = view.getUint64(0, true);
    const closeTime = view.getUint64(8, true);
    const openPrice = view.getInt64(16, true) / 10000;
    const highPrice = view.getInt64(24, true) / 10000;
    const lowPrice = view.getInt64(32, true) / 10000;
    const closePrice = view.getInt64(40, true) / 10000;
    const volume = view.getInt64(48, true) / 1000000;
    
    // Update chart with batched update
    batchUpdate(() => {
      const timeSeconds = Math.floor(openTime / 1000);
      if (candleSeries) {
        candleSeries.update({
          time: timeSeconds,
          open: openPrice,
          high: highPrice,
          low: lowPrice,
          close: closePrice
        });
      }
      if (volumeSeries) {
        volumeSeries.update({
          time: timeSeconds,
          value: volume,
          color: closePrice >= openPrice ? 'rgba(8, 153, 129, 0.5)' : 'rgba(242, 54, 69, 0.5)'
        });
      }
    });
  }
  
  function handleBinaryTrade(data, offset) {
    // BinaryTrade is 41 bytes
    if (data.byteLength < offset + 41) return;
    
    const view = new DataView(data.buffer, data.byteOffset + offset);
    const tradeId = view.getInt64(0, true);
    const price = view.getInt64(8, true) / 10000;
    const quantity = view.getInt64(16, true) / 1000000;
    const tradeTime = view.getUint64(24, true);
    const side = view.getUint8(32);
    
    // Update price display
    batchUpdate(() => {
      setPrice(price);
    });
  }
  
  function handleBinaryOrderBook(data, offset) {
    if (data.byteLength < offset + 12) return;
    
    const view = new DataView(data.buffer, data.byteOffset + offset);
    const lastUpdateId = view.getUint64(0, true);
    const bidsCount = view.getUint16(8, true);
    const asksCount = view.getUint16(10, true);
    
    // Would update order book display here
    console.log('[Frontend] Binary order book:', { lastUpdateId, bidsCount, asksCount });
  }

  function sendMessage(msg) {
    if (ws && ws.readyState === WebSocket.OPEN) {
      ws.send(JSON.stringify(msg));
      console.log('[Frontend] Sent:', msg);
    } else {
      console.warn('[Frontend] Cannot send - not connected');
    }
  }

  function requestHistory() {
    // Use correct message type 'getHistory' instead of 'fetch'
    sendMessage({
      type: 'getHistory',
      symbol: symbol(),
      interval: timeframe(),
      days: 7  // Get 7 days of history
    });
  }

  function loadData() {
    // Reset history state
    setHistoryLoaded(false);
    setPendingCandleUpdates([]);
    if (connectionStatus() === 'connected') {
      setStatusMessage('Loading data...');
      // Subscribe triggers bootstrap (history + live stream)
      sendMessage({ type: 'subscribe', symbol: symbol(), interval: timeframe() });
    }
  }

  function handleSymbolChange(newSymbol) {
    setSymbol(newSymbol);
    // Clear chart
    if (candleSeries) {
      // Create empty data to clear
      candleSeries.setData([]);
      volumeSeries.setData([]);
    }
    // Reset history state for new symbol
    setHistoryLoaded(false);
    setPendingCandleUpdates([]);
    // Subscribe triggers bootstrap (history + live stream)
    if (connectionStatus() === 'connected') {
      sendMessage({ type: 'subscribe', symbol: newSymbol });
      // History will be fetched by backend as part of bootstrap
    }
  }

  function handleTimeframeChange(tf) {
    setTimeframe(tf);
    // Clear chart
    if (candleSeries) {
      candleSeries.setData([]);
      volumeSeries.setData([]);
    }
    // Reset history state for new timeframe
    setHistoryLoaded(false);
    setPendingCandleUpdates([]);
    // Subscribe triggers bootstrap (history + live stream)
    if (connectionStatus() === 'connected') {
      sendMessage({ type: 'subscribe', symbol: symbol(), interval: tf });
      // History will be fetched by backend as part of bootstrap
    }
  }

  return (
    <div style={{ display: 'flex', 'flex-direction': 'column', width: '100%', height: '100%' }}>
      {/* Header */}
      <header style={{ height: '48px', background: '#1a1d29', 'border-bottom': '1px solid #2a2e39', display: 'flex', 'align-items': 'center', padding: '0 16px', gap: '16px' }}>
        <select 
          value={symbol()} 
          onChange={(e) => handleSymbolChange(e.target.value)} 
          style={{ background: '#242832', border: '1px solid #2a2e39', padding: '6px 12px', color: '#d1d4dc', 'border-radius': '4px' }}
        >
          <For each={SYMBOLS}>{(s) => <option value={s}>{s}</option>}</For>
        </select>
        
        <div style={{ display: 'flex', gap: '2px', background: '#242832', padding: '2px', 'border-radius': '4px' }}>
          <For each={TIMEFRAMES}>{(tf) => (
            <button 
              onClick={() => handleTimeframeChange(tf)} 
              style={{ 
                padding: '4px 8px', 
                border: 'none', 
                background: timeframe() === tf ? '#2962ff' : 'transparent', 
                color: timeframe() === tf ? 'white' : '#787b86', 
                'border-radius': '3px', 
                cursor: 'pointer', 
                'font-size': '11px' 
              }}
            >
              {tf}
            </button>
          )}</For>
        </div>
        
        <span style={{ 'font-size': '20px', 'font-weight': '700', color: priceChange() >= 0 ? '#089981' : '#f23645' }}>
          ${price().toLocaleString(undefined, { minimumFractionDigits: 2, maximumFractionDigits: 2 })}
        </span>
        <span style={{ color: priceChange() >= 0 ? '#089981' : '#f23645', 'font-size': '13px' }}>
          {priceChange() >= 0 ? '+' : ''}{priceChangePercent().toFixed(2)}%
        </span>
        
        <div style={{ flex: 1 }} />
        
        <span style={{ 'font-size': '11px', color: '#787b86' }}>{statusMessage()}</span>
        
        <div style={{ 
          width: '8px', 
          height: '8px', 
          'border-radius': '50%', 
          background: connectionStatus() === 'connected' ? '#089981' : '#f23645',
          animation: connectionStatus() === 'connected' ? 'none' : 'pulse 1s infinite'
        }} />
        
        <button 
          onClick={loadData} 
          style={{ 
            padding: '6px 12px', 
            background: '#2962ff', 
            border: 'none', 
            'border-radius': '4px', 
            color: 'white', 
            cursor: 'pointer' 
          }}
        >
          Load Data
        </button>
        
        <button 
          onClick={() => setShowSymbolSearch(!showSymbolSearch())} 
          style={{ 
            padding: '6px 12px', 
            background: showSymbolSearch() ? '#089981' : '#242832', 
            border: '1px solid #2a2e39', 
            'border-radius': '4px', 
            color: '#d1d4dc', 
            cursor: 'pointer',
            display: 'flex',
            'align-items': 'center',
            gap: '6px'
          }}
        >
          🔍 Search
        </button>
      </header>

      {/* Symbol Search Modal */}
      <Show when={showSymbolSearch()}>
        <div style={{ 
          position: 'fixed', 
          top: '48px', 
          right: '10px', 
          width: '400px', 
          background: '#1a1d29', 
          'border-radius': '8px', 
          'box-shadow': '0 4px 20px rgba(0,0,0,0.5)',
          'z-index': 1000,
          'border': '1px solid #2a2e39'
        }}>
          {/* Search Input */}
          <div style={{ padding: '12px', 'border-bottom': '1px solid #2a2e39' }}>
            <input 
              type="text"
              value={searchQuery()}
              onInput={handleSearchInput}
              placeholder="Search symbols... (e.g., BTC, ETH, SOL)"
              style={{ 
                width: '100%', 
                padding: '10px 12px', 
                background: '#242832', 
                border: '1px solid #2a2e39', 
                'border-radius': '4px', 
                color: '#d1d4dc',
                'font-size': '14px',
                outline: 'none'
              }}
            />
            
            {/* Quote Asset Filter */}
            <div style={{ 'margin-top': '8px', display: 'flex', gap: '4px', 'flex-wrap': 'wrap' }}>
              <button
                onClick={() => handleQuoteAssetChange('')}
                style={{
                  padding: '4px 8px',
                  background: !selectedQuoteAsset() ? '#2962ff' : '#242832',
                  border: '1px solid #2a2e39',
                  'border-radius': '4px',
                  color: '#d1d4dc',
                  'font-size': '11px',
                  cursor: 'pointer'
                }}
              >
                All
              </button>
              <For each={quoteAssets().slice(0, 6)}>{(qa) => (
                <button
                  onClick={() => handleQuoteAssetChange(qa)}
                  style={{
                    padding: '4px 8px',
                    background: selectedQuoteAsset() === qa ? '#2962ff' : '#242832',
                    border: '1px solid #2a2e39',
                    'border-radius': '4px',
                    color: '#d1d4dc',
                    'font-size': '11px',
                    cursor: 'pointer'
                  }}
                >
                  {qa}
                </button>
              )}</For>
            </div>
          </div>
          
          {/* Search Results */}
          <div style={{ 'max-height': '400px', 'overflow-y': 'auto' }}>
            <Show when={isSearching()}>
              <div style={{ padding: '20px', 'text-align': 'center', color: '#787b86' }}>
                Searching...
              </div>
            </Show>
            <Show when={!isSearching()}>
              <For each={searchResults()}>{(s) => (
                <div 
                  onClick={() => handleSymbolSelect(s.symbol)}
                  style={{
                    padding: '10px 12px',
                    display: 'flex',
                    'justify-content': 'space-between',
                    'align-items': 'center',
                    cursor: 'pointer',
                    'border-bottom': '1px solid #2a2e3930'
                  }}
                  onMouseEnter={(e) => e.currentTarget.style.background = '#242832'}
                  onMouseLeave={(e) => e.currentTarget.style.background = 'transparent'}
                >
                  <div>
                    <span style={{ 'font-weight': '600', color: '#d1d4dc' }}>{s.symbol}</span>
                    <span style={{ 'margin-left': '8px', 'font-size': '11px', color: '#787b86' }}>
                      {s.baseAsset}/{s.quoteAsset}
                    </span>
                  </div>
                  <div style={{ 'text-align': 'right' }}>
                    <div style={{ color: '#d1d4dc', 'font-size': '13px' }}>
                      ${s.lastPrice?.toLocaleString(undefined, { minimumFractionDigits: 2, maximumFractionDigits: 2 }) || '0.00'}
                    </div>
                    <div style={{ color: (s.priceChangePercent || 0) >= 0 ? '#089981' : '#f23645', 'font-size': '11px' }}>
                      {(s.priceChangePercent || 0) >= 0 ? '+' : ''}{(s.priceChangePercent || 0).toFixed(2)}%
                    </div>
                  </div>
                </div>
              )}</For>
              <Show when={searchResults().length === 0 && !isSearching()}>
                <div style={{ padding: '20px', 'text-align': 'center', color: '#787b86' }}>
                  No symbols found
                </div>
              </Show>
            </Show>
          </div>
        </div>
      </Show>

      {/* Main */}
      <main style={{ display: 'flex', flex: 1, overflow: 'hidden' }}>
        {/* Sidebar */}
        <aside style={{ width: '60px', background: '#1a1d29', 'border-right': '1px solid #2a2e39', display: 'flex', 'flex-direction': 'column', 'align-items': 'center', padding: '8px 0', gap: '8px' }}>
          <button 
            onClick={() => { setShowOrderBook(!showOrderBook()); setTimeout(() => chart && chartContainer && chart.applyOptions({ width: chartContainer.clientWidth, height: chartContainer.clientHeight }), 50); }} 
            style={{ 
              width: '40px', 
              height: '40px', 
              border: 'none', 
              background: showOrderBook() ? '#2962ff' : 'transparent', 
              color: '#d1d4dc', 
              'border-radius': '8px', 
              cursor: 'pointer',
              'font-size': '16px'
            }}
          >
            📊
          </button>
          <button 
            onClick={() => { setShowTradeHistory(!showTradeHistory()); setTimeout(() => chart && chartContainer && chart.applyOptions({ width: chartContainer.clientWidth, height: chartContainer.clientHeight }), 50); }} 
            style={{ 
              width: '40px', 
              height: '40px', 
              border: 'none', 
              background: showTradeHistory() ? '#2962ff' : 'transparent', 
              color: '#d1d4dc', 
              'border-radius': '8px', 
              cursor: 'pointer',
              'font-size': '16px'
            }}
          >
            📜
          </button>
        </aside>

        {/* Chart */}
        <section style={{ flex: 1, display: 'flex', 'flex-direction': 'column', background: '#131722' }}>
          <div style={{ height: '36px', background: '#1a1d29', 'border-bottom': '1px solid #2a2e39', display: 'flex', 'align-items': 'center', padding: '0 8px' }}>
            <span style={{ color: '#787b86', 'font-size': '12px' }}>{symbol()} {timeframe()}</span>
          </div>
          <div ref={chartContainer} style={{ flex: 1, position: 'relative' }} />
        </section>

        {/* Smart DOM (Volume Depth of Market) */}
        <Show when={showOrderBook()}>
          <div style={{ width: '320px', background: '#1a1d29', 'border-left': '1px solid #2a2e39', padding: '12px' }}>
            <div style={{ 'font-weight': '600', 'font-size': '12px', 'margin-bottom': '12px' }}>
              Smart DOM
              <Show when={pocPrice() > 0}>
                <span style={{ 'margin-left': '8px', 'font-size': '10px', color: '#787b86' }}>POC: {pocPrice().toFixed(2)}</span>
              </Show>
            </div>
            
            {/* Header */}
            <div style={{ display: 'grid', 'grid-template-columns': '1fr 1fr 1fr 1fr', 'font-size': '10px', color: '#787b86', 'margin-bottom': '8px' }}>
              <span>Bid Vol</span><span style={{ 'text-align': 'center' }}>Price</span><span style={{ 'text-align': 'right' }}>Ask Vol</span><span style={{ 'text-align': 'right' }}>Δ</span>
            </div>
            
            {/* Asks (Sells) - reversed to show highest at top */}
            <For each={smartDOMData().filter(d => d.price > pocPrice()).slice(0, 10).reverse()}>{(row) => (
              <div style={{ display: 'grid', 'grid-template-columns': '1fr 1fr 1fr 1fr', padding: '3px 0', 'font-size': '11px', 
                background: row.delta < -0.5 ? 'rgba(242, 54, 69, 0.15)' : 'transparent' }}>
                <span style={{ 'text-align': 'left', color: '#787b86' }}></span>
                <span style={{ 'text-align': 'center', color: '#f23645', 'font-weight': row.price === pocPrice() ? '700' : '400' }}>
                  {row.price.toFixed(2)}
                </span>
                <span style={{ 'text-align': 'right', color: '#f23645' }}>{row.restingAsk.toFixed(4)}</span>
                <span style={{ 'text-align': 'right', color: row.delta < 0 ? '#f23645' : '#089981' }}>
                  {row.delta.toFixed(2)}
                </span>
              </div>
            )}</For>
            
            {/* POC Line */}
            <div style={{ padding: '4px 0', 'border-top': '1px solid #2a2e39', 'border-bottom': '1px solid #2a2e39', 'margin': '4px 0', 
              display: 'flex', 'justify-content': 'space-between', 'font-size': '11px', 'background': '#2962ff20' }}>
              <span style={{ color: '#787b86' }}>Spread</span>
              <span>{(smartDOMData()[0]?.price - smartDOMData().find(d => d.restingAsk > 0)?.price || 0).toFixed(2)}</span>
            </div>
            
            {/* Bids (Buys) */}
            <For each={smartDOMData().filter(d => d.price <= pocPrice()).slice(0, 10)}>{(row) => (
              <div style={{ display: 'grid', 'grid-template-columns': '1fr 1fr 1fr 1fr', padding: '3px 0', 'font-size': '11px',
                background: row.delta > 0.5 ? 'rgba(8, 153, 129, 0.15)' : 'transparent' }}>
                <span style={{ 'text-align': 'left', color: '#089981', 'font-weight': '500' }}>{row.restingBid.toFixed(4)}</span>
                <span style={{ 'text-align': 'center', color: '#089981', 'font-weight': row.price === pocPrice() ? '700' : '400' }}>
                  {row.price.toFixed(2)}
                </span>
                <span style={{ 'text-align': 'right', color: '#787b86' }}></span>
                <span style={{ 'text-align': 'right', color: row.delta > 0 ? '#089981' : '#f23645' }}>
                  {row.delta.toFixed(2)}
                </span>
              </div>
            )}</For>
          </div>
        </Show>
      </main>
      
      <style>{`
        @keyframes pulse {
          0%, 100% { opacity: 1; }
          50% { opacity: 0.5; }
        }
      `}</style>
    </div>
  );
}

export default App;
