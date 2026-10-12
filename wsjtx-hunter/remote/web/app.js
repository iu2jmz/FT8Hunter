const $=s=>document.querySelector(s), $$=s=>[...document.querySelectorAll(s)];
let ws=null, cfg={webSocketPort:8788}, state={}, commandId=1;
let manualDisconnect=false, reconnectTimer=null, pingTimer=null;
const toast=(m,ms=1800)=>{const t=$('#toast');t.textContent=m;t.classList.add('show');setTimeout(()=>t.classList.remove('show'),ms)};
const formatHz=n=>Number(n||0).toLocaleString('it-IT');

async function loadCfg(){
  try{cfg=await (await fetch('remote-config.json',{cache:'no-store'})).json()}catch(e){}
}

function setConnected(ok){
  $('#connDot').classList.toggle('off',!ok);
  $('#connText').textContent=ok?'Connesso':'Offline';
}

function command(name,args={}){
  if(!ws||ws.readyState!==1){toast('Non connesso');return}
  ws.send(JSON.stringify(Object.assign({type:'command',id:commandId++,name:name},args)));
}

function clearReconnect(){
  if(reconnectTimer){clearTimeout(reconnectTimer);reconnectTimer=null}
}
function startPing(){
  if(pingTimer)clearInterval(pingTimer);
  pingTimer=setInterval(()=>{if(ws&&ws.readyState===1)ws.send(JSON.stringify({type:'ping'}))},20000);
}
function scheduleReconnect(){
  if(manualDisconnect||reconnectTimer)return;
  const token=localStorage.getItem('ft8hunter-token')||'';
  if(!token)return;
  reconnectTimer=setTimeout(()=>{reconnectTimer=null;connectRemote()},2500);
}
async function connectRemote(){
  if(ws&&(ws.readyState===0||ws.readyState===1))return;
  await loadCfg();
  const token=$('#token').value.trim()||localStorage.getItem('ft8hunter-token')||'';
  if(!token){$('#loginError').textContent='Inserisci il token';return}
  manualDisconnect=false;
  clearReconnect();
  localStorage.setItem('ft8hunter-token',token);
  $('#token').value=token;
  ws=new WebSocket('ws://'+location.hostname+':'+cfg.webSocketPort);
  ws.onopen=()=>ws.send(JSON.stringify({type:'auth',token:token}));
  ws.onclose=()=>{
    setConnected(false);
    if(pingTimer){clearInterval(pingTimer);pingTimer=null}
    if(manualDisconnect){$('#login').classList.remove('hidden')}
    else scheduleReconnect();
  };
  ws.onerror=()=>{$('#loginError').textContent='Connessione interrotta - riconnessione automatica...'};
  ws.onmessage=e=>{
    let m;try{m=JSON.parse(e.data)}catch(err){return}
    if(m.type==='auth'){
      if(m.ok){$('#login').classList.add('hidden');$('#loginError').textContent='';setConnected(true);startPing()}
      else $('#loginError').textContent=m.message||'Token non valido';
    }else if(m.type==='state'){
      state=m;renderState();
    }else if(m.type==='spectrum'){
      renderSpectrum(m);
    }else if(m.type==='command_result'){
      if(Array.isArray(m.rows))renderLog(m.rows);
      if(!m.ok)toast(m.message||'Comando rifiutato',5000);else if(m.message)toast(m.message);
    }
  };
}

