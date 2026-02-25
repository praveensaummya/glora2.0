import { useState, useEffect, useCallback } from 'react'
import { FontAwesomeIcon } from '@fortawesome/react-fontawesome'
import { faChartLine, faMoneyBillWave, faExchange, faCog, faRightFromBracket, faWallet, faBell, faSearch, faBars, faChartSimple, faLineChart, faBorderAll, faLayerGroup } from '@fortawesome/free-solid-svg-icons'
import Chart from './components/Chart'
import DOM from './components/DOM'
import FootprintChart from './components/FootprintChart'
import SessionSummary from './components/SessionSummary'
import AlertPanel, { AlertNotification } from './components/AlertPanel'
import Settings from './components/Settings'
import { TIMEFRAME_OPTIONS } from './utils/indicators'
import { useIPC } from './hooks/useIPC'

// Available indicators
const AVAILABLE_INDICATORS = [
  { id: 'sma20', name: 'SMA 20', color: '#22d3ee', type: 'overlay' },
  { id: 'sma50', name: 'SMA 50', color: '#a855f7', type: 'overlay' },
  { id: 'sma200', name: 'SMA 200', color: '#f59e0b', type: 'overlay' },
  { id: 'ema12', name: 'EMA 12', color: '#ec4899', type: 'overlay' },
  { id: 'ema26', name: 'EMA 26', color: '#8b5cf6', type: 'overlay' },
  { id: 'bollinger', name: 'Bollinger Bands', color: '#22c55e', type: 'overlay' },
  { id: 'rsi', name: 'RSI (14)', color: '#06b6d4', type: 'pane' },
  { id: 'atr', name: 'ATR (14)', color: '#f97316', type: 'pane' },
]

