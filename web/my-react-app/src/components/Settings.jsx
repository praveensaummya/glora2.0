/**
 * Settings.jsx - Settings Panel for API Configuration
 * 
 * Allows users to save their Binance API credentials which are
 * stored in the local SQLite database and used on next login.
 */

import { useState, useEffect } from 'react';
import backendWS from '../utils/BackendWebSocket';

export default function Settings({ isOpen, onClose }) {
  const [apiKey, setApiKey] = useState('');
  const [apiSecret, setApiSecret] = useState('');
  const [useTestnet, setUseTestnet] = useState(false);
  const [hasCredentials, setHasCredentials] = useState(false);
  const [loading, setLoading] = useState(false);
  const [message, setMessage] = useState(null);
  const [messageType, setMessageType] = useState(null);

  const loadCredentials = () => {
    setLoading(true);
    backendWS.send({ type: 'loadCredentials' });
  };

  // Load credentials on mount
  useEffect(() => {
    if (isOpen) {
      loadCredentials();
    }
  }, [isOpen]);

  // Handle messages from backend
  useEffect(() => {
    const handleMessage = (data) => {
      if (data.type === 'credentialsLoaded') {
        setLoading(false);
        setHasCredentials(data.hasCredentials);
        if (data.hasCredentials) {
          setApiKey(data.apiKey || '');
          setApiSecret(data.apiSecret || '');
          setUseTestnet(data.useTestnet || false);
        } else {
          setApiKey('');
          setApiSecret('');
          setUseTestnet(false);
        }
      } else if (data.type === 'credentialsSaved') {
        setLoading(false);
        setHasCredentials(true);
        setMessage('API credentials saved successfully!');
        setMessageType('success');
        setTimeout(() => setMessage(null), 3000);
      } else if (data.type === 'credentialsDeleted') {
        setLoading(false);
        setHasCredentials(false);
        setApiKey('');
        setApiSecret('');
        setUseTestnet(false);
        setMessage('API credentials deleted.');
        setMessageType('success');
        setTimeout(() => setMessage(null), 3000);
      } else if (data.type === 'error') {
        setLoading(false);
        setMessage(data.error || 'An error occurred');
        setMessageType('error');
        setTimeout(() => setMessage(null), 3000);
      }
    };

    backendWS.onMessage(handleMessage);
    return () => {
      // Cleanup listener
    };
  }, []);

  const handleSave = () => {
    if (!apiKey.trim() || !apiSecret.trim()) {
      setMessage('Please enter both API Key and API Secret');
      setMessageType('error');
      setTimeout(() => setMessage(null), 3000);
      return;
    }

    setLoading(true);
    backendWS.send({
      type: 'saveCredentials',
      apiKey: apiKey.trim(),
      apiSecret: apiSecret.trim(),
      useTestnet
    });
  };

  const handleDelete = () => {
    if (!hasCredentials) return;
    
    if (window.confirm('Are you sure you want to delete your API credentials?')) {
      setLoading(true);
      backendWS.send({ type: 'deleteCredentials' });
    }
  };

  const handleTestConnection = () => {
    setLoading(true);
    backendWS.send({ 
      type: 'getStatus'
    });
  };

  if (!isOpen) return null;

  return (
    <div className="fixed inset-0 bg-black bg-opacity-50 flex items-center justify-center z-50">
      <div className="bg-gray-800 rounded-lg p-6 w-full max-w-md mx-4">
        <div className="flex justify-between items-center mb-4">
          <h2 className="text-xl font-bold text-white">Settings</h2>
          <button 
            onClick={onClose}
            className="text-gray-400 hover:text-white"
          >
            ✕
          </button>
        </div>

        {/* Status Message */}
        {message && (
          <div className={`mb-4 p-3 rounded ${
            messageType === 'success' ? 'bg-green-900 text-green-200' : 'bg-red-900 text-red-200'
          }`}>
            {message}
          </div>
        )}

        {/* API Credentials Section */}
        <div className="mb-6">
          <h3 className="text-lg font-semibold text-white mb-3">Binance API</h3>
          <p className="text-sm text-gray-400 mb-4">
            Enter your Binance API credentials to enable authenticated requests.
            These are stored locally in SQLite and used on your next login.
          </p>

          <div className="space-y-4">
            <div>
              <label className="block text-sm text-gray-400 mb-1">API Key</label>
              <input
                type="text"
                value={apiKey}
                onChange={(e) => setApiKey(e.target.value)}
                placeholder="Enter your API key"
                className="w-full bg-gray-700 text-white px-3 py-2 rounded focus:outline-none focus:ring-2 focus:ring-blue-500"
              />
            </div>

            <div>
              <label className="block text-sm text-gray-400 mb-1">API Secret</label>
              <input
                type="password"
                value={apiSecret}
                onChange={(e) => setApiSecret(e.target.value)}
                placeholder="Enter your API secret"
                className="w-full bg-gray-700 text-white px-3 py-2 rounded focus:outline-none focus:ring-2 focus:ring-blue-500"
              />
            </div>

            <div className="flex items-center">
              <input
                type="checkbox"
                id="testnet"
                checked={useTestnet}
                onChange={(e) => setUseTestnet(e.target.checked)}
                className="mr-2"
              />
              <label htmlFor="testnet" className="text-sm text-gray-400">
                Use Testnet (for testing)
              </label>
            </div>
          </div>
        </div>

        {/* Action Buttons */}
        <div className="flex flex-wrap gap-3">
          <button
            onClick={handleSave}
            disabled={loading}
            className="flex-1 bg-blue-600 hover:bg-blue-700 text-white px-4 py-2 rounded disabled:opacity-50"
          >
            {loading ? 'Saving...' : 'Save Credentials'}
          </button>

          {hasCredentials && (
            <button
              onClick={handleDelete}
              disabled={loading}
              className="bg-red-600 hover:bg-red-700 text-white px-4 py-2 rounded disabled:opacity-50"
            >
              Delete
            </button>
          )}

          <button
            onClick={handleTestConnection}
            disabled={loading}
            className="bg-gray-600 hover:bg-gray-700 text-white px-4 py-2 rounded disabled:opacity-50"
          >
            Test
          </button>
        </div>

        {/* Info */}
        <div className="mt-4 p-3 bg-gray-900 rounded text-sm text-gray-400">
          <p>Your API credentials are:</p>
          <ul className="list-disc list-inside mt-1">
            <li>Stored locally in SQLite database</li>
            <li>Used automatically when you reconnect</li>
            <li>Never transmitted to any server except Binance</li>
          </ul>
        </div>
      </div>
    </div>
  );
}
