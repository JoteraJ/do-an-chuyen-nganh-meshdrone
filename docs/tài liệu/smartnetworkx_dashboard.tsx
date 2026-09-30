import React, { useState, useEffect, useRef } from 'react';

// --- ICONS (Inline SVGs) ---
const IconRoot = () => (
  <svg xmlns="http://www.w3.org/2000/svg" width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round" className="text-emerald-400"><path d="M5 22h14"/><path d="M5 2h14"/><path d="M12 2v20"/><path d="M8 6h8"/><path d="M8 18h8"/></svg>
);
const IconRobot = () => (
  <svg xmlns="http://www.w3.org/2000/svg" width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round" className="text-blue-400"><rect x="3" y="11" width="18" height="10" rx="2"/><circle cx="12" cy="5" r="2"/><path d="M12 7v4"/><line x1="8" y1="16" x2="8" y2="16"/><line x1="16" y1="16" x2="16" y2="16"/></svg>
);
const IconDrone = () => (
  <svg xmlns="http://www.w3.org/2000/svg" width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round" className="text-purple-400"><path d="M12 2v4"/><path d="M12 18v4"/><path d="M2 12h4"/><path d="M18 12h4"/><circle cx="12" cy="12" r="4"/><path d="m14.8 9.2 2.8-2.8"/><path d="m6.4 17.6 2.8-2.8"/><path d="m6.4 6.4 2.8 2.8"/><path d="m14.8 14.8 2.8 2.8"/></svg>
);
const IconSignal = ({ ping }) => {
  let color = "text-emerald-400";
  if (ping > 80) color = "text-yellow-400";
  if (ping > 150) color = "text-red-400";
  if (ping === -1) color = "text-slate-600"; // Offline
  return (
    <svg xmlns="http://www.w3.org/2000/svg" width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round" className={color}><path d="M2 20h.01"/><path d="M7 20v-4"/><path d="M12 20v-8"/><path d="M17 20V8"/><path d="M22 4v16"/></svg>
  );
};

