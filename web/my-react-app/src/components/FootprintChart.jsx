import { useState, useEffect, useRef } from 'react'
import { createChart, CrosshairMode } from 'lightweight-charts'
import { FontAwesomeIcon } from '@fortawesome/react-fontawesome'
import { faBorderAll } from '@fortawesome/free-solid-svg-icons'
import { 
  calculateFootprint, 
  calculateVolumeProfile, 
  findPOC, 
  calculateValueArea,
  detectImbalances,
  calculateDelta,
  ORDER_FLOW_COLORS,
  ORDER_FLOW_CONFIG
} from '../utils/orderFlow'

/**
 * FootprintChart Component
 * Renders footprint charts with delta coloring, POC, and Value Area visualization
 * Uses TradingView Lightweight Charts with Custom Series
 */
function FootprintChart({ 
  candles = [], 
  showDelta = true,
  showPOC = true,
  showVA = true,
  valueAreaPercent = 70,
  onPOCChange,
  onValueAreaChange,
  onImbalancesFound
}) {
  const chartContainerRef = useRef(null)
  const chartRef = useRef(null)
  const candleSeriesRef = useRef(null)
  const volumeProfileSeriesRef = useRef(null)
  const pocLineRef = useRef(null)
  const vaLinesRef = useRef([])
  const [currentPOC, setCurrentPOC] = useState(null)
  const [currentVA, setCurrentVA] = useState(null)
  const [imbalances, setImbalances] = useState([])
  const [deltaData, setDeltaData] = useState([])
  
  // Determine loading state
  const isLoading = !candles || candles.length === 0

  // Initialize chart
  useEffect(() => {
    if (!chartContainerRef.current) return

    const chart = createChart(chartContainerRef.current, {
      layout: {
        background: { type: 'solid', color: '#0f172a' },
        textColor: '#94a3b8',
      },
      grid: {
        vertLines: { color: '#1e293b' },
        horzLines: { color: '#1e293b' },
      },
      crosshair: {
        mode: CrosshairMode.Normal,
        vertLine: {
          color: '#475569',
          width: 1,
          style: 2,
        },
        horzLine: {
          color: '#475569',
          width: 1,
          style: 2,
        },
      },
      rightPriceScale: {
        borderColor: '#334155',
      },
      timeScale: {
        borderColor: '#334155',
        timeVisible: true,
        secondsVisible: false,
      },
      handleScroll: true,
      handleScale: true,
    })

    // Create candlestick series
    const candleSeries = chart.addCandlestickSeries({
      upColor: '#22c55e',
      downColor: '#ef4444',
      borderUpColor: '#22c55e',
      borderDownColor: '#ef4444',
      wickUpColor: '#22c55e',
      wickDownColor: '#ef4444',
    })

    // Create volume histogram series
    const volumeSeries = chart.addHistogramSeries({
      priceFormat: {
        type: 'volume',
      },
      priceScaleId: 'volume',
    })

    chart.priceScale('volume').applyOptions({
      scaleMargins: {
        top: 0.8,
        bottom: 0,
      },
    })

    candleSeriesRef.current = candleSeries
    volumeProfileSeriesRef.current = volumeSeries
    chartRef.current = chart

    // Handle resize
    const handleResize = () => {
      if (chartContainerRef.current) {
        chart.applyOptions({ 
          width: chartContainerRef.current.clientWidth,
          height: chartContainerRef.current.clientHeight 
        })
      }
    }

    window.addEventListener('resize', handleResize)
    handleResize()

    return () => {
      window.removeEventListener('resize', handleResize)
      chart.remove()
    }
  }, [])

  // Process candle data and calculate footprint
  useEffect(() => {
    if (!candles || candles.length === 0) return

    // Calculate footprint data
    const footprint = calculateFootprint(candles)

    // Calculate volume profile
    const profile = calculateVolumeProfile(footprint)

    // Find POC
    const poc = findPOC(profile)
    setCurrentPOC(poc) // eslint-disable-line react-hooks/set-state-in-effect
    onPOCChange?.(poc)

    // Calculate Value Area
    const va = calculateValueArea(profile, valueAreaPercent)
    setCurrentVA(va)
    onValueAreaChange?.(va)

    // Detect imbalances
    const imbs = detectImbalances(footprint, ORDER_FLOW_CONFIG.imbalanceRatio)
    setImbalances(imbs)
    onImbalancesFound?.(imbs)

    // Calculate delta
    const delta = calculateDelta(footprint)
    setDeltaData(delta)

    // Update chart data
    if (candleSeriesRef.current) {
      candleSeriesRef.current.setData(candles)
    }

    // Calculate and set volume data
    if (volumeProfileSeriesRef.current) {
      const volumeData = candles.map(candle => ({
        time: candle.time,
        value: candle.volume || 0,
        color: candle.close >= candle.open ? 'rgba(34, 197, 94, 0.5)' : 'rgba(239, 68, 68, 0.5)'
      }))
      volumeProfileSeriesRef.current.setData(volumeData)
    }

    // Update POC line
    if (showPOC && poc && chartRef.current) {
      // Remove existing POC line
      if (pocLineRef.current) {
        try {
          chartRef.current.removeSeries(pocLineRef.current)
        } catch {
          // Series might not exist
        }
      }

      // Create horizontal line at POC price
      const pocLine = chartRef.current.addLineSeries({
        color: ORDER_FLOW_COLORS.poc,
        lineWidth: 2,
        lineStyle: 2, // Dashed
        priceLineVisible: false,
        lastValueVisible: false,
        crosshairMarkerVisible: false,
      })

      const pocLineData = candles.map(c => ({
        time: c.time,
        value: poc.price
      }))
      pocLine.setData(pocLineData)
      pocLineRef.current = pocLine
    }

    // Update Value Area lines
    if (showVA && va && chartRef.current) {
      // Remove existing VA lines
      vaLinesRef.current.forEach(line => {
        try {
          chartRef.current.removeSeries(line)
        } catch {
          // Ignore
        }
      })
      vaLinesRef.current = []

      // VAH line
      const vahLine = chartRef.current.addLineSeries({
        color: ORDER_FLOW_COLORS.valueArea,
        lineWidth: 1,
        lineStyle: 3, // Dotted
        priceLineVisible: false,
        lastValueVisible: false,
        crosshairMarkerVisible: false,
      })
      vahLine.setData(candles.map(c => ({ time: c.time, value: va.high })))
      vaLinesRef.current.push(vahLine)

      // VAL line
      const valLine = chartRef.current.addLineSeries({
        color: ORDER_FLOW_COLORS.valueArea,
        lineWidth: 1,
        lineStyle: 3, // Dotted
        priceLineVisible: false,
        lastValueVisible: false,
        crosshairMarkerVisible: false,
      })
      valLine.setData(candles.map(c => ({ time: c.time, value: va.low })))
      vaLinesRef.current.push(valLine)
    }

  }, [candles, showPOC, showVA, valueAreaPercent, onPOCChange, onValueAreaChange, onImbalancesFound])

  // Format data for display
  const formatPrice = (price) => {
    return price?.toLocaleString(undefined, {
      minimumFractionDigits: 2,
      maximumFractionDigits: 2
    }) || '0.00'
  }

  // Show loading state when no data
  if (isLoading) {
    return (
      <div className="w-full h-full flex flex-col items-center justify-center bg-slate-900/50">
        <div className="flex flex-col items-center gap-4">
          <div className="w-12 h-12 border-4 border-amber-500/30 border-t-amber-500 rounded-full animate-spin"></div>
          <div className="text-center">
            <p className="text-amber-400 font-medium">Connecting to backend...</p>
            <p className="text-slate-500 text-sm mt-1">Waiting for footprint data</p>
          </div>
        </div>
      </div>
    )
  }
  
  return (
    <div className="relative w-full h-full">
      {/* Footprint Stats Overlay */}
      <div className="absolute top-2 left-2 z-10 flex flex-col gap-1">
        {/* POC */}
        {currentPOC && (
          <div className="flex items-center gap-2 px-2 py-1 bg-slate-900/80 rounded-lg border border-slate-700/50">
            <div 
              className="w-2 h-2 rounded-full" 
              style={{ backgroundColor: ORDER_FLOW_COLORS.poc }}
            />
            <span className="text-xs text-slate-400">POC:</span>
            <span className="text-xs font-mono text-white">
              ${formatPrice(currentPOC.price)}
            </span>
            <span className="text-xs text-slate-500">
              ({formatPrice(currentPOC.volume)})
            </span>
          </div>
        )}

        {/* Value Area */}
        {currentVA && (
          <div className="flex items-center gap-2 px-2 py-1 bg-slate-900/80 rounded-lg border border-slate-700/50">
            <div 
              className="w-2 h-2 rounded-full" 
              style={{ backgroundColor: ORDER_FLOW_COLORS.valueArea }}
            />
            <span className="text-xs text-slate-400">VA:</span>
            <span className="text-xs font-mono text-white">
              {formatPrice(currentVA.low)} - {formatPrice(currentVA.high)}
            </span>
          </div>
        )}
      </div>

      {/* Imbalances Count */}
      {imbalances.length > 0 && (
        <div className="absolute top-2 right-2 z-10 flex items-center gap-2 px-2 py-1 bg-slate-900/80 rounded-lg border border-slate-700/50">
          <FontAwesomeIcon 
            icon={faBorderAll} 
            className="text-xs"
            style={{ color: ORDER_FLOW_COLORS.imbalance }}
          />
          <span className="text-xs text-slate-400">Imbalances:</span>
          <span className="text-xs font-mono text-white">
            {imbalances.length}
          </span>
        </div>
      )}

      {/* Delta Indicator */}
      {showDelta && deltaData.length > 0 && (
        <div className="absolute bottom-14 left-2 z-10 flex flex-col gap-1">
          {deltaData.slice(-5).map((d, i) => (
            <div 
              key={i}
              className="flex items-center gap-2 px-2 py-1 bg-slate-900/80 rounded-lg border border-slate-700/50"
            >
              <span className="text-xs text-slate-400">
                {new Date(d.time * 1000).toLocaleTimeString()}
              </span>
              <span 
                className="text-xs font-mono"
                style={{ 
                  color: d.delta >= 0 ? ORDER_FLOW_COLORS.buyDelta : ORDER_FLOW_COLORS.sellDelta 
                }}
              >
                Δ: {d.delta >= 0 ? '+' : ''}{d.delta.toFixed(2)}
              </span>
              <span 
                className="text-xs font-mono"
                style={{ 
                  color: d.cumulativeDelta >= 0 ? ORDER_FLOW_COLORS.buyDelta : ORDER_FLOW_COLORS.sellDelta 
                }}
              >
                Σ: {d.cumulativeDelta >= 0 ? '+' : ''}{d.cumulativeDelta.toFixed(2)}
              </span>
            </div>
          ))}
        </div>
      )}

      {/* Chart Container */}
      <div ref={chartContainerRef} className="w-full h-full" />

      {/* Legend */}
      <div className="absolute bottom-2 left-2 z-10 flex items-center gap-4 text-xs text-slate-500">
        <div className="flex items-center gap-1">
          <div className="w-3 h-3 rounded-sm bg-green-500/50" />
          <span>Bullish</span>
        </div>
        <div className="flex items-center gap-1">
          <div className="w-3 h-3 rounded-sm bg-red-500/50" />
          <span>Bearish</span>
        </div>
        {showPOC && (
          <div className="flex items-center gap-1">
            <div 
              className="w-3 h-0.5" 
              style={{ backgroundColor: ORDER_FLOW_COLORS.poc }}
            />
            <span>POC</span>
          </div>
        )}
        {showVA && (
          <div className="flex items-center gap-1">
            <div 
              className="w-3 h-0.5 border-t border-dotted" 
              style={{ borderColor: ORDER_FLOW_COLORS.valueArea }}
            />
            <span>VA</span>
          </div>
        )}
      </div>
    </div>
  )
}

export default FootprintChart