function renderState(){
  $('#myCall').textContent=state.myCall||'IU2JMZ';

  const txAllowed=state.allowTransmit===true;
  const auth=$('#remoteTxBanner');
  auth.classList.toggle('blocked',!txAllowed);
  auth.innerHTML=txAllowed
    ? '<b>TX REMOTA ABILITATA</b><span>I comandi Hunter che possono trasmettere sono autorizzati dal PC.</span>'
    : '<b>TX REMOTA BLOCCATA</b><span>Sul PC abilita: Consenti comandi che possono avviare la TX.</span>';

  ['monitorAutoBtn','bandHunterBtn','dxAutoBtn','pskAutoBtn'].forEach(id=>{
    const b=$('#'+id); if(!b)return;
    b.disabled=!txAllowed;
    b.classList.toggle('remote-disabled',!txAllowed);
  });
  if($('#tuneBtn')){
    const lockTune=!txAllowed && !state.tune;
    $('#tuneBtn').disabled=lockTune;
    $('#tuneBtn').classList.toggle('remote-disabled',lockTune);
  }

  $('#freq').textContent=formatHz(state.frequencyHz);
  $('#freqInput').value=state.frequencyHz||0;
  $('#bandBadge').textContent=state.band||'--';
  $('#rxHz').textContent=state.rxOffset==null?'--':state.rxOffset;
  $('#txHz').textContent=state.txOffset==null?'--':state.txOffset;
  $('#rxInput').value=state.rxOffset==null?1500:state.rxOffset;
  $('#txInput').value=state.txOffset==null?1500:state.txOffset;
  if(state.rfPower!=null){$('#rfPower').textContent=state.rfPower;$('#power').value=state.rfPower}
  $$('.mode').forEach(b=>b.classList.toggle('active',b.dataset.mode===state.mode));
  $$('[data-band]').forEach(b=>b.classList.toggle('active',b.dataset.band===state.band));
  $('#monitorState').textContent=(state.monitorHunter&&state.monitorHunter.state)||'--';
  $('#monitorTarget').textContent=(state.monitorHunter&&state.monitorHunter.target)||'Nessun target';
  $('#targetCall').textContent=(state.monitorHunter&&state.monitorHunter.target)||'—';
  $('#targetCountry').textContent=(state.monitorHunter&&state.monitorHunter.country)||'—';
  $('#targetProgress').textContent=(state.monitorHunter&&state.monitorHunter.progress)||'0/8';
  $('#monitorAutoBtn').textContent=(state.monitorHunter&&state.monitorHunter.automatic)?'AUTO ON':'AUTO OFF';
  $('#monitorReady').textContent=(state.monitorHunter&&state.monitorHunter.filterReady)
    ? 'Filtro DXCC: PRONTO · Coda '+(state.monitorHunter.queueCount||0)
    : 'Filtro DXCC: NON PRONTO';
  $('#bandHunterState').textContent=(state.bandHunter&&state.bandHunter.active)?'ATTIVO':'OFF';
  $('#bandHunterInfo').textContent=(state.bandHunter&&state.bandHunter.currentBand)?(state.bandHunter.currentBand+' → '+(state.bandHunter.nextBand||'--')):'--';
  $('#bandReady').textContent=(state.monitorHunter&&state.monitorHunter.filterReady)?'Motore Monitor: PRONTO':'Motore Monitor: BLOCCATO';
  $('#dxState').textContent=(state.dxFun&&state.dxFun.active)?'ATTIVO':'IDLE';
  $('#dxInfo').textContent=(state.dxFun&&state.dxFun.connected)?'Cluster connesso · '+(state.dxFun.candidates||0)+' candidati':'Cluster disconnesso';
  $('#pskState').textContent=(state.pskReporter&&state.pskReporter.active)?'ATTIVO':'IDLE';
  $('#pskInfo').textContent=(state.pskReporter&&state.pskReporter.workedBeforeReady)
    ? 'Worked Before pronto · '+(state.pskReporter.candidates||0)+' candidati'
    : 'Worked Before non pronto';
  $('#tuneBtn').textContent=state.tune?'TUNE ON':'TUNE';
  $('#dxConnectBtn').textContent=(state.dxFun&&state.dxFun.connected)?'Disconnetti':'Connetti';
  $('#dxAutoBtn').textContent=(state.dxFun&&state.dxFun.automatic)?'AUTO QSY ON':'AUTO QSY OFF';
  $('#pskConnectBtn').textContent=(state.pskReporter&&state.pskReporter.connectionEnabled)?'Disconnetti':'Connetti';
  $('#pskAutoBtn').textContent=(state.pskReporter&&state.pskReporter.automatic)?'AUTO QSY ON':'AUTO QSY OFF';
  renderBlacklist(state.blacklist||[]);
  renderDx(state.dxFun||{});
  renderPsk(state.pskReporter||{});
}

