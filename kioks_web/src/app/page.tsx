'use client';

import React, { useEffect, useState, useRef, useCallback } from 'react';
import { ShieldAlert, ShieldCheck, HelpCircle, User, Calendar, CreditCard, Ticket } from 'lucide-react';
import type { MqttClient } from 'mqtt';

interface KioskConfig {
  mqtt_topic: string;
  music_url?: string | null;
  bg_music?: string | null;
  media_urls: string[];
}

interface ScanResult {
  status: 'valid' | 'invalid';
  name: string;
  message: string;
  rfid?: string;
  ticket_type?: string;
  valid_until?: string;
  saldo?: number;
}

// Helper diletakkan di luar komponen agar tidak memicu warning react-hooks/exhaustive-deps
const isVideoUrl = (url: string) => {
  const ext = url.split('.').pop()?.split('?')[0].toLowerCase();
  return ext && ['mp4', 'webm', 'ogg'].includes(ext);
};

export default function KioskPage() {
  const [tvId, setTvId] = useState<string | null>(null);
  const [config, setConfig] = useState<KioskConfig | null>(null);
  const [activeScan, setActiveScan] = useState<ScanResult | null>(null);
  const [currentSlideIndex, setCurrentSlideIndex] = useState(0);
  const [mqttStatus, setMqttStatus] = useState<'connecting' | 'connected' | 'disconnected' | 'error'>('disconnected');
  const [loading, setLoading] = useState<boolean>(true);
  const [errorMessage, setErrorMessage] = useState<string | null>(null);
  const [isMuted, setIsMuted] = useState<boolean>(true);
  const [showOverlay, setShowOverlay] = useState<boolean>(true);

  // Hydration-safe states
  const [adminUrl, setAdminUrl] = useState<string>('#');
  const [brokerHost, setBrokerHost] = useState<string>('10.127.10.8');

  const activeScanTimeoutRef = useRef<ReturnType<typeof setTimeout> | null>(null);
  const slideTimerRef = useRef<ReturnType<typeof setInterval> | null>(null);
  const mqttClientRef = useRef<MqttClient | null>(null);
  const videoRefs = useRef<(HTMLVideoElement | null)[]>([]);
  const audioRef = useRef<HTMLAudioElement | null>(null);

  // 1. Check LocalStorage for TV ID on Mount
  useEffect(() => {
    // Check if query params have a new TV ID to set
    const urlParams = new URLSearchParams(window.location.search);
    const queryTvId = urlParams.get('tv_id');
    const queryMqttTopic = urlParams.get('mqtt_topic');
    const queryApiUrl = urlParams.get('api_url');
    const queryMqttBroker = urlParams.get('mqtt_broker');
    
    if (queryTvId) {
      window.localStorage.setItem('tv_id', queryTvId);
      if (queryMqttTopic) window.localStorage.setItem('mqtt_topic', queryMqttTopic);
      if (queryApiUrl) window.localStorage.setItem('api_url', queryApiUrl);
      if (queryMqttBroker) window.localStorage.setItem('mqtt_broker', queryMqttBroker);
      
      // Clean query params from address bar
      window.history.replaceState({}, document.title, window.location.pathname);
    }

    const storedTvId = window.localStorage.getItem('tv_id');
    const storedApiUrl = window.localStorage.getItem('api_url') || 'http://localhost:5000';
    const storedBroker = window.localStorage.getItem('mqtt_broker') || 'ws://10.127.10.8:8083/mqtt';

    // Wrap state updates in a micro-delay (setTimeout) to avoid synchronous state update warnings (react-hooks/set-state-in-effect)
    const initTimer = setTimeout(() => {
      setAdminUrl(`${storedApiUrl}/login`);
      
      try {
        const match = storedBroker.match(/\/\/([^/]+)/);
        setBrokerHost(match ? match[1] : '10.127.10.8');
      } catch {
        setBrokerHost('10.127.10.8');
      }

      if (storedTvId) {
        setTvId(storedTvId);
      } else {
        setLoading(false);
      }
    }, 0);

    return () => clearTimeout(initTimer);
  }, []);

  // 2. Fetch Kiosk Config from Python API
  useEffect(() => {
    if (!tvId) return;

    const fetchConfig = async () => {
      setLoading(true);
      setErrorMessage(null);
      const apiUrl = window.localStorage.getItem('api_url') || 'http://localhost:5000';
      
      try {
        const res = await fetch(`${apiUrl}/api/config/${tvId}`);
        if (!res.ok) {
          throw new Error(`Kiosk dengan ID "${tvId}" tidak ditemukan di server admin.`);
        }
        const data: KioskConfig = await res.json();
        
        // Sync topic to localStorage if present
        if (data.mqtt_topic) {
          window.localStorage.setItem('mqtt_topic', data.mqtt_topic);
        }
        
        setConfig(data);
        setCurrentSlideIndex(0);
      } catch (err) {
        console.error(err);
        setErrorMessage(err instanceof Error ? err.message : 'Gagal memuat konfigurasi dari server.');
      } finally {
        setLoading(false);
      }
    };

    fetchConfig();
  }, [tvId]);

  // 4. Handle RFID Card Scan Popup with Timeout Reset
  const handleNewScan = useCallback((scan: ScanResult) => {
    // Clear existing timeout
    if (activeScanTimeoutRef.current) {
      clearTimeout(activeScanTimeoutRef.current);
    }
    setActiveScan(scan);
    // Auto dismiss after 4 seconds
    activeScanTimeoutRef.current = setTimeout(() => {
      setActiveScan(null);
    }, 4000);
  }, []);

  // 3. MQTT WebSockets Connection (Dynamic client-side load)
  useEffect(() => {
    if (!config || !config.mqtt_topic) return;

    let activeClient: MqttClient | null = null;

    const connectMqtt = async () => {
      const brokerUrl = window.localStorage.getItem('mqtt_broker') || 'ws://10.127.10.8:8083/mqtt';
      setMqttStatus('connecting');

      try {
        const mqttModule = await import('mqtt');
        const connectFn = mqttModule.connect || (mqttModule as unknown as { default?: typeof mqttModule }).default?.connect;
        if (!connectFn) {
          throw new Error('MQTT connect function not found in imported module');
        }

        const options = {
          username: 'kiosk_nextjs',
          password: '11223344',
          clientId: 'kiosk_web_' + Math.random().toString(16).substring(2, 8),
          clean: true,
          reconnectPeriod: 5000,
        };
        const client = connectFn(brokerUrl, options);

        activeClient = client;
        mqttClientRef.current = client;

        client.on('connect', () => {
          setMqttStatus('connected');
          client.subscribe(config.mqtt_topic, (err) => {
            if (err) {
              console.error('MQTT subscribe error:', err);
              setMqttStatus('error');
            } else {
              console.log(`Subscribed to topic: ${config.mqtt_topic}`);
            }
          });
        });

        client.on('message', (topic, message) => {
          if (topic === config.mqtt_topic) {
            try {
              const payload: any = JSON.parse(message.toString());
              const isAccessGranted = payload.result === 'granted' || payload.status === 'valid' || payload.status === 'success';
              const scanResult: ScanResult = {
                status: isAccessGranted ? 'valid' : 'invalid',
                name: payload.name || (isAccessGranted ? 'Member' : 'Kartu Tidak Dikenal'),
                message: payload.message || '',
                rfid: payload.rfid,
                ticket_type: payload.ticket_type,
                valid_until: payload.valid_until,
                saldo: payload.saldo
              };
              handleNewScan(scanResult);
            } catch (e) {
              console.error('Failed to parse MQTT message payload:', e);
            }
          }
        });

        client.on('close', () => {
          setMqttStatus('disconnected');
        });

        client.on('error', (err) => {
          console.error('MQTT connection error:', err);
          setMqttStatus('error');
        });

      } catch (e) {
        console.error('MQTT initialization error:', e);
        setMqttStatus('error');
      }
    };

    connectMqtt();

    return () => {
      if (activeClient) {
        activeClient.end();
      }
    };
  }, [config, handleNewScan]);

  // Menggunakan useCallback agar aman saat dimasukkan ke dalam dependency useEffect
  const nextSlide = useCallback(() => {
    if (!config || !config.media_urls || config.media_urls.length === 0) return;
    setCurrentSlideIndex((prevIndex) => (prevIndex + 1) % config.media_urls.length);
  }, [config]);

  // 5. Slideshow/Carousel Timer & End Detection
  useEffect(() => {
    if (!config || !config.media_urls || config.media_urls.length <= 1) {
      if (slideTimerRef.current) clearInterval(slideTimerRef.current);
      return;
    }

    if (slideTimerRef.current) clearInterval(slideTimerRef.current);
    
    slideTimerRef.current = setInterval(nextSlide, 5000);

    return () => {
      if (slideTimerRef.current) clearInterval(slideTimerRef.current);
    };
  }, [config, nextSlide]);

  // 6. Force reload or cleanup on unmount
  useEffect(() => {
    const overlayTimer = setTimeout(() => {
      setShowOverlay(false);
    }, 3000);

    return () => {
      clearTimeout(overlayTimer);
      if (activeScanTimeoutRef.current) clearTimeout(activeScanTimeoutRef.current);
      if (slideTimerRef.current) clearInterval(slideTimerRef.current);
    };
  }, []);

  // When active slide is video, trigger autoplay and handle ending
  useEffect(() => {
    if (!config || !config.media_urls) return;
    const currentUrl = config.media_urls[currentSlideIndex];
    if (isVideoUrl(currentUrl)) {
      const activeVideo = videoRefs.current[currentSlideIndex];
      if (activeVideo) {
        activeVideo.currentTime = 0;
        activeVideo.play().catch((err) => console.log('Video autoplay blocked/failed:', err));
      }
    }
  }, [currentSlideIndex, config]);

  // 7. Direct DOM manipulation to bypass React's muted attribute binding issues
  useEffect(() => {
    videoRefs.current.forEach((video) => {
      if (video) {
        video.muted = isMuted;
      }
    });
  }, [isMuted, currentSlideIndex]);

  // 8. Background music control logic
  useEffect(() => {
    const hasMusic = config?.bg_music || config?.music_url;
    if (!config || !hasMusic || !audioRef.current) return;
    
    const currentUrl = config.media_urls[currentSlideIndex];
    const isCurrentVideo = currentUrl ? isVideoUrl(currentUrl) : false;

    if (isMuted) {
      audioRef.current.muted = true;
      audioRef.current.pause();
    } else {
      audioRef.current.muted = false;
      if (isCurrentVideo) {
        audioRef.current.pause();
      } else {
        audioRef.current.play().catch((err) => {
          console.log('Background music autoplay blocked:', err);
        });
      }
    }
  }, [isMuted, currentSlideIndex, config]);

  const toggleFullscreen = () => {
    setIsMuted(false);
    if (!document.fullscreenElement) {
      document.documentElement.requestFullscreen().catch((err) => {
        console.log(`Error attempting to enable full-screen mode:`, err);
      });
    }
  };

  // Handle Loading State
  if (loading) {
    return (
      <div className="min-h-screen bg-slate-950 flex flex-col items-center justify-center text-slate-100">
        <div className="h-10 w-10 border-4 border-emerald-500 border-t-transparent rounded-full animate-spin mb-4"></div>
        <p className="text-sm font-semibold tracking-wider text-slate-400">MEMUAT KIOSK...</p>
      </div>
    );
  }

  // Handle Error State
  if (errorMessage) {
    return (
      <div className="min-h-screen bg-slate-950 flex flex-col items-center justify-center text-slate-100 p-6 relative">
        <a href={adminUrl} aria-label="Admin Login Configuration" className="absolute top-0 right-0 w-[50px] h-[50px] bg-transparent cursor-default z-50"></a>
        <div className="max-w-md w-full bg-slate-900 border border-slate-800 rounded-3xl p-8 text-center shadow-2xl">
          <HelpCircle className="h-16 w-16 text-rose-500 mx-auto mb-4" />
          <h2 className="text-2xl font-bold text-white mb-2">Error Konfigurasi</h2>
          <p className="text-slate-400 text-sm mb-6">{errorMessage}</p>
          <p className="text-xs text-slate-500 leading-normal">
            Pastikan server admin Anda berjalan di <code className="bg-black px-1.5 py-0.5 rounded text-rose-400 font-mono">http://localhost:5000</code> dan TV ID ini telah terdaftar di dashboard.
          </p>
        </div>
      </div>
    );
  }

  // Handle TV ID Setup State
  if (!tvId) {
    return (
      <div className="min-h-screen bg-slate-950 flex flex-col items-center justify-center text-slate-100 p-6 relative">
        <a href={adminUrl} aria-label="Admin Login Configuration" className="absolute top-0 right-0 w-[50px] h-[50px] bg-transparent cursor-default z-50"></a>
        <div className="max-w-md w-full bg-slate-900 border border-slate-800 rounded-3xl p-8 text-center shadow-2xl">
          <div className="h-14 w-14 rounded-2xl bg-amber-500/10 border border-amber-500/20 text-amber-500 flex items-center justify-center mx-auto mb-4 animate-bounce">
            <svg className="h-8 w-8" fill="none" viewBox="0 0 24 24" stroke="currentColor">
              <path strokeLinecap="round" strokeLinejoin="round" strokeWidth="2" d="M12 9v2m0 4h.01m-6.938 4h13.856c1.54 0 2.502-1.667 1.732-3L13.732 4c-.77-1.333-2.694-1.333-3.464 0L3.34 16c-.77 1.333.192 3 1.732 3z" />
            </svg>
          </div>
          <h2 className="text-2xl font-extrabold text-white mb-2">Kiosk Belum Dikonfigurasi</h2>
          <p className="text-slate-400 text-sm leading-relaxed mb-6">
            Layar TV ID belum diset pada Kiosk ini. Silakan atur TV ID di <code className="bg-slate-950 px-1.5 py-0.5 rounded text-amber-500 font-mono">localStorage</code> atau klik pojok kanan atas layar ini untuk dikonfigurasi melalui Server Admin.
          </p>
          <div className="text-[11px] text-slate-600 bg-slate-950/40 p-4 border border-slate-850 rounded-2xl">
            <p className="font-semibold text-slate-500 mb-1">Panduan Pengaturan Cepat:</p>
            <p>Buka konsol browser (F12) dan jalankan:</p>
            <code className="block mt-1 bg-black p-2 rounded text-emerald-400 font-mono select-all">
              {"localStorage.setItem('tv_id', 'TV-LOBBY'); location.reload();"}
            </code>
          </div>
        </div>
      </div>
    );
  }

  const mediaUrls = config?.media_urls || [];
  const hasMedia = mediaUrls.length > 0;

  return (
    <div 
      onClick={toggleFullscreen}
      className="min-h-screen bg-black text-slate-100 overflow-hidden relative w-full h-screen select-none cursor-pointer"
    >
      
      {/* Invisible Admin Trigger Link */}
      <a href={adminUrl} aria-label="Admin Login Configuration" className="absolute top-0 right-0 w-[50px] h-[50px] bg-transparent cursor-default z-50" title="Admin Control Panel"></a>

      {/* IDLE STATE: Fullscreen Background Media Slideshow */}
      <div className={`absolute inset-0 w-full h-full transition-all duration-700 ${activeScan ? 'blur-xl scale-105 saturate-50 brightness-[0.25]' : 'blur-none scale-100 saturate-100 brightness-100'}`}>
        {hasMedia ? (
          mediaUrls.map((url, index) => {
            const isVideo = isVideoUrl(url);
            const isActive = index === currentSlideIndex;
            return (
              <div
                key={url}
                className={`absolute inset-0 w-full h-full transition-opacity duration-1000 ease-in-out ${isActive ? 'opacity-100 z-10 animate-fade-in' : 'opacity-0 z-0'}`}
              >
                {isVideo ? (
                  <video
                    ref={(el) => { videoRefs.current[index] = el; }}
                    src={url}
                    muted={isMuted}
                    loop={mediaUrls.length === 1} // Loop only if single video
                    playsInline
                    onEnded={nextSlide}
                    className="w-full h-full object-cover"
                  />
                ) : (
              /* eslint-disable-next-line @next/next/no-img-element */
                  <img
                    src={url}
                    alt="Slideshow"
                    className="w-full h-full object-cover"
                  />
                )}
              </div>
            );
          })
        ) : (
          /* Fallback Ambient Dark Gradient when no media is uploaded */
          <div className="w-full h-full bg-gradient-to-tr from-slate-950 via-slate-900 to-indigo-950 flex flex-col items-center justify-center p-6">
            <div className="absolute inset-0 opacity-10 bg-[linear-gradient(to_right,#0f172a_1px,transparent_1px),linear-gradient(to_bottom,#0f172a_1px,transparent_1px)] bg-[size:4rem_4rem]"></div>
            <div className="relative z-10 text-center space-y-4">
              <span className="inline-flex items-center px-3 py-1 rounded-full text-xs font-semibold bg-emerald-500/10 text-emerald-400 border border-emerald-500/20">
                TV ID: {tvId}
              </span>
              <p className="text-slate-500 text-sm font-medium tracking-wide uppercase">Menunggu Unggahan Media Slideshow...</p>
            </div>
          </div>
        )}
      </div>

      {/* Bottom Status Overlay */}
      <div className={`absolute bottom-4 left-4 z-20 flex items-center space-x-2 text-[10px] bg-black/60 backdrop-blur-md px-3 py-1.5 border border-slate-800/80 rounded-xl transition-all duration-1000 ${showOverlay ? 'opacity-100 translate-y-0' : 'opacity-0 translate-y-2 pointer-events-none'}`}>
        <span className="text-slate-500 uppercase tracking-wider font-semibold">TV ID:</span>
        <span className="text-white font-bold font-mono">{tvId}</span>
        <span className="text-slate-700">|</span>
        <span className="text-slate-500 uppercase tracking-wider font-semibold">Broker ({brokerHost}):</span>
        <span className={`font-bold uppercase tracking-wider ${
          !config?.mqtt_topic ? 'text-slate-500' :
          mqttStatus === 'connected' ? 'text-emerald-400' :
          mqttStatus === 'connecting' ? 'text-amber-500 animate-pulse' : 'text-rose-500'
        }`}>
          {!config?.mqtt_topic ? 'Disabled (Aman)' : mqttStatus === 'connected' ? 'Connected (Aman)' : mqttStatus === 'connecting' ? 'Connecting' : 'Offline (Tidak Aman)'}
        </span>
      </div>
      
      {(config?.bg_music || config?.music_url) && (
        <audio
          ref={audioRef}
          src={config.bg_music || config.music_url || undefined}
          loop
          preload="auto"
          style={{ display: 'none' }}
        />
      )}

      {/* ACTIVE STATE: RFID Scan Popup Overlay */}
      {activeScan && (
        <div className="absolute inset-0 flex items-center justify-center z-30 px-4 transition-all duration-300 backdrop-blur-xl bg-black/60">
          
          <div className="max-w-md w-full bg-slate-950/70 border border-slate-800/85 backdrop-blur-2xl rounded-3xl p-8 shadow-[0_0_50px_rgba(0,0,0,0.8)] relative overflow-hidden transition-all duration-300 transform scale-100 animate-in fade-in zoom-in-95">
            
            {activeScan.status === 'invalid' ? (
              // Red Invalid Access Popup
              <div className="text-center relative z-10 space-y-6">
                <div className="absolute top-1/2 left-1/2 -translate-x-1/2 -translate-y-1/2 w-64 h-64 bg-red-500/20 rounded-full blur-3xl pointer-events-none -z-10 animate-pulse"></div>
                
                <div className="h-16 w-16 rounded-full bg-red-500/10 border border-red-500/30 flex items-center justify-center mx-auto text-red-500 shadow-[0_0_40px_rgba(239,68,68,0.4)]">
                  <ShieldAlert className="h-8 w-8" />
                </div>
                
                <div className="space-y-2">
                  <span className="text-[10px] font-black tracking-widest text-red-500 uppercase bg-red-500/10 border border-red-500/20 px-3 py-1 rounded-full">
                    Akses Ditolak
                  </span>
                  <h3 className="text-3xl font-extrabold text-white tracking-tight mt-2">
                    {activeScan.name || 'Kartu Tidak Dikenal'}
                  </h3>
                </div>
                
                <div className="bg-red-950/30 border border-red-500/20 py-2.5 px-4 rounded-xl inline-block shadow-[0_0_20px_rgba(239,68,68,0.15)]">
                  <p className="text-sm font-semibold text-red-300 tracking-wide">
                    {activeScan.message || 'Kartu Tidak Valid / Kedaluwarsa'}
                  </p>
                </div>
              </div>
            ) : activeScan.ticket_type === 'qr_guest' ? (
              // Blue/Green Valid QR Guest (Tamu)
              <div className="text-center relative z-10 space-y-6">
                <div className="absolute top-1/2 left-1/2 -translate-x-1/2 -translate-y-1/2 w-64 h-64 bg-blue-500/20 rounded-full blur-3xl pointer-events-none -z-10 animate-pulse"></div>
                
                <div className="h-16 w-16 rounded-full bg-blue-500/10 border border-blue-500/30 flex items-center justify-center mx-auto text-blue-400 shadow-[0_0_40px_rgba(59,130,246,0.4)]">
                  <Ticket className="h-8 w-8" />
                </div>
                
                <div className="space-y-1">
                  <span className="text-[10px] font-black tracking-widest text-blue-400 uppercase bg-blue-500/10 border border-blue-500/20 px-3 py-1 rounded-full">
                    TAMU / GUEST
                  </span>
                  
                  <div className="pt-3">
                    <h3 className="text-3xl font-extrabold text-white tracking-tight leading-none mt-1">
                      {activeScan.name}
                    </h3>
                  </div>
                </div>

                <div className="bg-blue-950/30 border border-blue-500/20 py-2 px-4 rounded-xl inline-block">
                  <p className="text-sm font-semibold text-blue-300 tracking-wide">
                    {activeScan.message || 'Silakan Masuk'}
                  </p>
                </div>

                {/* Detailed metadata - QR Guest */}
                <div className="mt-6 pt-6 border-t border-slate-800/80 text-left space-y-3">
                  <div className="flex justify-between items-center text-xs">
                    <span className="text-slate-400 flex items-center"><Ticket className="h-3.5 w-3.5 mr-1.5 text-slate-500" /> Tipe Scan</span>
                    <span className="font-semibold text-slate-200">QR Guest (Tamu)</span>
                  </div>
                  {activeScan.rfid && (
                    <div className="flex justify-between items-center text-xs">
                      <span className="text-slate-400 flex items-center"><User className="h-3.5 w-3.5 mr-1.5 text-slate-500" /> Token QR</span>
                      <span className="font-mono text-slate-300 bg-slate-900 px-2 py-0.5 rounded border border-slate-800">
                        {activeScan.rfid}
                      </span>
                    </div>
                  )}
                  {activeScan.valid_until && (
                    <div className="flex justify-between items-center text-xs">
                      <span className="text-slate-400 flex items-center"><Calendar className="h-3.5 w-3.5 mr-1.5 text-slate-500" /> Valid Sampai</span>
                      <span className="font-semibold text-slate-200 font-mono">
                        {activeScan.valid_until}
                      </span>
                    </div>
                  )}
                </div>
              </div>
            ) : activeScan.ticket_type === 'rfid_master' ? (
              // Purple Valid RFID Master (Petugas)
              <div className="text-center relative z-10 space-y-6">
                <div className="absolute top-1/2 left-1/2 -translate-x-1/2 -translate-y-1/2 w-64 h-64 bg-purple-500/20 rounded-full blur-3xl pointer-events-none -z-10 animate-pulse"></div>
                
                <div className="h-16 w-16 rounded-full bg-purple-500/10 border border-purple-500/30 flex items-center justify-center mx-auto text-purple-400 shadow-[0_0_40px_rgba(168,85,247,0.4)]">
                  <ShieldCheck className="h-8 w-8" />
                </div>
                
                <div className="space-y-1">
                  <span className="text-[10px] font-black tracking-widest text-purple-400 uppercase bg-purple-500/10 border border-purple-500/20 px-3 py-1 rounded-full">
                    AKSES MASTER (PETUGAS)
                  </span>
                  
                  <div className="pt-3">
                    <h3 className="text-3xl font-extrabold text-white tracking-tight leading-none mt-1">
                      {activeScan.name}
                    </h3>
                  </div>
                </div>

                <div className="bg-purple-950/30 border border-purple-500/20 py-2.5 px-4 rounded-xl inline-block shadow-[0_0_20px_rgba(168,85,247,0.15)]">
                  <p className="text-sm font-semibold text-purple-300 tracking-wide">
                    {activeScan.message || 'Akses Master Diberikan'}
                  </p>
                </div>

                {/* Detailed metadata - RFID Master */}
                <div className="mt-6 pt-6 border-t border-slate-800/80 text-left space-y-3">
                  <div className="flex justify-between items-center text-xs">
                    <span className="text-slate-400 flex items-center"><Ticket className="h-3.5 w-3.5 mr-1.5 text-slate-500" /> Tipe Scan</span>
                    <span className="font-semibold text-slate-200">RFID Master (Petugas)</span>
                  </div>
                  {activeScan.rfid && (
                    <div className="flex justify-between items-center text-xs">
                      <span className="text-slate-400 flex items-center"><User className="h-3.5 w-3.5 mr-1.5 text-slate-500" /> Token / RFID</span>
                      <span className="font-mono text-slate-300 bg-slate-900 px-2 py-0.5 rounded border border-slate-800">
                        {activeScan.rfid}
                      </span>
                    </div>
                  )}
                  {activeScan.valid_until && (
                    <div className="flex justify-between items-center text-xs">
                      <span className="text-slate-400 flex items-center"><Calendar className="h-3.5 w-3.5 mr-1.5 text-slate-500" /> Valid Sampai</span>
                      <span className="font-semibold text-slate-200 font-mono">
                        {activeScan.valid_until}
                      </span>
                    </div>
                  )}
                  <div className="flex justify-between items-center text-xs">
                    <span className="text-slate-400 flex items-center"><CreditCard className="h-3.5 w-3.5 mr-1.5 text-slate-500" /> Saldo Kartu</span>
                    <span className="font-bold text-purple-400 font-mono">
                      Akses Unlimited
                    </span>
                  </div>
                </div>
              </div>
            ) : (
              // Green Valid RFID Member (Member)
              <div className="text-center relative z-10 space-y-6">
                <div className="absolute top-1/2 left-1/2 -translate-x-1/2 -translate-y-1/2 w-64 h-64 bg-lime-500/20 rounded-full blur-3xl pointer-events-none -z-10 animate-pulse"></div>
                
                <div className="h-16 w-16 rounded-full bg-lime-500/10 border border-lime-500/30 flex items-center justify-center mx-auto text-lime-400 shadow-[0_0_40px_rgba(132,204,22,0.4)]">
                  <ShieldCheck className="h-8 w-8" />
                </div>
                
                <div className="space-y-1">
                  <span className="text-[10px] font-black tracking-widest text-lime-400 uppercase bg-lime-500/10 border border-lime-500/20 px-3 py-1 rounded-full">
                    MEMBER
                  </span>
                  
                  <div className="pt-3">
                    <h3 className="text-3xl font-extrabold text-white tracking-tight leading-none mt-1">
                      {activeScan.name}
                    </h3>
                  </div>
                </div>

                <div className="bg-lime-950/30 border border-lime-500/20 py-2.5 px-4 rounded-xl inline-block shadow-[0_0_20px_rgba(132,204,22,0.15)]">
                  <p className="text-sm font-semibold text-lime-300 tracking-wide">
                    {activeScan.message || 'Akses Diberikan'}
                  </p>
                </div>

                {/* Detailed metadata - RFID Member */}
                <div className="mt-6 pt-6 border-t border-slate-800/80 text-left space-y-3">
                  <div className="flex justify-between items-center text-xs">
                    <span className="text-slate-400 flex items-center"><Ticket className="h-3.5 w-3.5 mr-1.5 text-slate-500" /> Tipe Scan</span>
                    <span className="font-semibold text-slate-200">RFID Member (Lokal)</span>
                  </div>
                  {activeScan.rfid && (
                    <div className="flex justify-between items-center text-xs">
                      <span className="text-slate-400 flex items-center"><User className="h-3.5 w-3.5 mr-1.5 text-slate-500" /> Token / RFID</span>
                      <span className="font-mono text-slate-300 bg-slate-900 px-2 py-0.5 rounded border border-slate-800">
                        {activeScan.rfid}
                      </span>
                    </div>
                  )}
                  {activeScan.valid_until && (
                    <div className="flex justify-between items-center text-xs">
                      <span className="text-slate-400 flex items-center"><Calendar className="h-3.5 w-3.5 mr-1.5 text-slate-500" /> Valid Sampai</span>
                      <span className="font-semibold text-slate-200 font-mono">
                        {activeScan.valid_until}
                      </span>
                    </div>
                  )}
                  {activeScan.saldo !== undefined && (
                    <div className="flex justify-between items-center text-xs">
                      <span className="text-slate-400 flex items-center"><CreditCard className="h-3.5 w-3.5 mr-1.5 text-slate-500" /> Saldo Kartu</span>
                      <span className="font-bold text-emerald-400 font-mono">
                        Rp {activeScan.saldo.toLocaleString('id-ID')}
                      </span>
                    </div>
                  )}
                </div>
              </div>
            )}
            
          </div>
        </div>
      )}

    </div>
  );
}