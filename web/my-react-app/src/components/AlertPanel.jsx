/**
 * Alert Configuration Panel Component
 * 
 * Provides UI for:
 * - Configuring alert types
 * - Viewing alert history
 * - Managing notification settings
 */

import { useState, useEffect, useCallback } from 'react';
import { 
  AlertType, 
  DEFAULT_ALERT_CONFIGS, 
  alertManager, 
  loadAlertConfigs, 
  saveAlertConfigs 
} from '../utils/alerts';

const AlertPanel = ({ onAlertTrigger, isOpen, onClose }) => {
  const [alertConfigs, setAlertConfigs] = useState(() => loadAlertConfigs());
  const [alertHistory, setAlertHistory] = useState(() => alertManager.getHistory());
  const [activeTab, setActiveTab] = useState('config');
  const [soundEnabled, setSoundEnabled] = useState(true);
  const [preferences, setPreferences] = useState({
    soundEnabled: true,
    visualEnabled: true,
    debounceDelay: 1000,
    maxHistory: 100,
    showNotifications: true
  });

  // Subscribe to alert manager
  useEffect(() => {
    const unsubscribe = alertManager.subscribe((alert) => {
      setAlertHistory(prev => [alert, ...prev].slice(0, 50));
      if (onAlertTrigger) {
        onAlertTrigger(alert);
      }
    });
    
    return () => unsubscribe();
  }, [onAlertTrigger]);

  // Save configs when changed
  const handleConfigChange = useCallback((alertType, updates) => {
    setAlertConfigs(prev => {
      const newConfig = {
        ...prev,
        [alertType]: {
          ...prev[alertType],
          ...updates
        }
      };
      saveAlertConfigs(newConfig);
      return newConfig;
    });
  }, []);

  // Toggle alert enabled
  const handleToggleAlert = useCallback((alertType) => {
    const config = alertConfigs[alertType];
    handleConfigChange(alertType, { enabled: !config.enabled });
  }, [alertConfigs, handleConfigChange]);

  // Update alert params
  const handleParamChange = useCallback((alertType, param, value) => {
    const config = alertConfigs[alertType];
    handleConfigChange(alertType, {
      params: {
        ...config.params,
        [param]: value
      }
    });
  }, [alertConfigs, handleConfigChange]);

  // Clear history
  const handleClearHistory = useCallback(() => {
    alertManager.clearHistory();
    setAlertHistory([]);
  }, []);

  // Format timestamp
  const formatTime = (timestamp) => {
    const date = new Date(timestamp);
    return date.toLocaleTimeString();
  };

  // Get priority color
  const getPriorityColor = (priority) => {
    switch (priority) {
      case 'high': return 'text-red-400';
      case 'medium': return 'text-yellow-400';
      default: return 'text-blue-400';
    }
  };

  // Get alert type name
  const getAlertTypeName = (type) => {
    const config = alertConfigs[type];
    return config?.name || type;
  };

  if (!isOpen) return null;

  return (
    <div className="fixed inset-0 z-50 flex items-center justify-center bg-black/50">
      <div className="bg-slate-900 rounded-xl w-full max-w-2xl max-h-[80vh] overflow-hidden border border-slate-700">
        {/* Header */}
        <div className="flex items-center justify-between px-4 py-3 border-b border-slate-700">
          <h2 className="text-lg font-semibold text-white">Alert Configuration</h2>
          <button 
            onClick={onClose}
            className="text-slate-400 hover:text-white"
          >
            ✕
          </button>
        </div>

        {/* Tabs */}
        <div className="flex border-b border-slate-700">
          {['config', 'history', 'settings'].map(tab => (
            <button
              key={tab}
              onClick={() => setActiveTab(tab)}
              className={`px-4 py-2 text-sm font-medium transition-colors ${
                activeTab === tab 
                  ? 'text-cyan-400 border-b-2 border-cyan-400' 
                  : 'text-slate-400 hover:text-white'
              }`}
            >
              {tab.charAt(0).toUpperCase() + tab.slice(1)}
            </button>
          ))}
        </div>

        {/* Content */}
        <div className="p-4 overflow-y-auto max-h-[60vh]">
          {activeTab === 'config' && (
            <div className="space-y-3">
              {Object.entries(alertConfigs).map(([type, config]) => (
                <div 
                  key={type}
                  className="bg-slate-800 rounded-lg p-3"
                >
                  <div className="flex items-center justify-between mb-2">
                    <div className="flex items-center gap-2">
                      <input
                        type="checkbox"
                        checked={config.enabled}
                        onChange={() => handleToggleAlert(type)}
                        className="w-4 h-4 rounded bg-slate-700 border-slate-600"
                      />
                      <span className="text-white font-medium">{config.name}</span>
                    </div>
                    <span className="text-xs text-slate-500">{config.description}</span>
                  </div>
                  
                  {config.enabled && (
                    <div className="ml-6 mt-2 space-y-2">
                      {Object.entries(config.params).map(([param, value]) => (
                        <div key={param} className="flex items-center justify-between text-sm">
                          <span className="text-slate-400 capitalize">
                            {param.replace(/([A-Z])/g, ' $1').trim()}
                          </span>
                          <input
                            type="number"
                            value={value}
                            onChange={(e) => handleParamChange(type, param, parseFloat(e.target.value))}
                            className="w-20 bg-slate-700 border border-slate-600 rounded px-2 py-1 text-white text-right"
                          />
                        </div>
                      ))}
                    </div>
                  )}
                </div>
              ))}
            </div>
          )}

          {activeTab === 'history' && (
            <div>
              {alertHistory.length > 0 && (
                <button
                  onClick={handleClearHistory}
                  className="mb-3 text-sm text-slate-400 hover:text-white"
                >
                  Clear History
                </button>
              )}
              
              {alertHistory.length === 0 ? (
                <p className="text-slate-500 text-center py-8">No alerts triggered yet</p>
              ) : (
                <div className="space-y-2">
                  {alertHistory.map((alert, index) => (
                    <div 
                      key={index}
                      className="bg-slate-800 rounded-lg p-3 flex items-start justify-between"
                    >
                      <div>
                        <span className={`text-xs font-medium ${getPriorityColor(alert.priority)}`}>
                          {alert.priority?.toUpperCase()}
                        </span>
                        <p className="text-white text-sm">{alert.message}</p>
                        <p className="text-slate-500 text-xs mt-1">
                          {getAlertTypeName(alert.type)}
                        </p>
                      </div>
                      <span className="text-slate-500 text-xs">
                        {formatTime(alert.timestamp)}
                      </span>
                    </div>
                  ))}
                </div>
              )}
            </div>
          )}

          {activeTab === 'settings' && (
            <div className="space-y-4">
              <div className="flex items-center justify-between">
                <span className="text-white">Sound Alerts</span>
                <input
                  type="checkbox"
                  checked={soundEnabled}
                  onChange={(e) => setSoundEnabled(e.target.checked)}
                  className="w-4 h-4 rounded bg-slate-700 border-slate-600"
                />
              </div>
              
              <div className="flex items-center justify-between">
                <span className="text-white">Debounce Delay (ms)</span>
                <input
                  type="number"
                  value={preferences.debounceDelay}
                  onChange={(e) => setPreferences(prev => ({ ...prev, debounceDelay: parseInt(e.target.value) }))}
                  className="w-24 bg-slate-700 border border-slate-600 rounded px-2 py-1 text-white text-right"
                />
              </div>
              
              <div className="flex items-center justify-between">
                <span className="text-white">Max History</span>
                <input
                  type="number"
                  value={preferences.maxHistory}
                  onChange={(e) => setPreferences(prev => ({ ...prev, maxHistory: parseInt(e.target.value) }))}
                  className="w-24 bg-slate-700 border border-slate-600 rounded px-2 py-1 text-white text-right"
                />
              </div>

              <div className="pt-4 border-t border-slate-700">
                <button
                  onClick={alertManager.resetSession.bind(alertManager)}
                  className="w-full bg-red-600/20 text-red-400 py-2 rounded-lg hover:bg-red-600/30 transition-colors"
                >
                  Reset Session
                </button>
              </div>
            </div>
          )}
        </div>
      </div>
    </div>
  );
};