function App() {
  const { isConnected } = useIPC()
  const [logs, setLogs] = useState([])
  const [symbol, setSymbol] = useState('BTCUSDT')
  const [interval, setInterval] = useState('1m')
  const [indicators, setIndicators] = useState(['sma20', 'ema12'])
  const [showIndicatorPanel, setShowIndicatorPanel] = useState(false)
  const [showDOM, setShowDOM] = useState(false)
  const [showFootprint, setShowFootprint] = useState(false)
  const [domDepth, setDomDepth] = useState(20)
  const [footprintPOC, setFootprintPOC] = useState(null)
  const [footprintVA, setFootprintVA] = useState(null)
  const [footprintImbalances, setFootprintImbalances] = useState([])
  
  // Alert state
  const [showAlertPanel, setShowAlertPanel] = useState(false)
  const [notifications, setNotifications] = useState([])
  const [showSessionSummary, setShowSessionSummary] = useState(true)
  const [showSettings, setShowSettings] = useState(false)

  const addLog = (message) => {
    const time = new Date().toLocaleTimeString()
    setLogs(prev => [...prev, { time, message }])
  }

  // Handle interval change
  const handleIntervalChange = useCallback((newInterval) => {
    setInterval(newInterval)
    addLog(`Switched to ${newInterval} timeframe`)
  }, [])

  // Handle indicator toggle
  const toggleIndicator = useCallback((indicatorId) => {
    setIndicators(prev => {
      if (prev.includes(indicatorId)) {
        return prev.filter(i => i !== indicatorId)
      } else {
        return [...prev, indicatorId]
      }
    })
  }, [])

  // Handle DOM depth change
  const handleDOMDepthChange = useCallback((depth) => {
    setDomDepth(depth)
    addLog(`DOM depth changed to ${depth} levels`)
  }, [])

  // Toggle footprint chart mode
  const toggleFootprintChart = useCallback(() => {
    setShowFootprint(prev => !prev)
    if (!showFootprint) {
      addLog('Footprint chart mode enabled')
    } else {
      addLog('Footprint chart mode disabled')
    }
  }, [showFootprint])

  // Handle POC change from footprint chart
  const handlePOCChange = useCallback((poc) => {
    setFootprintPOC(poc)
  }, [])

  // Handle Value Area change from footprint chart
  const handleValueAreaChange = useCallback((va) => {
    setFootprintVA(va)
  }, [])

  // Handle imbalances found
  const handleImbalancesFound = useCallback((imbs) => {
    setFootprintImbalances(imbs)
    if (imbs.length > 0) {
      addLog(`Found ${imbs.length} order flow imbalances`)
    }
  }, [])

  // Handle alert trigger
  const handleAlertTrigger = useCallback((alert) => {
    setNotifications(prev => [alert, ...prev].slice(0, 5))
  }, [])

  // Remove notification
  const handleRemoveNotification = useCallback((index) => {
    setNotifications(prev => prev.filter((_, i) => i !== index))
  }, [])

  // Handle connection status changes
  useEffect(() => {
    if (isConnected) {
      // Use refs to avoid setState in effect
      console.log('[App] Connected to C++ Backend via IPC')
    }
  }, [isConnected])

  return (
    <div className="flex h-screen bg-gradient-to-br from-slate-900 via-slate-800 to-slate-900">
      {/* Sidebar */}
      <aside className="w-16 flex flex-col items-center py-4 bg-slate-950/50 border-r border-slate-700/50">
        <div className="mb-8">
          <FontAwesomeIcon icon={faChartLine} className="text-2xl text-cyan-400" />
        </div>
        
        <nav className="flex flex-col gap-4 flex-1">
          <button className="p-3 rounded-xl bg-cyan-500/20 text-cyan-400">
            <FontAwesomeIcon icon={faChartLine} />
          </button>
          <button className="p-3 rounded-xl text-slate-400 hover:bg-slate-800 hover:text-slate-200 transition-colors">
            <FontAwesomeIcon icon={faMoneyBillWave} />
          </button>
          <button className="p-3 rounded-xl text-slate-400 hover:bg-slate-800 hover:text-slate-200 transition-colors">
            <FontAwesomeIcon icon={faExchange} />
          </button>
          <button className="p-3 rounded-xl text-slate-400 hover:bg-slate-800 hover:text-slate-200 transition-colors">
            <FontAwesomeIcon icon={faWallet} />
          </button>
          <button 
            onClick={() => setShowAlertPanel(true)}
            className="p-3 rounded-xl text-slate-400 hover:bg-slate-800 hover:text-slate-200 transition-colors relative"
          >
            <FontAwesomeIcon icon={faBell} />
          </button>
        </nav>

        <button 
          className="p-3 rounded-xl text-slate-400 hover:bg-slate-800 hover:text-slate-200 transition-colors"
          onClick={() => setShowSettings(true)}
        >
          <FontAwesomeIcon icon={faCog} />
        </button>
        <button className="p-3 rounded-xl text-red-400 hover:bg-red-500/20 transition-colors mt-2">
          <FontAwesomeIcon icon={faRightFromBracket} />
        </button>
      </aside>

      {/* Main Content */}
      <main className="flex-1 flex flex-col">
        {/* Header */}
        <header className="h-14 flex items-center justify-between px-4 bg-slate-950/30 border-b border-slate-700/50">
          <div className="flex items-center gap-4">
            <button className="lg:hidden text-slate-400">
              <FontAwesomeIcon icon={faBars} />
            </button>
            
            {/* Symbol Input */}
            <div className="flex items-center gap-2">
              <input 
                type="text" 
                value={symbol}
                onChange={(e) => setSymbol(e.target.value.toUpperCase())}
                className="bg-slate-800/50 border border-slate-700 rounded-lg px-3 py-1 text-sm text-white font-semibold focus:outline-none focus:border-cyan-500 w-28"
              />
              <span className="text-lg font-semibold text-white">/USDT</span>
              <span className="font-mono text-slate-400">
                Waiting for data...
              </span>
            </div>
          </div>

          <div className="flex items-center gap-4">
            {/* Timeframe Selector */}
            <div className="flex items-center gap-1 bg-slate-800/50 rounded-lg p-1">
              {TIMEFRAME_OPTIONS.map((tf) => (
                <button
                  key={tf.value}
                  onClick={() => handleIntervalChange(tf.value)}
                  className={`px-3 py-1 text-xs font-medium rounded-md transition-colors ${
                    interval === tf.value
                      ? 'bg-cyan-500 text-white'
                      : 'text-slate-400 hover:text-white hover:bg-slate-700'
                  }`}
                  title={tf.description}
                >
                  {tf.label}
                </button>
              ))}
            </div>

            {/* Indicator Toggle */}
            <button 
              onClick={() => setShowIndicatorPanel(!showIndicatorPanel)}
              className={`p-2 rounded-lg transition-colors ${
                showIndicatorPanel 
                  ? 'bg-cyan-500/20 text-cyan-400' 
                  : 'text-slate-400 hover:text-white hover:bg-slate-800'
              }`}
              title="Toggle Indicators"
            >
              <FontAwesomeIcon icon={faChartSimple} />
            </button>

            {/* DOM Toggle */}
            <button 
              onClick={() => {
                setShowDOM(!showDOM)
                if (!showDOM) addLog('Order Book (DOM) panel opened')
              }}
              className={`p-2 rounded-lg transition-colors ${
                showDOM 
                  ? 'bg-purple-500/20 text-purple-400' 
                  : 'text-slate-400 hover:text-white hover:bg-slate-800'
              }`}
              title="Toggle DOM Panel"
            >
              <FontAwesomeIcon icon={faBorderAll} />
            </button>

            {/* Footprint Chart Toggle */}
            <button 
              onClick={toggleFootprintChart}
              className={`p-2 rounded-lg transition-colors ${
                showFootprint 
                  ? 'bg-amber-500/20 text-amber-400' 
                  : 'text-slate-400 hover:text-white hover:bg-slate-800'
              }`}
              title="Toggle Footprint Chart"
            >
              <FontAwesomeIcon icon={faLayerGroup} />
            </button>

            {/* Session Summary Toggle */}
            <button 
              onClick={() => setShowSessionSummary(!showSessionSummary)}
              className={`p-2 rounded-lg transition-colors ${
                showSessionSummary 
                  ? 'bg-green-500/20 text-green-400' 
                  : 'text-slate-400 hover:text-white hover:bg-slate-800'
              }`}
              title="Toggle Session Summary"
            >
              <FontAwesomeIcon icon={faChartSimple} />
            </button>

            {/* Search */}
            <div className="relative">
              <FontAwesomeIcon icon={faSearch} className="absolute left-3 top-1/2 -translate-y-1/2 text-slate-500" />
              <input 
                type="text" 
                placeholder="Search..." 
                className="bg-slate-800/50 border border-slate-700 rounded-lg pl-10 pr-4 py-1.5 text-sm text-slate-200 placeholder-slate-500 focus:outline-none focus:border-cyan-500"
              />
            </div>
            <button className="relative text-slate-400 hover:text-slate-200">
              <FontAwesomeIcon icon={faBell} />
              <span className="absolute -top-1 -right-1 w-2 h-2 bg-cyan-400 rounded-full"></span>
            </button>
            <div className="flex items-center gap-2">
              <div className="w-8 h-8 rounded-full bg-gradient-to-r from-cyan-500 to-purple-500"></div>
              <span className="text-sm text-slate-300">Trader</span>
            </div>
          </div>
        </header>

        {/* Chart Area */}
        <div className="flex-1 p-4 flex gap-4">
          {/* Main Chart */}
          <div className="flex-1 bg-slate-900/50 rounded-2xl border border-slate-700/50 overflow-hidden">
            {showFootprint ? (
              <FootprintChart 
                theme="dark"
                showPOC={true}
                showVA={true}
                valueAreaPercent={70}
                onPOCChange={handlePOCChange}
                onValueAreaChange={handleValueAreaChange}
                onImbalancesFound={handleImbalancesFound}
              />
            ) : (
              <Chart 
                theme="dark" 
                symbol={symbol}
                interval={interval}
                indicators={indicators}
              />
            )}
          </div>

          {/* DOM Panel */}
          {showDOM && (
            <div className="w-72 flex-shrink-0">
              <DOM 
                depth={domDepth}
                onDepthChange={handleDOMDepthChange}
              />
            </div>
          )}

          {/* Session Summary */}
          {showSessionSummary && (
            <div className="flex-shrink-0">
              <SessionSummary compact />
            </div>
          )}

          {/* Indicator Panel */}
          {showIndicatorPanel && (
            <div className="w-64 bg-slate-900/50 rounded-2xl border border-slate-700/50 p-4">
              <h3 className="text-sm font-semibold text-white mb-3 flex items-center gap-2">
                <FontAwesomeIcon icon={faLineChart} className="text-cyan-400" />
                Indicators
              </h3>
              
              <div className="space-y-2">
                {AVAILABLE_INDICATORS.map((indicator) => (
                  <button
                    key={indicator.id}
                    onClick={() => toggleIndicator(indicator.id)}
                    className={`w-full flex items-center justify-between px-3 py-2 rounded-lg text-sm transition-colors ${
                      indicators.includes(indicator.id)
                        ? 'bg-slate-700 text-white'
                        : 'text-slate-400 hover:bg-slate-800 hover:text-white'
                    }`}
                  >
                    <div className="flex items-center gap-2">
                      <span 
                        className="w-3 h-3 rounded-full" 
                        style={{ backgroundColor: indicator.color }}
                      />
                      <span>{indicator.name}</span>
                    </div>
                    {indicators.includes(indicator.id) && (
                      <span className="text-xs text-green-400">ON</span>
                    )}
                  </button>
                ))}
              </div>

              <div className="mt-4 pt-4 border-t border-slate-700">
                <h4 className="text-xs font-medium text-slate-400 mb-2">Active Indicators</h4>
                <div className="flex flex-wrap gap-1">
                  {indicators.map((ind) => {
                    const info = AVAILABLE_INDICATORS.find(i => i.id === ind)
                    return info ? (
                      <span 
                        key={ind}
                        className="inline-flex items-center gap-1 px-2 py-1 rounded text-xs"
                        style={{ backgroundColor: `${info.color}20`, color: info.color }}
                      >
                        {info.name}
                      </span>
                    ) : null
                  })}
                  {indicators.length === 0 && (
                    <span className="text-xs text-slate-500">No indicators selected</span>
                  )}
                </div>
              </div>
            </div>
          )}
        </div>

        {/* Status Bar */}
        <footer className="h-10 flex items-center justify-between px-4 bg-slate-950/30 border-t border-slate-700/50 text-xs">
          <div className="flex items-center gap-4">
            <span className="flex items-center gap-1.5">
              <span className={`w-2 h-2 rounded-full ${isConnected ? 'bg-green-400' : 'bg-yellow-400'} animate-pulse`}></span>
              <span className="text-slate-400">{isConnected ? 'Connected' : 'Connecting...'}</span>
            </span>
            <span className="text-slate-500">Symbol: {symbol}/USDT</span>
            <span className="text-slate-500">Interval: {interval}</span>
            {showFootprint && (
              <>
                <span className="text-amber-400">Footprint: ON</span>
                {footprintPOC && (
                  <span className="text-slate-500">POC: ${footprintPOC.price?.toFixed(2)}</span>
                )}
                {footprintVA && (
                  <span className="text-slate-500">VA: {footprintVA.low?.toFixed(2)}-{footprintVA.high?.toFixed(2)}</span>
                )}
                {footprintImbalances.length > 0 && (
                  <span className="text-purple-400">Imbalances: {footprintImbalances.length}</span>
                )}
              </>
            )}
            {showDOM && (
              <span className="text-purple-400">DOM: ON</span>
            )}
          </div>
          <div className="flex items-center gap-4">
            <span className="text-slate-500">WebView Ready</span>
          </div>
        </footer>
      </main>

      {/* Alert Panel Modal */}
      <AlertPanel 
        isOpen={showAlertPanel}
        onClose={() => setShowAlertPanel(false)}
        onAlertTrigger={handleAlertTrigger}
      />

      {/* Settings Modal */}
      <Settings
        isOpen={showSettings}
        onClose={() => setShowSettings(false)}
        logs={logs}
      />

      {/* Alert Notifications */}
      {notifications.map((notification, index) => (
        <AlertNotification 
          key={`${notification.timestamp}-${index}`}
          alert={notification}
          onClose={() => handleRemoveNotification(index)}
        />
      ))}
    </div>
  )
}

export default App
