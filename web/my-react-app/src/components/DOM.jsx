import { useState, useEffect } from 'react'
import { FontAwesomeIcon } from '@fortawesome/react-fontawesome'
import { faArrowsRotate, faChartColumn, faCaretUp } from '@fortawesome/free-solid-svg-icons'
import { formatVolume } from '../utils/orderFlow'

const DEPTH_OPTIONS = [
  { value: 10, label: '10' },
  { value: 20, label: '20' },
  { value: 50, label: '50' },
]

/**
 * DOM (Depth of Market) Component
 * Displays real-time order book with bid/ask levels
 */
function DOM({ 
  depth = 20, 
  onDepthChange,
  orderBookData,
  isLoading = false,
  lastUpdate = null
}) {
  const [bids, setBids] = useState([])
  const [asks, setAsks] = useState([])
  const [spread, setSpread] = useState({ value: 0, percent: 0 })
  const [midPrice, setMidPrice] = useState(0)
  
  // Determine if we have data
  const hasData = orderBookData && (orderBookData.bids?.length > 0 || orderBookData.asks?.length > 0)

  // Process order book data
  useEffect(() => {
    if (orderBookData) {
      // Sort bids descending (highest first)
      const sortedBids = [...orderBookData.bids || []]
        .sort((a, b) => b[1] - a[1])
        .slice(0, depth)
        .map(([price, quantity], index) => ({
          price: parseFloat(price),
          quantity: parseFloat(quantity),
          total: 0,
          percent: 0,
          position: index
        }))

      // Sort asks ascending (lowest first)
      const sortedAsks = [...orderBookData.asks || []]
        .sort((a, b) => a[1] - b[1])
        .slice(0, depth)
        .map(([price, quantity], index) => ({
          price: parseFloat(price),
          quantity: parseFloat(quantity),
          total: 0,
          percent: 0,
          position: index
        }))

      // Calculate cumulative totals for depth visualization
      let bidTotal = 0
      for (const bid of sortedBids) {
        bidTotal += bid.quantity
        bid.total = bidTotal
      }

      let askTotal = 0
      for (const ask of sortedAsks) {
        askTotal += ask.quantity
        ask.total = askTotal
      }

      // Calculate percentages for visualization
      const maxBidTotal = sortedBids.length > 0 ? sortedBids[sortedBids.length - 1].total : 1
      const maxAskTotal = sortedAsks.length > 0 ? sortedAsks[sortedAsks.length - 1].total : 1

      for (const bid of sortedBids) {
        bid.percent = (bid.total / maxBidTotal) * 100
      }

      for (const ask of sortedAsks) {
        ask.percent = (ask.total / maxAskTotal) * 100
      }

      setBids(sortedBids)
      setAsks(sortedAsks)

      // Calculate spread and mid price
      if (sortedBids.length > 0 && sortedAsks.length > 0) {
        const bestBid = sortedBids[0].price
        const bestAsk = sortedAsks[0].price
        const spreadValue = bestAsk - bestBid
        const mid = (bestBid + bestAsk) / 2
        const spreadPercent = (spreadValue / mid) * 100

        setSpread({ value: spreadValue, percent: spreadPercent })
        setMidPrice(mid)
      }
    } else {
      // No data available - clear order book
      setBids([])
      setAsks([])
      setSpread({ value: 0, percent: 0 })
      setMidPrice(0)
    }
  }, [orderBookData, depth])

  // Format price for display
  const formatPrice = (price) => {
    return price.toLocaleString(undefined, { 
      minimumFractionDigits: 2, 
      maximumFractionDigits: 2 
    })
  }

  return (
    <div className="flex flex-col h-full bg-slate-900/80 rounded-xl border border-slate-700/50 overflow-hidden">
      {/* Header */}
      <div className="flex items-center justify-between px-3 py-2 bg-slate-800/50 border-b border-slate-700/50">
        <div className="flex items-center gap-2">
          <FontAwesomeIcon icon={faChartColumn} className="text-cyan-400" />
          <span className="text-sm font-semibold text-white">Order Book</span>
        </div>
        
        <div className="flex items-center gap-2">
          {/* Depth Selector */}
          <div className="flex items-center gap-1 bg-slate-700/50 rounded-lg p-0.5">
            {DEPTH_OPTIONS.map((option) => (
              <button
                key={option.value}
                onClick={() => onDepthChange?.(option.value)}
                className={`px-2 py-0.5 text-xs font-medium rounded-md transition-colors ${
                  depth === option.value
                    ? 'bg-cyan-500 text-white'
                    : 'text-slate-400 hover:text-white'
                }`}
              >
                {option.label}
              </button>
            ))}
          </div>

          <button 
            onClick={() => {}}
            className="p-1.5 rounded-lg text-slate-400 hover:text-cyan-400 hover:bg-slate-700/50 transition-colors"
            title="Refresh"
          >
            <FontAwesomeIcon icon={faArrowsRotate} className="text-xs" />
          </button>
        </div>
      </div>

      {/* Spread Info */}
      <div className="flex items-center justify-between px-3 py-1.5 bg-slate-800/30 text-xs">
        <div className="flex items-center gap-3">
          <span className="text-slate-400">Spread:</span>
          <span className="text-amber-400 font-mono">{spread.value.toFixed(2)}</span>
          <span className="text-slate-500">({spread.percent.toFixed(3)}%)</span>
        </div>
        <div className="flex items-center gap-1">
          <FontAwesomeIcon icon={faCaretUp} className="text-green-400 text-xs" />
          <span className="text-slate-400">Mid:</span>
          <span className="text-white font-mono">${formatPrice(midPrice)}</span>
        </div>
      </div>

      {/* Column Headers */}
      <div className="grid grid-cols-3 gap-2 px-3 py-1.5 text-xs font-medium text-slate-500 border-b border-slate-700/50">
        <span>Price (USDT)</span>
        <span className="text-right">Amount</span>
        <span className="text-right">Total</span>
      </div>

      {/* Order Book Content */}
      <div className="flex-1 flex flex-col overflow-hidden">
        {!hasData ? (
          <div className="flex-1 flex flex-col items-center justify-center text-center p-4">
            <div className="w-10 h-10 border-3 border-cyan-500/30 border-t-cyan-500 rounded-full animate-spin mb-3"></div>
            <p className="text-cyan-400 font-medium text-sm">Waiting for order book...</p>
            <p className="text-slate-500 text-xs mt-1">Connect to backend for real-time data</p>
          </div>
        ) : (
          <>
            {/* Asks (Sell Orders) - Red */}
            <div className="flex-1 overflow-y-auto flex flex-col-reverse">
              {asks.map((ask, index) => (
                <div 
                  key={`ask-${index}`} 
                  className="grid grid-cols-3 gap-2 px-3 py-0.5 text-xs relative hover:bg-slate-800/30"
                >
                  {/* Depth Visualization Background */}
                  <div 
                    className="absolute inset-0 bg-red-500/10"
                    style={{ width: `${ask.percent}%`, right: 0, left: 'auto' }}
                  />
                  
                  <span className="text-red-400 font-mono relative z-10">
                    {formatPrice(ask.price)}
                  </span>
                  <span className="text-right text-slate-300 font-mono relative z-10">
                    {ask.quantity.toFixed(4)}
                  </span>
                  <span className="text-right text-slate-500 font-mono relative z-10">
                    {formatVolume(ask.total)}
                  </span>
                </div>
              ))}
            </div>

            {/* Current Price Divider */}
            <div className="px-3 py-1.5 bg-slate-800/50 border-y border-slate-700/50 flex items-center justify-center">
              <span className="text-lg font-bold text-white">
                ${formatPrice(midPrice)}
              </span>
            </div>

            {/* Bids (Buy Orders) - Green */}
            <div className="flex-1 overflow-y-auto">
              {bids.map((bid, index) => (
                <div 
                  key={`bid-${index}`} 
                  className="grid grid-cols-3 gap-2 px-3 py-0.5 text-xs relative hover:bg-slate-800/30"
                >
                  {/* Depth Visualization Background */}
                  <div 
                    className="absolute inset-0 bg-green-500/10"
                    style={{ width: `${bid.percent}%` }}
                  />
                  
                  <span className="text-green-400 font-mono relative z-10">
                    {formatPrice(bid.price)}
                  </span>
                  <span className="text-right text-slate-300 font-mono relative z-10">
                    {bid.quantity.toFixed(4)}
                  </span>
                  <span className="text-right text-slate-500 font-mono relative z-10">
                    {formatVolume(bid.total)}
                  </span>
                </div>
              ))}
            </div>
          </>
        )}
      </div>

      {/* Footer */}
      <div className="px-3 py-1.5 bg-slate-800/30 border-t border-slate-700/50 flex items-center justify-between text-xs">
        <span className="text-slate-500">
          {bids.length} bids / {asks.length} asks
        </span>
        {lastUpdate && (
          <span className="text-slate-500">
            Updated: {new Date(lastUpdate).toLocaleTimeString()}
          </span>
        )}
        {isLoading && (
          <span className="text-cyan-400 animate-pulse">Loading...</span>
        )}
      </div>
    </div>
  )
}

export default DOM