function renderBlacklist(rows){
  const root=$('#blacklistRows');root.innerHTML='';
  if(!rows.length){root.innerHTML='<p>Nessuna stazione in blacklist.</p>';return}
  rows.forEach(x=>{
    const d=document.createElement('div');
    d.className='list-row';
    d.innerHTML='<div><b>'+x.call+'</b><br><small>'+(x.band||'')+'</small></div><small>'+(x.remaining||'')+'</small><button data-call="'+x.call+'">Riabilita</button>';
    root.appendChild(d);
  });
  root.querySelectorAll('button').forEach(b=>b.onclick=()=>command('blacklist_reinstate',{call:b.dataset.call}));
}

function renderDx(dx){
  $('#dxCounters').textContent=(dx.spots||0)+' spot · '+(dx.candidates||0)+' candidati';
  const root=$('#dxRows');root.innerHTML='';
  const rows=dx.recent||[];
  if(!rows.length){root.innerHTML='<p>Nessuno spot disponibile.</p>';return}
  rows.forEach(x=>{
    const d=document.createElement('div');d.className='list-row remote-event';
    d.innerHTML='<div><b>'+x.call+'</b><br><small>'+x.utc+' · '+x.band+' · '+x.mode+'</small></div><small>'+x.mhz+' MHz</small><b>'+x.status+'</b>';
    root.appendChild(d);
  });
}

function renderPsk(psk){
  $('#pskCounters').textContent=(psk.reports||0)+' report · '+(psk.candidates||0)+' candidati';
  const root=$('#pskRows');root.innerHTML='';
  const rows=psk.recent||[];
  if(!rows.length){root.innerHTML='<p>Nessun report disponibile.</p>';return}
  rows.forEach(x=>{
    const d=document.createElement('div');d.className='list-row remote-event';
    d.innerHTML='<div><b>'+x.call+'</b><br><small>'+x.country+' · '+x.band+' · '+x.mode+'</small></div><small>SNR '+x.snr+'</small><b>'+x.status+'</b>';
    root.appendChild(d);
  });
}

function renderLog(rows){
  const root=$('#logRows');root.innerHTML='';
  if(!rows.length){root.innerHTML='<p>Nessun QSO disponibile.</p>';return}
  rows.forEach(x=>{
    const d=document.createElement('div');d.className='list-row remote-event';
    d.innerHTML='<div><b>'+x.call+'</b><br><small>'+x.date+'</small></div><small>'+x.band+'</small><b>'+x.mode+'</b>';
    root.appendChild(d);
  });
}

function renderSpectrum(m){
  const a=m.values||[];if(!a.length)return;
  const cv=$('#spectrum'),ctx=cv.getContext('2d'),w=cv.width,h=cv.height;
  ctx.fillStyle='#020812';ctx.fillRect(0,0,w,h);
  let min=Math.min.apply(null,a),max=Math.max.apply(null,a),span=Math.max(1e-9,max-min);
  ctx.beginPath();
  a.forEach((v,i)=>{const x=i/(a.length-1)*w,y=h-((v-min)/span)*(h-12);if(i)ctx.lineTo(x,y);else ctx.moveTo(x,y)});
  ctx.strokeStyle='#16e5a5';ctx.lineWidth=2;ctx.stroke();

  const wf=$('#waterfall'),wx=wf.getContext('2d');
  const img=wx.getImageData(0,0,w,h-3);wx.putImageData(img,0,3);
  for(let x=0;x<w;x++){
    const i=Math.min(a.length-1,Math.floor(x/w*a.length));
    const t=(a[i]-min)/span;
    const r=Math.floor(255*Math.max(0,(t-.55)*2.2));
    const g=Math.floor(255*Math.min(1,t*1.5));
    const b=Math.floor(255*(1-t*.75));
    wx.fillStyle='rgb('+r+','+g+','+b+')';wx.fillRect(x,0,1,3);
  }
}

