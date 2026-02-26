import { createSignal, onMount, Show, For, onCleanup, createEffect } from 'solid-js';
import { createChart, ColorType } from 'lightweight-charts';

const SYMBOLS = ['BTCUSDT', 'ETHUSDT', 'SOLUSDT', 'BNBUSDT'];
const TIMEFRAMES = ['1m', '5m', '15m', '1h', '4h', '1D'];

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

  let chartContainer;
  let chart;
  let candleSeries;
  let volumeSeries;
  let ws;
  let reconnectTimer;

  onMount(() => {
    initChart();
    connectWebSocket();
  });

  onCleanup(() => {
    if (ws) ws.close();
    if (reconnectTimer) clearTimeout(reconnectTimer);
    if (chart) chart.remove();
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

  function connectWebSocket() {
    if (ws) ws.close();
    
    ws = new WebSocket('ws://localhost:8080');
    
    ws.onopen = () => {
      setConnectionStatus('connected');
      setStatusMessage('Connected - Loading data...');
      console.log('[Frontend] Connected to backend');
      
      // Subscribe to symbol
      sendMessage({ type: 'subscribe', symbol: symbol() });
      
      // Request history
      requestHistory();
    };
    
    ws.onclose = (e) => {
      setConnectionStatus('disconnected');
      setStatusMessage('Disconnected - Reconnecting...');
      console.log('[Frontend] Disconnected:', e.code, e.reason);
      
      // Auto reconnect after 3 seconds
      reconnectTimer = setTimeout(() => connectWebSocket(), 3000);
    };
    
    ws.onerror = (e) => {
      console.error('[Frontend] WebSocket error:', e);
      setStatusMessage('Connection error');
    };
    
    ws.onmessage = (event) => {
      try {
        const msg = JSON.parse(event.data);
        handleMessage(msg);
      } catch (e) {
        console.error('[Frontend] Parse error:', e, event.data);
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
        }
        break;
        
      case 'tick':
        // Handle real-time tick
        if (msg.data) {
          setPrice(parseFloat(msg.data.price || msg.data.c || 0));
          setPriceChange(parseFloat(msg.data.priceChange || 0));
          setPriceChangePercent(parseFloat(msg.data.priceChangePercent || 0));
        }
        break;
        
      case 'candle':
        // Handle real-time candle update
        if (msg.time && msg.open !== undefined) {
          const timeSeconds = Math.floor((msg.time || msg.t) / 1000);
          const candle = {
            time: timeSeconds,
            open: parseFloat(msg.open || msg.o),
            high: parseFloat(msg.high || msg.h),
            low: parseFloat(msg.low || msg.l),
            close: parseFloat(msg.close || msg.c),
          };
          if (candleSeries) candleSeries.update(candle);
          
          const vol = {
            time: timeSeconds,
            value: parseFloat(msg.volume || msg.v || 0),
            color: parseFloat(msg.close || msg.c) >= parseFloat(msg.open || msg.o) ? 'rgba(8, 153, 129, 0.5)' : 'rgba(242, 54, 69, 0.5)',
          };
          if (volumeSeries) volumeSeries.update(vol);
        }
        break;
        
      case 'subscribed':
        setStatusMessage('Subscribed to ' + msg.symbol);
        break;
        
      case 'error':
        setStatusMessage('Error: ' + msg.error);
        console.error('[Frontend] Server error:', msg.error);
        break;
        
      default:
        console.log('[Frontend] Unknown message type:', msg.type);
    }
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
    if (connectionStatus() === 'connected') {
      setStatusMessage('Loading data...');
      requestHistory();
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
    // Re-request history for new symbol
    if (connectionStatus() === 'connected') {
      sendMessage({ type: 'subscribe', symbol: newSymbol });
      requestHistory();
    }
  }

  function handleTimeframeChange(tf) {
    setTimeframe(tf);
    // Clear chart
    if (candleSeries) {
      candleSeries.setData([]);
      volumeSeries.setData([]);
    }
    // Re-request history for new timeframe
    if (connectionStatus() === 'connected') {
      requestHistory();
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
      </header>

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

        {/* Order Book */}
        <Show when={showOrderBook()}>
          <div style={{ width: '280px', background: '#1a1d29', 'border-left': '1px solid #2a2e39', padding: '12px' }}>
            <div style={{ 'font-weight': '600', 'font-size': '12px', 'margin-bottom': '12px' }}>Order Book</div>
            <div style={{ display: 'grid', 'grid-template-columns': '1fr 1fr 1fr', 'font-size': '11px', color: '#787b86', 'margin-bottom': '8px' }}>
              <span>Price</span><span style={{ 'text-align': 'right' }}>Quantity</span><span style={{ 'text-align': 'right' }}>Total</span>
            </div>
            <For each={Array.from({ length: 10 }, (_, i) => i)}>{(i) => (
              <div style={{ display: 'grid', 'grid-template-columns': '1fr 1fr 1fr', padding: '4px 0', 'font-size': '11px' }}>
                <span style={{ color: '#f23645' }}>{(45000 + i * 5).toFixed(2)}</span>
                <span style={{ 'text-align': 'right' }}>{(Math.random() * 2).toFixed(4)}</span>
                <span style={{ 'text-align': 'right', color: '#787b86' }}>{(Math.random() * 10).toFixed(4)}</span>
              </div>
            )}</For>
            <div style={{ padding: '8px 0', 'border-top': '1px solid #2a2e39', 'border-bottom': '1px solid #2a2e39', 'margin': '8px 0', display: 'flex', 'justify-content': 'space-between', 'font-size': '12px' }}>
              <span style={{ color: '#787b86' }}>Spread</span>
              <span>5.00 (0.01%)</span>
            </div>
            <For each={Array.from({ length: 10 }, (_, i) => i)}>{(i) => (
              <div style={{ display: 'grid', 'grid-template-columns': '1fr 1fr 1fr', padding: '4px 0', 'font-size': '11px' }}>
                <span style={{ color: '#089981' }}>{(45000 - i * 5).toFixed(2)}</span>
                <span style={{ 'text-align': 'right' }}>{(Math.random() * 2).toFixed(4)}</span>
                <span style={{ 'text-align': 'right', color: '#787b86' }}>{(Math.random() * 10).toFixed(4)}</span>
              </div>
            )}</For>
          </div>
        </Show>

        {/* Trade History */}
        <Show when={showTradeHistory()}>
          <div style={{ width: '280px', background: '#1a1d29', 'border-left': '1px solid #2a2e39', padding: '12px' }}>
            <div style={{ 'font-weight': '600', 'font-size': '12px', 'margin-bottom': '12px' }}>Recent Trades</div>
            <div style={{ display: 'grid', 'grid-template-columns': '1fr 1fr 1fr', 'font-size': '11px', color: '#787b86', 'margin-bottom': '8px' }}>
              <span>Price</span><span style={{ 'text-align': 'right' }}>Amount</span><span style={{ 'text-align': 'right' }}>Time</span>
            </div>
            <For each={Array.from({ length: 20 }, (_, i) => i)}>{(i) => (
              <div style={{ display: 'grid', 'grid-template-columns': '1fr 1fr 1fr', padding: '4px 0', 'font-size': '11px', 'border-bottom': '1px solid #2a2e3920' }}>
                <span style={{ color: Math.random() > 0.5 ? '#089981' : '#f23645' }}>{(45000 + (Math.random() - 0.5) * 100).toFixed(2)}</span>
                <span style={{ 'text-align': 'right' }}>{(Math.random() * 0.5).toFixed(4)}</span>
                <span style={{ 'text-align': 'right', color: '#787b86' }}>{new Date(Date.now() - i * 1000).toLocaleTimeString('en-US', { hour12: false })}</span>
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
