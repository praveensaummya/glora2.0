import { useEffect, useRef, useState, useCallback } from 'react'
import { createChart, ColorType, CrosshairMode } from 'lightweight-charts'
import { useMarketData } from '../hooks/useIPC'
import { 
  calculateSMA, 
  calculateEMA, 
  calculateBollingerBands, 
  INDICATOR_COLORS
} from '../utils/indicators'

export default function Chart({ 
  theme = 'dark', 
  symbol = 'BTCUSDT', 
  interval: initialInterval = '1m',
  indicators = [],
  onIntervalChange
}) {
  const chartContainerRef = useRef(null)
  const chartRef = useRef(null)
  const candlestickSeriesRef = useRef(null)
  const volumeSeriesRef = useRef(null)
  const lastCandleRef = useRef(null)
  const [interval, setInterval] = useState(initialInterval)
  
  // Indicator series refs
  const indicatorSeriesRefs = useRef({})
  
  // Get market data from IPC
  const { candles, latestCandle, error } = useMarketData(symbol, interval)
  
  // Determine states from data availability
  const isLoading = !candles || candles.length === 0
  const hasError = !!error

  // Handle interval change
  const handleIntervalChange = useCallback((newInterval) => {
    setInterval(newInterval)
    if (onIntervalChange) {
      onIntervalChange(newInterval)
    }
  }, [onIntervalChange])
  
  // Theme colors
  const isDark = theme === 'dark'
  const backgroundColor = isDark ? '#0f172a' : '#ffffff'
  const textColor = isDark ? '#94a3b8' : '#475569'
  const gridColor = isDark ? '#1e293b' : '#e2e8f0'
  
  // Create chart
  const initChart = useCallback(() => {
    if (!chartContainerRef.current) return
    
    // Remove existing chart
    if (chartRef.current) {
      chartRef.current.remove()
    }
    
    const chart = createChart(chartContainerRef.current, {
      layout: {
        background: { type: ColorType.Solid, color: backgroundColor },
        textColor: textColor,
        fontFamily: "'JetBrains Mono', monospace",
      },
      grid: {
        vertLines: { color: gridColor, style: 1 },
        horzLines: { color: gridColor, style: 1 },
      },
      crosshair: {
        mode: CrosshairMode.Normal,
        vertLine: {
          color: isDark ? '#06b6d4' : '#0891b2',
          width: 1,
          style: 2,
          labelBackgroundColor: isDark ? '#06b6d4' : '#0891b2',
        },
        horzLine: {
          color: isDark ? '#06b6d4' : '#0891b2',
          width: 1,
          style: 2,
          labelBackgroundColor: isDark ? '#06b6d4' : '#0891b2',
        },
      },
      rightPriceScale: {
        borderColor: gridColor,
        scaleMargins: {
          top: 0.1,
          bottom: 0.2,
        },
      },
      timeScale: {
        borderColor: gridColor,
        timeVisible: true,
        secondsVisible: false,
      },
      handleScale: {
        axisPressedMouseMove: true,
      },
      handleScroll: {
        vertTouchDrag: true,
      },
    })
    
    chartRef.current = chart
    
    // Add candlestick series
    const candlestickSeries = chart.addCandlestickSeries({
      upColor: '#22c55e',
      downColor: '#ef4444',
      borderUpColor: '#22c55e',
      borderDownColor: '#ef4444',
      wickUpColor: '#22c55e',
      wickDownColor: '#ef4444',
    })
    candlestickSeriesRef.current = candlestickSeries
    
    // Add volume histogram
    const volumeSeries = chart.addHistogramSeries({
      priceFormat: {
        type: 'volume',
      },
      priceScaleId: '',
      scaleMargins: {
        top: 0.8,
        bottom: 0,
      },
    })
    volumeSeriesRef.current = volumeSeries
    
    return chart
  }, [backgroundColor, textColor, gridColor, isDark])
  
  // Add indicator series
  const addIndicatorSeries = useCallback((indicatorName, data, options = {}) => {
    if (!chartRef.current || !data || data.length === 0) return
    
    const colors = INDICATOR_COLORS
    let series
    
    switch (indicatorName) {
      case 'sma20':
        series = chartRef.current.addLineSeries({
          color: colors.sma20,
          lineWidth: 2,
          priceLineVisible: false,
          lastValueVisible: false,
          ...options
        })
        break
      case 'sma50':
        series = chartRef.current.addLineSeries({
          color: colors.sma50,
          lineWidth: 2,
          priceLineVisible: false,
          lastValueVisible: false,
          ...options
        })
        break
      case 'sma200':
        series = chartRef.current.addLineSeries({
          color: colors.sma200,
          lineWidth: 2,
          priceLineVisible: false,
          lastValueVisible: false,
          ...options
        })
        break
      case 'ema12':
        series = chartRef.current.addLineSeries({
          color: colors.ema12,
          lineWidth: 2,
          priceLineVisible: false,
          lastValueVisible: false,
          ...options
        })
        break
      case 'ema26':
        series = chartRef.current.addLineSeries({
          color: colors.ema26,
          lineWidth: 2,
          priceLineVisible: false,
          lastValueVisible: false,
          ...options
        })
        break
      case 'bbUpper':
        series = chartRef.current.addLineSeries({
          color: colors.bollingerUpper,
          lineWidth: 1,
          lineStyle: 2,
          priceLineVisible: false,
          lastValueVisible: false,
          ...options
        })
        break
      case 'bbMiddle':
        series = chartRef.current.addLineSeries({
          color: colors.bollingerMiddle,
          lineWidth: 1,
          priceLineVisible: false,
          lastValueVisible: false,
          ...options
        })
        break
      case 'bbLower':
        series = chartRef.current.addLineSeries({
          color: colors.bollingerLower,
          lineWidth: 1,
          lineStyle: 2,
          priceLineVisible: false,
          lastValueVisible: false,
          ...options
        })
        break
      default:
        return
    }
    
    if (series) {
      series.setData(data)
      indicatorSeriesRefs.current[indicatorName] = series
    }
  }, [])
  
  // Update indicators based on candle data
  const updateIndicators = useCallback((candleData) => {
    if (!candleData || candleData.length === 0) return
    
    // Remove existing indicator series
    Object.values(indicatorSeriesRefs.current).forEach(series => {
      if (series && chartRef.current) {
        chartRef.current.removeSeries(series)
      }
    })
    indicatorSeriesRefs.current = {}
    
    // Calculate and add indicators
    indicators.forEach(indicator => {
      switch (indicator) {
        case 'sma20': {
          const sma20 = calculateSMA(candleData, 20)
          addIndicatorSeries('sma20', sma20)
          break
        }
        case 'sma50': {
          const sma50 = calculateSMA(candleData, 50)
          addIndicatorSeries('sma50', sma50)
          break
        }
        case 'sma200': {
          const sma200 = calculateSMA(candleData, 200)
          addIndicatorSeries('sma200', sma200)
          break
        }
        case 'ema12': {
          const ema12 = calculateEMA(candleData, 12)
          addIndicatorSeries('ema12', ema12)
          break
        }
        case 'ema26': {
          const ema26 = calculateEMA(candleData, 26)
          addIndicatorSeries('ema26', ema26)
          break
        }
        case 'bollinger': {
          const bb = calculateBollingerBands(candleData, 20, 2)
          addIndicatorSeries('bbUpper', bb.upper)
          addIndicatorSeries('bbMiddle', bb.middle)
          addIndicatorSeries('bbLower', bb.lower)
          break
        }
        case 'rsi':
        case 'atr':
          // These would be added as separate panes in full implementation
          break
      }
    })
  }, [indicators, addIndicatorSeries])
  
  // Initialize chart
  useEffect(() => {
    initChart()
    
    // Handle resize
    const handleResize = () => {
      if (chartContainerRef.current && chartRef.current) {
        chartRef.current.applyOptions({
          width: chartContainerRef.current.clientWidth,
          height: chartContainerRef.current.clientHeight,
        })
      }
    }
    
    // Initial resize
    handleResize()
    
    // Add resize observer
    const resizeObserver = new ResizeObserver(handleResize)
    if (chartContainerRef.current) {
      resizeObserver.observe(chartContainerRef.current)
    }
    
    // Cleanup
    return () => {
      resizeObserver.disconnect()
      if (chartRef.current) {
        chartRef.current.remove()
        chartRef.current = null
      }
    }
  }, [initChart])
  
  // Update theme
  useEffect(() => {
    if (!chartRef.current) return
    
    chartRef.current.applyOptions({
      layout: {
        background: { type: ColorType.Solid, color: backgroundColor },
        textColor: textColor,
      },
      grid: {
        vertLines: { color: gridColor },
        horzLines: { color: gridColor },
      },
      rightPriceScale: {
        borderColor: gridColor,
      },
      timeScale: {
        borderColor: gridColor,
      },
    })
  }, [theme, backgroundColor, textColor, gridColor])
  
  // Handle data loading
  useEffect(() => {
    if (!candlestickSeriesRef.current || !volumeSeriesRef.current) return
    
    let candleData
    let volumeData
    
    if (candles && candles.length > 0) {
      // Use real data from IPC
      candleData = candles.map(c => ({
        time: c.time,
        open: c.open,
        high: c.high,
        low: c.low,
        close: c.close,
      }))
      volumeData = candles.map(c => ({
        time: c.time,
        value: c.volume || 0,
        color: c.close >= c.open ? 'rgba(34, 197, 94, 0.5)' : 'rgba(239, 68, 68, 0.5)',
      }))
    } else {
      // No data available - clear chart and show loading state
      candleData = []
      volumeData = []
    }
    
    if (candleData && candleData.length > 0) {
      candlestickSeriesRef.current.setData(candleData)
      volumeSeriesRef.current.setData(volumeData)
      lastCandleRef.current = candleData[candleData.length - 1]
      
      // Update indicators
      updateIndicators(candleData)
      
      // Fit content
      chartRef.current?.timeScale().fitContent()
    } else {
      // Clear chart when no data
      candlestickSeriesRef.current.setData([])
      volumeSeriesRef.current.setData([])
    }
  }, [candles, error, interval, indicators, updateIndicators])
  
  // Handle real-time updates from IPC
  useEffect(() => {
    if (!latestCandle || !candlestickSeriesRef.current) return
    
    // Update chart with latest candle from IPC
    candlestickSeriesRef.current.update({
      time: latestCandle.time,
      open: latestCandle.open,
      high: latestCandle.high,
      low: latestCandle.low,
      close: latestCandle.close,
    })
    
    // Update volume
    if (volumeSeriesRef.current) {
      volumeSeriesRef.current.update({
        time: latestCandle.time,
        value: latestCandle.volume || 0,
        color: latestCandle.close >= latestCandle.open ? 
          'rgba(34, 197, 94, 0.5)' : 'rgba(239, 68, 68, 0.5)',
      })
    }
    
    lastCandleRef.current = latestCandle
    
    // Update indicators with new data point
    if (candles && candles.length > 0) {
      const candleData = candles.map(c => ({
        time: c.time,
        open: c.open,
        high: c.high,
        low: c.low,
        close: c.close,
      }))
      candleData.push({
        time: latestCandle.time,
        open: latestCandle.open,
        high: latestCandle.high,
        low: latestCandle.low,
        close: latestCandle.close,
      })
      updateIndicators(candleData)
    }
  }, [latestCandle, candles, updateIndicators])
  
  // Show loading state when no data
  if (isLoading) {
    return (
      <div className="w-full h-full flex flex-col items-center justify-center bg-slate-900/50">
        <div className="flex flex-col items-center gap-4">
          <div className="w-12 h-12 border-4 border-cyan-500/30 border-t-cyan-500 rounded-full animate-spin"></div>
          <div className="text-center">
            <p className="text-cyan-400 font-medium">Connecting to backend...</p>
            <p className="text-slate-500 text-sm mt-1">Waiting for market data from {symbol}</p>
          </div>
        </div>
      </div>
    )
  }
  
  // Show error state
  if (hasError) {
    return (
      <div className="w-full h-full flex flex-col items-center justify-center bg-slate-900/50">
        <div className="flex flex-col items-center gap-4">
          <div className="w-12 h-12 rounded-full bg-red-500/20 flex items-center justify-center">
            <span className="text-red-400 text-xl">!</span>
          </div>
          <div className="text-center">
            <p className="text-red-400 font-medium">Connection Error</p>
            <p className="text-slate-500 text-sm mt-1">{error || 'Failed to connect to backend'}</p>
          </div>
        </div>
      </div>
    )
  }
  
  return (
    <div 
      ref={chartContainerRef} 
      className="w-full h-full"
      style={{ minHeight: '300px' }}
    />
  )
}