// Alert Notification Toast Component
export const AlertNotification = ({ alert, onClose }) => {
  const [isVisible, setIsVisible] = useState(true);

  useEffect(() => {
    const timer = setTimeout(() => {
      setIsVisible(false);
      if (onClose) onClose();
    }, 5000);
    
    return () => clearTimeout(timer);
  }, [onClose]);

  if (!isVisible) return null;

  const priorityColors = {
    high: 'border-red-500 bg-red-500/10',
    medium: 'border-yellow-500 bg-yellow-500/10',
    low: 'border-blue-500 bg-blue-500/10'
  };

  return (
    <div className={`fixed top-4 right-4 z-50 animate-slide-in ${priorityColors[alert.priority] || priorityColors.low} border-l-4 rounded-lg p-4 shadow-lg max-w-sm`}>
      <div className="flex items-start justify-between gap-2">
        <div>
          <p className="text-white font-medium text-sm">{alert.message}</p>
          <p className="text-slate-400 text-xs mt-1">
            {alert.type} • {new Date(alert.timestamp).toLocaleTimeString()}
          </p>
        </div>
        <button 
          onClick={() => { setIsVisible(false); if (onClose) onClose(); }}
          className="text-slate-400 hover:text-white"
        >
          ✕
        </button>
      </div>
    </div>
  );
};

export default AlertPanel;
