/**
 * useChartWorker.js - Hook for Web Worker with OffscreenCanvas
 * 
 * This hook manages the chart rendering worker for high-performance
 * background rendering using OffscreenCanvas.
 */

import { useState, useEffect, useRef, useCallback } from 'react';
import chartWorker from '../workers/chartWorker?worker';

// Message ID generator
let messageId = 0;
const generateId = () => ++messageId;

/**
 * useChartWorker - Hook for Web Worker chart rendering
 * 
 * @param {Object} options - Configuration options
 */
export function useChartWorker(options = {}) {
  const [isReady, setIsReady] = useState(false);
  const [isRendering, setIsRendering] = useState(false);
  const [metrics, setMetrics] = useState(null);
  const [error, setError] = useState(null);
  
  const workerRef = useRef(null);
  const canvasRef = useRef(null);
  const pendingMessages = useRef(new Map());
  const renderQueue = useRef([]);
  const isProcessing = useRef(false);

  // Initialize worker
  useEffect(() => {
    const worker = new chartWorker();
    workerRef.current = worker;
    
    const handleMessage = (event) => {
      const { type, id, data } = event.data;
      
      // Handle response
      if (id && pendingMessages.current.has(id)) {
        const { resolve, reject } = pendingMessages.current.get(id);
        pendingMessages.current.delete(id);
        
        if (type === 'error') {
          reject(new Error(data.error));
        } else {
          resolve(data);
        }
      }
      
      // Handle events
      switch (type) {
        case 'initialized':
          console.log('[ChartWorker] Initialized');
          setIsReady(true);
          break;
          
        case 'ready':
          console.log('[ChartWorker] Canvas ready');
          break;
          
        case 'rendered':
          setIsRendering(false);
          if (data) {
            setMetrics(prev => ({
              ...prev,
              fps: data.fps,
              lastRenderTime: data.renderTime,
              candlesRendered: data.candlesRendered
            }));
          }
          break;
          
        case 'indicators':
          // Handle calculated indicators
          break;
          
        case 'error':
          console.error('[ChartWorker] Error:', data);
          setError(data.error || 'Unknown error');
          break;
      }
    };
    
    worker.addEventListener('message', handleMessage);
    
    return () => {
      worker.removeEventListener('message', handleMessage);
      worker.terminate();
      workerRef.current = null;
    };
  }, []);

  /**
   * Initialize OffscreenCanvas
   */
  const initCanvas = useCallback((canvas, width, height) => {
    if (!workerRef.current || !canvas) return Promise.reject('Worker not ready');
    
    canvasRef.current = canvas;
    
    // Transfer canvas to worker
    const offscreen = canvas.transferControlToOffscreen();
    
    return new Promise((resolve, reject) => {
      const id = generateId();
      pendingMessages.current.set(id, { resolve, reject });
      
      workerRef.current.postMessage({
        type: 'init',
        id,
        data: {
          canvas: offscreen,
          width,
          height,
          dpr: window.devicePixelRatio || 1
        }
      });
    });
  }, []);

  /**
   * Render chart
   */
  const render = useCallback((data) => {
    if (!workerRef.current || !isReady) return;
    
    setIsRendering(true);
    
    return new Promise((resolve, reject) => {
      const id = generateId();
      pendingMessages.current.set(id, { resolve, reject });
      
      workerRef.current.postMessage({
        type: 'render',
        id,
        data
      });
    });
  }, [isReady]);

  /**
   * Calculate indicators
   */
  const calculateIndicators = useCallback((candles, indicatorTypes) => {
    if (!workerRef.current || !isReady) return Promise.reject('Worker not ready');
    
    return new Promise((resolve, reject) => {
      const id = generateId();
      pendingMessages.current.set(id, { resolve, reject });
      
      workerRef.current.postMessage({
        type: 'calculateIndicators',
        id,
        data: {
          candles,
          indicators: indicatorTypes
        }
      });
    });
  }, [isReady]);

  /**
   * Send data to worker
   */
  const setData = useCallback((data) => {
    if (!workerRef.current || !isReady) return Promise.reject('Worker not ready');
    
    return new Promise((resolve, reject) => {
      const id = generateId();
      pendingMessages.current.set(id, { resolve, reject });
      
      workerRef.current.postMessage({
        type: 'setData',
        id,
        data
      });
    });
  }, [isReady]);

  /**
   * Resize canvas
   */
  const resize = useCallback((width, height) => {
    if (!workerRef.current || !isReady) return Promise.reject('Worker not ready');
    
    return new Promise((resolve, reject) => {
      const id = generateId();
      pendingMessages.current.set(id, { resolve, reject });
      
      workerRef.current.postMessage({
        type: 'resize',
        id,
        data: {
          width,
          height,
          dpr: window.devicePixelRatio || 1
        }
      });
    });
  }, [isReady]);

  /**
   * Get performance metrics
   */
  const getMetrics = useCallback(() => {
    if (!workerRef.current || !isReady) return Promise.reject('Worker not ready');
    
    return new Promise((resolve, reject) => {
      const id = generateId();
      pendingMessages.current.set(id, { resolve, reject });
      
      workerRef.current.postMessage({
        type: 'getMetrics',
        id
      });
    });
  }, [isReady]);

  /**
   * Process batch of data
   */
  const processBatch = useCallback((batch) => {
    if (!workerRef.current || !isReady) return Promise.reject('Worker not ready');
    
    return new Promise((resolve, reject) => {
      const id = generateId();
      pendingMessages.current.set(id, { resolve, reject });
      
      workerRef.current.postMessage({
        type: 'processBatch',
        id,
        data: batch
      });
    });
  }, [isReady]);

  return {
    isReady,
    isRendering,
    metrics,
    error,
    initCanvas,
    render,
    calculateIndicators,
    setData,
    resize,
    getMetrics,
    processBatch
  };
}

/**
 * useOffscreenCanvas - Hook for canvas element with OffscreenCanvas transfer
 * 
 * @param {HTMLCanvasElement} canvasRef - Ref to canvas element
 * @param {Object} chartWorker - Chart worker instance
 */
export function useOffscreenCanvas(canvasRef, chartWorker) {
  const [isTransferred, setIsTransferred] = useState(false);
  const [error, setError] = useState(null);

  useEffect(() => {
    if (!canvasRef.current || !chartWorker) return;
    
    const canvas = canvasRef.current;
    
    try {
      // Check for OffscreenCanvas support
      if (!canvas.transferControlToOffscreen) {
        setError(new Error('OffscreenCanvas not supported'));
        return;
      }
      
      const initPromise = chartWorker.initCanvas(
        canvas,
        canvas.clientWidth * window.devicePixelRatio,
        canvas.clientHeight * window.devicePixelRatio,
        window.devicePixelRatio
      );
      
      initPromise
        .then(() => setIsTransferred(true))
        .catch(err => setError(err));
        
    } catch (err) {
      setError(err);
    }
    
    // Handle resize
    const handleResize = () => {
      if (chartWorker && isTransferred) {
        chartWorker.resize(
          canvas.clientWidth * window.devicePixelRatio,
          canvas.clientHeight * window.devicePixelRatio,
          window.devicePixelRatio
        );
      }
    };
    
    window.addEventListener('resize', handleResize);
    
    return () => {
      window.removeEventListener('resize', handleResize);
    };
  }, [canvasRef, chartWorker, isTransferred]);

  return {
    isTransferred,
    error
  };
}

export default useChartWorker;
