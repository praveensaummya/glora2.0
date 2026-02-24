/**
 * WebSocket client for connecting to C++ backend
 * Handles real-time market data from the backend
 */

class BackendWebSocket {
  constructor(url = 'ws://localhost:8080') {
    this.url = url;
    this.ws = null;
    this.reconnectInterval = 3000;
    this.messageHandlers = [];
    this.connectionHandlers = [];
    this.isConnected = false;
  }

  connect() {
    console.log(`[BackendWS] Connecting to ${this.url}...`);
    
    try {
      this.ws = new WebSocket(this.url);
      
      this.ws.onopen = () => {
        console.log('[BackendWS] Connected to backend');
        this.isConnected = true;
        this.connectionHandlers.forEach(handler => handler(true));
      };
      
      this.ws.onclose = () => {
        console.log('[BackendWS] Disconnected from backend');
        this.isConnected = false;
        this.connectionHandlers.forEach(handler => handler(false));
        
        // Attempt to reconnect
        setTimeout(() => this.connect(), this.reconnectInterval);
      };
      
      this.ws.onerror = (error) => {
        console.error('[BackendWS] Error:', error);
      };
      
      this.ws.onmessage = (event) => {
        try {
          const data = JSON.parse(event.data);
          this.messageHandlers.forEach(handler => handler(data));
        } catch (e) {
          console.error('[BackendWS] Failed to parse message:', e);
        }
      };
    } catch (e) {
      console.error('[BackendWS] Failed to connect:', e);
      setTimeout(() => this.connect(), this.reconnectInterval);
    }
  }

  disconnect() {
    if (this.ws) {
      this.ws.close();
      this.ws = null;
    }
    this.isConnected = false;
  }

  onMessage(handler) {
    this.messageHandlers.push(handler);
  }

  onConnectionChange(handler) {
    this.connectionHandlers.push(handler);
  }

  send(message) {
    if (this.ws && this.isConnected) {
      this.ws.send(JSON.stringify(message));
    }
  }

  isConnected() {
    return this.isConnected;
  }
}

// Singleton instance
const backendWS = new BackendWebSocket();

export default backendWS;