export default function MeshDashboard() {
  // --- STATES ---
  const [nodes, setNodes] = useState([
    { id: 'Root-Gateway', name: 'Gateway (Base)', parent: null, type: 'Root', ping: 2, battery: 100, active: true },
    { id: 'Robot-1', name: 'UGV-Relay-Alpha', parent: 'Root-Gateway', type: 'Robot', ping: 24, battery: 85, active: true },
    { id: 'Robot-2', name: 'UGV-Relay-Beta', parent: 'Root-Gateway', type: 'Robot', ping: 35, battery: 92, active: true },
    { id: 'Drone-1', name: 'UAV-Eye-Sky', parent: 'Robot-1', type: 'Drone', ping: 68, battery: 45, active: true },
  ]);
  
  const [metrics, setMetrics] = useState({ throughput: 120, latency: 45, packetLoss: 0.5 });
  const [logs, setLogs] = useState([{ time: new Date().toLocaleTimeString(), msg: 'Mesh Network Initialized on Port 5555' }]);
  const [frameCount, setFrameCount] = useState(0);

  // Helper to add logs
  const addLog = (msg) => {
    setLogs(prev => [{ time: new Date().toLocaleTimeString(), msg }, ...prev].slice(0, 8));
  };

  useEffect(() => {
    // 1. Simulate Motion JPEG Frames (approx 4-5 fps)
    const frameInterval = setInterval(() => {
      setFrameCount(prev => prev + 1);
    }, 250);

    // 2. Simulate Telemetry & Network Fluctuations
    const telemetryInterval = setInterval(() => {
      setMetrics({
        throughput: Math.floor(80 + Math.random() * 60), // 80 - 140 Kbps
        latency: Math.floor(40 + Math.random() * 30),    // 40 - 70 ms
        packetLoss: (Math.random() * 2).toFixed(1)       // 0.0 - 2.0 %
      });

      // Fluctuate Ping
      setNodes(prevNodes => prevNodes.map(node => {
        if (node.id === 'Root-Gateway' || !node.active) return node;
        let pingVarience = (Math.random() > 0.5 ? 1 : -1) * Math.floor(Math.random() * 10);
        return { ...node, ping: Math.max(10, node.ping + pingVarience) };
      }));
    }, 1000);

    // 3. Simulate Self-Healing Event (Robot-1 dies, Drone-1 moves to Robot-2)
    const healingTimeout1 = setTimeout(() => {
      addLog("CRITICAL: Robot-1 signal lost! Disconnected.");
      setNodes(prev => prev.map(n => n.id === 'Robot-1' ? { ...n, active: false, ping: -1 } : n));
      setNodes(prev => prev.map(n => n.id === 'Drone-1' ? { ...n, active: false, ping: -1, parent: null } : n)); // Drone lost parent
    }, 8000);

    const healingTimeout2 = setTimeout(() => {
      addLog("HEALING: Drone-1 scanning for new parent...");
    }, 11000);

    const healingTimeout3 = setTimeout(() => {
      addLog("SUCCESS: Drone-1 reconnected via Robot-2. Routing table updated.");
      setNodes(prev => prev.map(n => n.id === 'Drone-1' ? { ...n, active: true, ping: 85, parent: 'Robot-2' } : n));
    }, 14000);

    return () => {
      clearInterval(frameInterval);
      clearInterval(telemetryInterval);
      clearTimeout(healingTimeout1);
      clearTimeout(healingTimeout2);
      clearTimeout(healingTimeout3);
    };
  }, []);

  // Recursive function to render Tree
  const renderTree = (parentId, depth = 0) => {
    const children = nodes.filter(n => n.parent === parentId);
    if (children.length === 0) return null;

    return (
      <div className="flex flex-col space-y-2 w-full">
        {children.map(node => (
          <div key={node.id} className="flex flex-col relative">
            {/* Connection Line */}
            {depth > 0 && (
              <div className="absolute -left-4 top-5 w-4 border-t-2 border-slate-600 border-dashed"></div>
            )}
            
            <div className={`flex items-center p-3 rounded-lg border ${node.active ? 'bg-slate-800 border-slate-700' : 'bg-red-900/20 border-red-800/50 opacity-70'} shadow-sm transition-all duration-300`}>
              <div className="mr-3">
                {node.type === 'Root' && <IconRoot />}
                {node.type === 'Robot' && <IconRobot />}
                {node.type === 'Drone' && <IconDrone />}
              </div>
              <div className="flex-1">
                <div className="flex justify-between items-center">
                  <h4 className={`text-sm font-semibold ${node.active ? 'text-slate-200' : 'text-red-400'}`}>{node.name}</h4>
                  <span className="flex items-center gap-1 text-xs text-slate-400">
                    <IconSignal ping={node.ping} /> {node.ping > -1 ? `${node.ping}ms` : 'ERR'}
                  </span>
                </div>
                <div className="flex gap-3 text-xs mt-1 text-slate-500">
                  <span>MAC: {node.id.substring(0,6)}...</span>
                  <span>Bat: {node.battery}%</span>
                  <span className={node.active ? 'text-emerald-500' : 'text-red-500'}>{node.active ? 'Online' : 'Offline'}</span>
                </div>
              </div>
            </div>
            {/* Render Children Recursively */}
            <div className="ml-8 mt-2 border-l-2 border-slate-700 border-dashed pl-4">
              {renderTree(node.id, depth + 1)}
            </div>
          </div>
        ))}
      </div>
    );
  };

  return (
    <div className="min-h-screen bg-slate-950 text-slate-200 font-sans p-4 sm:p-6 lg:p-8">
      {/* HEADER */}
      <header className="flex justify-between items-center mb-6 pb-4 border-b border-slate-800">
        <div>
          <h1 className="text-2xl font-bold text-white tracking-tight">SmartNetworkX <span className="text-blue-500 font-light">Dashboard</span></h1>
          <p className="text-sm text-slate-400 mt-1">Mạng Ad-hoc phi tập trung: Drone & UGV</p>
        </div>
        <div className="flex items-center gap-2 px-4 py-2 bg-emerald-900/30 text-emerald-400 border border-emerald-800 rounded-full text-sm font-medium">
          <div className="w-2 h-2 rounded-full bg-emerald-400 animate-pulse"></div>
          System Live
        </div>
      </header>

      <div className="grid grid-cols-1 lg:grid-cols-12 gap-6">
        
        {/* LEFT PANEL: TOPOLOGY TREE */}
        <div className="lg:col-span-4 flex flex-col gap-6">
          <div className="bg-slate-900 border border-slate-800 rounded-xl p-5 shadow-xl">
            <h2 className="text-lg font-semibold text-white mb-4 flex items-center gap-2">
              <svg xmlns="http://www.w3.org/2000/svg" width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round" className="text-blue-400"><path d="M21 16V8a2 2 0 0 0-1-1.73l-7-4a2 2 0 0 0-2 0l-7 4A2 2 0 0 0 3 8v8a2 2 0 0 0 1 1.73l7 4a2 2 0 0 0 2 0l7-4A2 2 0 0 0 21 16z"/><polyline points="3.27 6.96 12 12.01 20.73 6.96"/><line x1="12" y1="22.08" x2="12" y2="12"/></svg>
              Mesh Topology
            </h2>
            <div className="w-full">
              {renderTree(null)}
            </div>
          </div>

          {/* TELEMETRY METRICS */}
          <div className="bg-slate-900 border border-slate-800 rounded-xl p-5 shadow-xl">
            <h2 className="text-lg font-semibold text-white mb-4">Mạng lưới Metrics</h2>
            <div className="grid grid-cols-2 gap-4">
              <div className="bg-slate-800 p-3 rounded-lg border border-slate-700">
                <div className="text-slate-400 text-xs mb-1">Băng thông (Throughput)</div>
                <div className="text-xl font-bold text-blue-400">{metrics.throughput} <span className="text-sm font-normal text-slate-500">Kbps</span></div>
              </div>
              <div className="bg-slate-800 p-3 rounded-lg border border-slate-700">
                <div className="text-slate-400 text-xs mb-1">Độ trễ trung bình (RTT)</div>
                <div className="text-xl font-bold text-emerald-400">{metrics.latency} <span className="text-sm font-normal text-slate-500">ms</span></div>
              </div>
              <div className="bg-slate-800 p-3 rounded-lg border border-slate-700 col-span-2 flex justify-between items-center">
                <div>
                  <div className="text-slate-400 text-xs mb-1">Packet Loss</div>
                  <div className={`text-xl font-bold ${metrics.packetLoss > 1 ? 'text-yellow-400' : 'text-emerald-400'}`}>{metrics.packetLoss}%</div>
                </div>
                <div className="w-1/2 bg-slate-700 rounded-full h-2">
                  <div className="bg-blue-500 h-2 rounded-full transition-all duration-300" style={{ width: `${Math.min(100, metrics.throughput / 1.5)}%` }}></div>
                </div>
              </div>
            </div>
          </div>
        </div>

        {/* MIDDLE PANEL: CAMERA FEEDS */}
        <div className="lg:col-span-8 flex flex-col gap-6">
          
          <div className="bg-slate-900 border border-slate-800 rounded-xl overflow-hidden shadow-xl">
            <div className="p-4 border-b border-slate-800 flex justify-between items-center bg-slate-900/50">
              <h2 className="text-lg font-semibold text-white flex items-center gap-2">
                <svg xmlns="http://www.w3.org/2000/svg" width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round" className="text-purple-400"><path d="M14.5 4h-5L7 7H4a2 2 0 0 0-2 2v9a2 2 0 0 0 2 2h16a2 2 0 0 0 2-2V9a2 2 0 0 0-2-2h-3l-2.5-3z"/><circle cx="12" cy="13" r="3"/></svg>
                Media Stream (Motion JPEG)
              </h2>
              <div className="text-xs text-slate-400 bg-slate-800 px-2 py-1 rounded">~ 4 FPS via Mesh</div>
            </div>
            
            <div className="grid grid-cols-1 md:grid-cols-2 gap-1 p-1 bg-slate-950">
              {/* Drone Camera Simulation */}
              <div className="relative aspect-video bg-black rounded group">
                <div className="absolute top-2 left-2 z-10 bg-black/60 text-white text-xs px-2 py-1 rounded backdrop-blur font-mono border border-white/10">
                  UAV-Eye-Sky (Drone-1)
                </div>
                {nodes.find(n => n.id === 'Drone-1')?.active ? (
                  <>
                    <img 
                      src={`https://placehold.co/640x360/1e293b/a78bfa?text=Drone+Camera+Feed\nFrame:+${frameCount}`} 
                      alt="Drone Feed" 
                      className="w-full h-full object-cover opacity-90 group-hover:opacity-100 transition-opacity"
                    />
                    <div className="absolute bottom-2 right-2 text-xs text-green-400 font-mono bg-black/50 px-1 rounded">REC [●]</div>
                  </>
                ) : (
                  <div className="w-full h-full flex flex-col items-center justify-center text-red-500 font-mono border-2 border-red-900/50 border-dashed bg-red-950/20">
                    <svg xmlns="http://www.w3.org/2000/svg" width="32" height="32" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round" className="mb-2 opacity-50"><path d="m2 2 20 20"/><path d="m15 9.34-1.83-1.83a2 2 0 0 0-2.83 0l-1.83 1.83a2 2 0 0 0 0 2.83L10.34 14"/><path d="m15 15 2.12-2.12a2 2 0 0 0 0-2.83L15 7.93"/></svg>
                    SIGNAL LOST
                  </div>
                )}
              </div>

              {/* Robot Camera Simulation */}
              <div className="relative aspect-video bg-black rounded group">
                <div className="absolute top-2 left-2 z-10 bg-black/60 text-white text-xs px-2 py-1 rounded backdrop-blur font-mono border border-white/10">
                  UGV-Alpha (Robot-1)
                </div>
                {nodes.find(n => n.id === 'Robot-1')?.active ? (
                  <img 
                    src={`https://placehold.co/640x360/1e293b/60a5fa?text=Robot+Front+Cam\nFrame:+${Math.floor(frameCount * 0.8)}`} 
                    alt="Robot Feed" 
                    className="w-full h-full object-cover opacity-90 group-hover:opacity-100 transition-opacity"
                  />
                ) : (
                  <div className="w-full h-full flex flex-col items-center justify-center text-red-500 font-mono border-2 border-red-900/50 border-dashed bg-red-950/20">
                    SIGNAL LOST
                  </div>
                )}
              </div>
            </div>
          </div>

          {/* EVENT LOGS (Terminal Style) */}
          <div className="bg-slate-900 border border-slate-800 rounded-xl overflow-hidden shadow-xl flex-1 flex flex-col">
            <div className="p-3 border-b border-slate-800 bg-slate-900/80 flex items-center gap-2">
              <svg xmlns="http://www.w3.org/2000/svg" width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round" className="text-slate-400"><polyline points="4 17 10 11 4 5"/><line x1="12" y1="19" x2="20" y2="19"/></svg>
              <h3 className="text-sm font-mono text-slate-300">System Logs & Routing Events</h3>
            </div>
            <div className="p-4 bg-slate-950 flex-1 overflow-y-auto font-mono text-xs space-y-2 h-48 max-h-60 border-t-4 border-slate-900 inset-shadow">
              {logs.map((log, i) => (
                <div key={i} className="flex gap-3 items-start border-b border-slate-800/50 pb-1">
                  <span className="text-slate-500 whitespace-nowrap">[{log.time}]</span>
                  <span className={`${
                    log.msg.includes('CRITICAL') ? 'text-red-400 font-bold' : 
                    log.msg.includes('SUCCESS') ? 'text-emerald-400' : 
                    log.msg.includes('HEALING') ? 'text-yellow-400' : 'text-slate-300'
                  }`}>
                    {log.msg}
                  </span>
                </div>
              ))}
              <div className="animate-pulse text-slate-600 mt-2">_</div>
            </div>
          </div>

        </div>
      </div>
    </div>
  );
}