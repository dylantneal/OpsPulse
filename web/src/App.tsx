import { useState, useEffect, useRef, useCallback } from 'react'
import './App.css'

// Types matching the backend
interface Event {
  id: string
  timestamp: number
  channel: string
  level: string
  message: string
}

interface Incident {
  id: string
  created_at: number
  updated_at: number
  severity: number
  status: string
  title: string
  channel: string
  owner: string
}

interface ConnectionState {
  connected: boolean
  authenticated: boolean
  user: string
  role: string
}

type Tab = 'dashboard' | 'events' | 'incidents'

function App() {
  const [tab, setTab] = useState<Tab>('dashboard')
  const [events, setEvents] = useState<Event[]>([])
  const [incidents, setIncidents] = useState<Incident[]>([])
  const [connectionState, setConnectionState] = useState<ConnectionState>({
    connected: false,
    authenticated: false,
    user: '',
    role: ''
  })
  const [loginForm, setLoginForm] = useState({ user: 'operator', token: 'operator-secret' })
  const [newEventForm, setNewEventForm] = useState({ channel: 'trading', level: 'info', message: '' })
  const [newIncidentForm, setNewIncidentForm] = useState({ sev: 2, channel: 'trading', title: '', description: '' })
  const [notifications, setNotifications] = useState<string[]>([])
  
  const wsRef = useRef<WebSocket | null>(null)
  const reconnectTimeoutRef = useRef<number | null>(null)

  const addNotification = useCallback((message: string) => {
    setNotifications(prev => [...prev.slice(-4), message])
    setTimeout(() => {
      setNotifications(prev => prev.slice(1))
    }, 5000)
  }, [])

  const connect = useCallback(() => {
    if (wsRef.current?.readyState === WebSocket.OPEN) return

    const ws = new WebSocket('ws://localhost:8080')
    wsRef.current = ws

    ws.onopen = () => {
      setConnectionState(prev => ({ ...prev, connected: true }))
      addNotification('Connected to OpsPulse server')
    }

    ws.onclose = () => {
      setConnectionState({ connected: false, authenticated: false, user: '', role: '' })
      addNotification('Disconnected from server')
      
      // Auto-reconnect after 3 seconds
      reconnectTimeoutRef.current = window.setTimeout(() => {
        connect()
      }, 3000)
    }

    ws.onerror = () => {
      addNotification('Connection error')
    }

    ws.onmessage = (event) => {
      try {
        const msg = JSON.parse(event.data)
        handleMessage(msg)
      } catch (e) {
        console.error('Failed to parse message:', e)
      }
    }
  }, [addNotification])

  const handleMessage = useCallback((msg: { type: string; payload: Record<string, unknown> }) => {
    switch (msg.type) {
      case 'welcome':
        addNotification('Welcome to OpsPulse WebSocket API')
        break
        
      case 'auth_response':
        if (msg.payload.success) {
          setConnectionState(prev => ({
            ...prev,
            authenticated: true,
            user: loginForm.user,
            role: msg.payload.role as string
          }))
          addNotification(`Authenticated as ${loginForm.user} (${msg.payload.role})`)
          // Subscribe to all channels and request data
          sendMessage({ type: 'subscribe', payload: { channels: ['*'] } })
          sendMessage({ type: 'list_events', payload: { limit: 50 } })
          sendMessage({ type: 'list_incidents', payload: {} })
        } else {
          addNotification(`Authentication failed: ${msg.payload.error}`)
        }
        break
        
      case 'events':
        setEvents((msg.payload.events as Event[]) || [])
        break
        
      case 'incidents':
        setIncidents((msg.payload.incidents as Incident[]) || [])
        break
        
      case 'push_event':
        setEvents(prev => [msg.payload as unknown as Event, ...prev.slice(0, 99)])
        addNotification(`New event: ${(msg.payload as Event).message}`)
        break
        
      case 'push_incident':
        setIncidents(prev => {
          const existing = prev.find(i => i.id === (msg.payload as Incident).id)
          if (existing) {
            return prev.map(i => i.id === (msg.payload as Incident).id ? msg.payload as unknown as Incident : i)
          }
          return [msg.payload as unknown as Incident, ...prev]
        })
        addNotification(`New incident: ${(msg.payload as Incident).title}`)
        break
        
      case 'push_incident_update':
        setIncidents(prev => prev.map(i => {
          if (i.id === msg.payload.id) {
            return { ...i, [msg.payload.field as string]: msg.payload.new_value }
          }
          return i
        }))
        addNotification(`Incident ${msg.payload.id} updated: ${msg.payload.field}`)
        // Refresh incidents list
        sendMessage({ type: 'list_incidents', payload: {} })
        break
        
      case 'ack':
        addNotification(msg.payload.message as string)
        break
        
      case 'error':
        addNotification(`Error: ${msg.payload.message}`)
        break
    }
  }, [addNotification, loginForm.user])

  const sendMessage = useCallback((msg: object) => {
    if (wsRef.current?.readyState === WebSocket.OPEN) {
      wsRef.current.send(JSON.stringify(msg))
    }
  }, [])

  const authenticate = () => {
    sendMessage({
      type: 'auth',
      payload: { user: loginForm.user, token: loginForm.token }
    })
  }

  const createEvent = () => {
    if (!newEventForm.message.trim()) return
    sendMessage({
      type: 'event',
      payload: newEventForm
    })
    setNewEventForm(prev => ({ ...prev, message: '' }))
    // Refresh events after a short delay
    setTimeout(() => sendMessage({ type: 'list_events', payload: { limit: 50 } }), 100)
  }

  const createIncident = () => {
    if (!newIncidentForm.title.trim()) return
    sendMessage({
      type: 'incident_create',
      payload: newIncidentForm
    })
    setNewIncidentForm(prev => ({ ...prev, title: '', description: '' }))
    // Refresh incidents after a short delay
    setTimeout(() => sendMessage({ type: 'list_incidents', payload: {} }), 100)
  }

  const updateIncidentStatus = (id: string, status: string) => {
    sendMessage({
      type: 'incident_update',
      payload: { id, status }
    })
    setTimeout(() => sendMessage({ type: 'list_incidents', payload: {} }), 100)
  }

  useEffect(() => {
    connect()
    return () => {
      if (reconnectTimeoutRef.current) {
        clearTimeout(reconnectTimeoutRef.current)
      }
      wsRef.current?.close()
    }
  }, [connect])

  const formatTime = (ts: number) => {
    return new Date(ts).toLocaleString()
  }

  const getLevelColor = (level: string) => {
    switch (level) {
      case 'critical': return '#ff4757'
      case 'error': return '#ff6b81'
      case 'warn': return '#ffa502'
      case 'info': return '#3742fa'
      case 'debug': return '#747d8c'
      default: return '#57606f'
    }
  }

  const getSeverityColor = (sev: number) => {
    switch (sev) {
      case 1: return '#ff4757'
      case 2: return '#ff6b81'
      case 3: return '#ffa502'
      case 4: return '#2ed573'
      case 5: return '#57606f'
      default: return '#57606f'
    }
  }

  const getStatusColor = (status: string) => {
    switch (status) {
      case 'OPEN': return '#ff4757'
      case 'ACKED': return '#ffa502'
      case 'INVESTIGATING': return '#3742fa'
      case 'MITIGATED': return '#1e90ff'
      case 'RESOLVED': return '#2ed573'
      case 'CLOSED': return '#57606f'
      default: return '#57606f'
    }
  }

  const openIncidents = incidents.filter(i => !['RESOLVED', 'CLOSED'].includes(i.status))
  const criticalIncidents = incidents.filter(i => i.severity <= 2 && !['RESOLVED', 'CLOSED'].includes(i.status))

  return (
    <div className="app">
      {/* Header */}
      <header className="header">
        <div className="header-left">
          <h1 className="logo">
            <span className="logo-ops">Ops</span>
            <span className="logo-pulse">Pulse</span>
          </h1>
          <span className="tagline">Real-Time Incident & Ops Console</span>
        </div>
        <div className="header-right">
          <div className={`connection-status ${connectionState.connected ? 'connected' : 'disconnected'}`}>
            <span className="status-dot"></span>
            {connectionState.connected ? 'Connected' : 'Disconnected'}
          </div>
          {connectionState.authenticated && (
            <div className="user-info">
              <span className="user-name">{connectionState.user}</span>
              <span className="user-role">{connectionState.role}</span>
            </div>
          )}
        </div>
      </header>

      {/* Notifications */}
      <div className="notifications">
        {notifications.map((n, i) => (
          <div key={i} className="notification">{n}</div>
        ))}
      </div>

      {/* Main Content */}
      <main className="main">
        {!connectionState.authenticated ? (
          <div className="login-container">
            <div className="login-card">
              <h2>Authentication Required</h2>
              <p>Enter your credentials to access the OpsPulse dashboard</p>
              <div className="form-group">
                <label>Username</label>
                <input
                  type="text"
                  value={loginForm.user}
                  onChange={(e) => setLoginForm(prev => ({ ...prev, user: e.target.value }))}
                  placeholder="Username"
                />
              </div>
              <div className="form-group">
                <label>Token</label>
                <input
                  type="password"
                  value={loginForm.token}
                  onChange={(e) => setLoginForm(prev => ({ ...prev, token: e.target.value }))}
                  placeholder="API Token"
                />
              </div>
              <button className="btn btn-primary" onClick={authenticate} disabled={!connectionState.connected}>
                {connectionState.connected ? 'Authenticate' : 'Connecting...'}
              </button>
              <div className="login-hint">
                <p>Default credentials:</p>
                <code>admin / admin-secret</code><br/>
                <code>operator / operator-secret</code><br/>
                <code>viewer / viewer-secret</code>
              </div>
            </div>
          </div>
        ) : (
          <>
            {/* Navigation Tabs */}
            <nav className="tabs">
              <button 
                className={`tab ${tab === 'dashboard' ? 'active' : ''}`}
                onClick={() => setTab('dashboard')}
              >
                Dashboard
              </button>
              <button 
                className={`tab ${tab === 'events' ? 'active' : ''}`}
                onClick={() => setTab('events')}
              >
                Events ({events.length})
              </button>
              <button 
                className={`tab ${tab === 'incidents' ? 'active' : ''}`}
                onClick={() => setTab('incidents')}
              >
                Incidents ({incidents.length})
              </button>
            </nav>

            {/* Tab Content */}
            <div className="tab-content">
              {tab === 'dashboard' && (
                <div className="dashboard">
                  {/* Stats Cards */}
                  <div className="stats-grid">
                    <div className="stat-card">
                      <div className="stat-value">{events.length}</div>
                      <div className="stat-label">Total Events</div>
                    </div>
                    <div className="stat-card">
                      <div className="stat-value">{incidents.length}</div>
                      <div className="stat-label">Total Incidents</div>
                    </div>
                    <div className="stat-card warning">
                      <div className="stat-value">{openIncidents.length}</div>
                      <div className="stat-label">Open Incidents</div>
                    </div>
                    <div className="stat-card danger">
                      <div className="stat-value">{criticalIncidents.length}</div>
                      <div className="stat-label">Critical (SEV1-2)</div>
                    </div>
                  </div>

                  {/* Quick Actions */}
                  <div className="quick-actions">
                    <div className="action-card">
                      <h3>Quick Event</h3>
                      <div className="inline-form">
                        <select 
                          value={newEventForm.channel}
                          onChange={(e) => setNewEventForm(prev => ({ ...prev, channel: e.target.value }))}
                        >
                          <option value="trading">trading</option>
                          <option value="infra">infra</option>
                          <option value="risk">risk</option>
                          <option value="market-data">market-data</option>
                        </select>
                        <select
                          value={newEventForm.level}
                          onChange={(e) => setNewEventForm(prev => ({ ...prev, level: e.target.value }))}
                        >
                          <option value="debug">debug</option>
                          <option value="info">info</option>
                          <option value="warn">warn</option>
                          <option value="error">error</option>
                          <option value="critical">critical</option>
                        </select>
                        <input
                          type="text"
                          placeholder="Event message..."
                          value={newEventForm.message}
                          onChange={(e) => setNewEventForm(prev => ({ ...prev, message: e.target.value }))}
                          onKeyDown={(e) => e.key === 'Enter' && createEvent()}
                        />
                        <button className="btn btn-primary" onClick={createEvent}>Send</button>
                      </div>
                    </div>

                    <div className="action-card">
                      <h3>Create Incident</h3>
                      <div className="inline-form">
                        <select
                          value={newIncidentForm.sev}
                          onChange={(e) => setNewIncidentForm(prev => ({ ...prev, sev: parseInt(e.target.value) }))}
                        >
                          <option value={1}>SEV1</option>
                          <option value={2}>SEV2</option>
                          <option value={3}>SEV3</option>
                          <option value={4}>SEV4</option>
                          <option value={5}>SEV5</option>
                        </select>
                        <select
                          value={newIncidentForm.channel}
                          onChange={(e) => setNewIncidentForm(prev => ({ ...prev, channel: e.target.value }))}
                        >
                          <option value="trading">trading</option>
                          <option value="infra">infra</option>
                          <option value="risk">risk</option>
                          <option value="market-data">market-data</option>
                        </select>
                        <input
                          type="text"
                          placeholder="Incident title..."
                          value={newIncidentForm.title}
                          onChange={(e) => setNewIncidentForm(prev => ({ ...prev, title: e.target.value }))}
                          onKeyDown={(e) => e.key === 'Enter' && createIncident()}
                        />
                        <button className="btn btn-danger" onClick={createIncident}>Create</button>
                      </div>
                    </div>
                  </div>

                  {/* Recent Activity */}
                  <div className="recent-grid">
                    <div className="recent-card">
                      <h3>Recent Events</h3>
                      <div className="event-list compact">
                        {events.slice(0, 5).map(e => (
                          <div key={e.id} className="event-item">
                            <span className="event-level" style={{ background: getLevelColor(e.level) }}>
                              {e.level}
                            </span>
                            <span className="event-channel">{e.channel}</span>
                            <span className="event-message">{e.message}</span>
                          </div>
                        ))}
                        {events.length === 0 && <div className="empty">No events yet</div>}
                      </div>
                    </div>

                    <div className="recent-card">
                      <h3>Open Incidents</h3>
                      <div className="incident-list compact">
                        {openIncidents.slice(0, 5).map(i => (
                          <div key={i.id} className="incident-item">
                            <span className="incident-sev" style={{ background: getSeverityColor(i.severity) }}>
                              SEV{i.severity}
                            </span>
                            <span className="incident-id">{i.id}</span>
                            <span className="incident-title">{i.title}</span>
                            <span className="incident-status" style={{ color: getStatusColor(i.status) }}>
                              {i.status}
                            </span>
                          </div>
                        ))}
                        {openIncidents.length === 0 && <div className="empty">No open incidents</div>}
                      </div>
                    </div>
                  </div>
                </div>
              )}

              {tab === 'events' && (
                <div className="events-view">
                  <div className="view-header">
                    <h2>Events</h2>
                    <div className="inline-form">
                      <select 
                        value={newEventForm.channel}
                        onChange={(e) => setNewEventForm(prev => ({ ...prev, channel: e.target.value }))}
                      >
                        <option value="trading">trading</option>
                        <option value="infra">infra</option>
                        <option value="risk">risk</option>
                        <option value="market-data">market-data</option>
                      </select>
                      <select
                        value={newEventForm.level}
                        onChange={(e) => setNewEventForm(prev => ({ ...prev, level: e.target.value }))}
                      >
                        <option value="debug">debug</option>
                        <option value="info">info</option>
                        <option value="warn">warn</option>
                        <option value="error">error</option>
                        <option value="critical">critical</option>
                      </select>
                      <input
                        type="text"
                        placeholder="Event message..."
                        value={newEventForm.message}
                        onChange={(e) => setNewEventForm(prev => ({ ...prev, message: e.target.value }))}
                        onKeyDown={(e) => e.key === 'Enter' && createEvent()}
                      />
                      <button className="btn btn-primary" onClick={createEvent}>Send Event</button>
                    </div>
                  </div>
                  <div className="event-list full">
                    {events.map(e => (
                      <div key={e.id} className="event-row">
                        <span className="event-time">{formatTime(e.timestamp)}</span>
                        <span className="event-level" style={{ background: getLevelColor(e.level) }}>
                          {e.level}
                        </span>
                        <span className="event-channel">{e.channel}</span>
                        <span className="event-message">{e.message}</span>
                        <span className="event-id">{e.id}</span>
                      </div>
                    ))}
                    {events.length === 0 && <div className="empty">No events recorded</div>}
                  </div>
                </div>
              )}

              {tab === 'incidents' && (
                <div className="incidents-view">
                  <div className="view-header">
                    <h2>Incidents</h2>
                    <div className="inline-form">
                      <select
                        value={newIncidentForm.sev}
                        onChange={(e) => setNewIncidentForm(prev => ({ ...prev, sev: parseInt(e.target.value) }))}
                      >
                        <option value={1}>SEV1</option>
                        <option value={2}>SEV2</option>
                        <option value={3}>SEV3</option>
                        <option value={4}>SEV4</option>
                        <option value={5}>SEV5</option>
                      </select>
                      <select
                        value={newIncidentForm.channel}
                        onChange={(e) => setNewIncidentForm(prev => ({ ...prev, channel: e.target.value }))}
                      >
                        <option value="trading">trading</option>
                        <option value="infra">infra</option>
                        <option value="risk">risk</option>
                        <option value="market-data">market-data</option>
                      </select>
                      <input
                        type="text"
                        placeholder="Incident title..."
                        value={newIncidentForm.title}
                        onChange={(e) => setNewIncidentForm(prev => ({ ...prev, title: e.target.value }))}
                        onKeyDown={(e) => e.key === 'Enter' && createIncident()}
                      />
                      <button className="btn btn-danger" onClick={createIncident}>Create Incident</button>
                    </div>
                  </div>
                  <div className="incident-table">
                    <div className="table-header">
                      <span>ID</span>
                      <span>SEV</span>
                      <span>Status</span>
                      <span>Channel</span>
                      <span>Title</span>
                      <span>Owner</span>
                      <span>Created</span>
                      <span>Actions</span>
                    </div>
                    {incidents.map(i => (
                      <div key={i.id} className="table-row">
                        <span className="incident-id">{i.id}</span>
                        <span>
                          <span className="incident-sev" style={{ background: getSeverityColor(i.severity) }}>
                            SEV{i.severity}
                          </span>
                        </span>
                        <span>
                          <span className="incident-status-badge" style={{ background: getStatusColor(i.status) }}>
                            {i.status}
                          </span>
                        </span>
                        <span className="incident-channel">{i.channel}</span>
                        <span className="incident-title">{i.title}</span>
                        <span className="incident-owner">{i.owner || '-'}</span>
                        <span className="incident-time">{formatTime(i.created_at)}</span>
                        <span className="incident-actions">
                          {i.status === 'OPEN' && (
                            <button className="btn btn-sm" onClick={() => updateIncidentStatus(i.id, 'ACKED')}>
                              ACK
                            </button>
                          )}
                          {i.status === 'ACKED' && (
                            <button className="btn btn-sm" onClick={() => updateIncidentStatus(i.id, 'INVESTIGATING')}>
                              Investigate
                            </button>
                          )}
                          {i.status === 'INVESTIGATING' && (
                            <button className="btn btn-sm" onClick={() => updateIncidentStatus(i.id, 'MITIGATED')}>
                              Mitigate
                            </button>
                          )}
                          {i.status === 'MITIGATED' && (
                            <button className="btn btn-sm btn-success" onClick={() => updateIncidentStatus(i.id, 'RESOLVED')}>
                              Resolve
                            </button>
                          )}
                          {i.status === 'RESOLVED' && (
                            <button className="btn btn-sm" onClick={() => updateIncidentStatus(i.id, 'CLOSED')}>
                              Close
                            </button>
                          )}
                        </span>
                      </div>
                    ))}
                    {incidents.length === 0 && <div className="empty">No incidents recorded</div>}
                  </div>
                </div>
              )}
            </div>
          </>
        )}
      </main>
    </div>
  )
}

export default App