$$('.tabs button').forEach(b=>b.onclick=()=>{
  $$('.tabs button').forEach(x=>x.classList.remove('active'));
  $$('.page').forEach(x=>x.classList.remove('active'));
  b.classList.add('active');$('#'+b.dataset.tab).classList.add('active');
  if(b.dataset.tab==='log')command('get_log');
});
$$('.mode').forEach(b=>b.onclick=()=>command('set_mode',{mode:b.dataset.mode}));
$$('[data-band]').forEach(b=>b.onclick=()=>command('set_band',{band:b.dataset.band}));
$$('[data-rx]').forEach(b=>b.onclick=()=>command('set_rx_offset',{value:Number($('#rxInput').value)+Number(b.dataset.rx)}));
$$('[data-tx]').forEach(b=>b.onclick=()=>command('set_tx_offset',{value:Number($('#txInput').value)+Number(b.dataset.tx)}));
$('#rxInput').onchange=()=>command('set_rx_offset',{value:Number($('#rxInput').value)});
$('#txInput').onchange=()=>command('set_tx_offset',{value:Number($('#txInput').value)});
$('#power').onchange=()=>command('set_power',{value:Number($('#power').value)});
$('#monitorBtn').onclick=()=>command('monitor',{enabled:true});
$('#tuneBtn').onclick=()=>command('tune',{enabled:!state.tune});
$('#stopTxBtn').onclick=()=>command('stop_tx');
$('#refreshBtn').onclick=()=>command('refresh');
$('#monitorAutoBtn').onclick=()=>{if(!state.allowTransmit){toast('TX remota non autorizzata nelle impostazioni PC',5000);return}command('monitor_hunter_auto',{enabled:!(state.monitorHunter&&state.monitorHunter.automatic)})};
$('#bandHunterBtn').onclick=()=>{if(!state.allowTransmit){toast('TX remota non autorizzata nelle impostazioni PC',5000);return}command('band_hunter',{enabled:!(state.bandHunter&&state.bandHunter.active)})};
$('#setFreqBtn').onclick=()=>command('set_frequency',{frequencyHz:Number($('#freqInput').value)});
$('#blacklistRefresh').onclick=()=>command('refresh');
$('#logRefresh').onclick=()=>command('get_log');
$('#dxRefreshBtn').onclick=()=>command('refresh');
$('#pskRefreshBtn').onclick=()=>command('refresh');
$('#dxConnectBtn').onclick=()=>command('dxfun_connect',{enabled:!(state.dxFun&&state.dxFun.connected)});
$('#dxAutoBtn').onclick=()=>{if(!state.allowTransmit){toast('TX remota non autorizzata nelle impostazioni PC',5000);return}command('dxfun_auto',{enabled:!(state.dxFun&&state.dxFun.automatic)})};
$('#pskConnectBtn').onclick=()=>command('psk_connect',{enabled:!(state.pskReporter&&state.pskReporter.connectionEnabled)});
$('#pskAutoBtn').onclick=()=>{if(!state.allowTransmit){toast('TX remota non autorizzata nelle impostazioni PC',5000);return}command('psk_auto',{enabled:!(state.pskReporter&&state.pskReporter.automatic)})};
$('#disconnectBtn').onclick=()=>{
  manualDisconnect=true;
  clearReconnect();
  if(pingTimer){clearInterval(pingTimer);pingTimer=null}
  localStorage.removeItem('ft8hunter-token');
  if(ws)ws.close();
  setConnected(false);
  $('#login').classList.remove('hidden');
  if(window.FT8HunterNative&&typeof window.FT8HunterNative.disconnectRemote==='function'){
    try{window.FT8HunterNative.disconnectRemote()}catch(e){}
  }
};
$('#themeSelect').onchange=e=>{
  document.body.classList.toggle('light',e.target.value==='light');
  localStorage.setItem('ft8hunter-theme',e.target.value);
};
$('#connectBtn').onclick=connectRemote;

const savedTheme=localStorage.getItem('ft8hunter-theme')||'dark';
$('#themeSelect').value=savedTheme;
document.body.classList.toggle('light',savedTheme==='light');
const savedToken=localStorage.getItem('ft8hunter-token')||'';
$('#token').value=savedToken;
if('serviceWorker' in navigator)navigator.serviceWorker.register('service-worker.js').catch(()=>{});
if(savedToken)connectRemote();

window.ensureRemoteConnected=()=>{
  const token=localStorage.getItem('ft8hunter-token')||'';
  if(token&&(!ws||ws.readyState===2||ws.readyState===3)){
    $('#token').value=token;
    connectRemote();
  }
};
document.addEventListener('visibilitychange',()=>{if(!document.hidden)window.ensureRemoteConnected()});
window.addEventListener('online',()=>window.ensureRemoteConnected());
window.addEventListener('pageshow',()=>window.ensureRemoteConnected());
