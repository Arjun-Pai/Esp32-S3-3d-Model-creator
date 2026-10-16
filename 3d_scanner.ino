/*
  TEMPAD 3D SCANNER  -  ESP32-CAM turntable scanner, web UI, STL output
  ---------------------------------------------------------------------
  How it works
    * The ESP32 only serves JPEG frames and spins the turntable.
    * All the maths runs in your browser (phone or PC):
        background subtraction -> silhouette per angle -> visual-hull carving
        -> smoothing -> surface-nets mesh -> binary STL download.
    * No internet needed; no libraries beyond the ESP32 Arduino core.

  Board: ESP32-S3 camera board (OV3660), same camera pins as tempad_camera_sd_webui.ino.
  Arduino IDE settings (same as your camera sketch):
    Board: ESP32S3 Dev Module | USB CDC On Boot: Enabled | PSRAM: OPI PSRAM
    Flash Size: 16MB | Partition Scheme: Huge APP (or the 16M one) | Serial Monitor 115200
  Tools > Core Debug Level: Error (to see camera driver messages).

  Pins already used on your board (do not reuse for the motor):
    Camera: 4-13, 15-18   microSD: 38, 39, 40   MPU-6050: 1, 2
    PSRAM/flash: 26-37    USB: 19, 20           Boot/strapping: 0, 3, 45, 46
  Turntable (optional - manual rotation also works from the UI), A4988/DRV8825:
    STEP -> GPIO41, DIR -> GPIO42, EN -> GPIO47 (EN low = on), GND shared with the board
    If your round display (GC9A01A) uses any of 41/42/47, change them below.
  The microSD is not used here: the STL is downloaded by the browser.
*/

#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include "esp_camera.h"

// ------------------------- USER SETTINGS -------------------------
#define WIFI_SSID      "Airtel_ArjunPai_Mesh_5G"
#define WIFI_PASS      "Hanuman@56165616"
#define AP_SSID        "Tempad-Scanner"     // fallback hotspot if WiFi fails
#define AP_PASS        "tempad1234"
#define HOSTNAME       "tempad"             // http://tempad.local

#define CAM_XCLK_HZ    10000000             // raise to 20000000 once picture is clean
#define CAM_VFLIP      1                    // check with text in front of the lens:
#define CAM_HMIRROR    0                    // image must be upright and NOT mirrored

#define STEP_PIN       41
#define DIR_PIN        42
#define EN_PIN         47
#define LED_PIN        -1                   // set to a free GPIO for a lamp; -1 = none
#define MOTOR_STEPS    200                  // full steps per motor rev
#define MICROSTEP      16                   // driver microstepping
#define GEAR_RATIO     1.0                  // turntable turns / motor turns inverse (e.g. 3.0 = 3 motor revs per table rev)
#define STEP_US        600                  // half-period of a step pulse (bigger = slower, more torque)
#define ROT_DIR        1                    // flip to 0 to reverse motor direction
// -----------------------------------------------------------------

#define STEPS_PER_REV  ((float)MOTOR_STEPS * MICROSTEP * GEAR_RATIO)

// ESP32-S3 camera pins (identical to your working camera + SD sketch)
#define PWDN_GPIO_NUM  -1
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM  15
#define SIOD_GPIO_NUM   4
#define SIOC_GPIO_NUM   5
#define Y9_GPIO_NUM    16
#define Y8_GPIO_NUM    17
#define Y7_GPIO_NUM    18
#define Y6_GPIO_NUM    12
#define Y5_GPIO_NUM    10
#define Y4_GPIO_NUM     8
#define Y3_GPIO_NUM     9
#define Y2_GPIO_NUM    11
#define VSYNC_GPIO_NUM  6
#define HREF_GPIO_NUM   7
#define PCLK_GPIO_NUM  13

WebServer server(80);
float stepRemainder = 0;
bool camOk = false;

// ============================ WEB PAGE ============================
const char PAGE[] PROGMEM = R"rawliteral(
<!doctype html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Tempad 3D Scanner</title>
<style>
:root{--bg:#120d05;--pn:#1e1608;--am:#ffb000;--dim:#a87400;--ln:#4a3500}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--am);font:14px/1.4 ui-monospace,Menlo,Consolas,monospace}
header{padding:12px 16px;border-bottom:2px solid var(--ln);font-size:18px;letter-spacing:3px}
main{display:grid;gap:12px;padding:12px;max-width:1100px;margin:auto;grid-template-columns:repeat(auto-fit,minmax(320px,1fr))}
section{background:var(--pn);border:1px solid var(--ln);border-radius:6px;padding:12px}
h2{margin:0 0 8px;font-size:13px;color:var(--dim);text-transform:uppercase;letter-spacing:2px}
label{display:flex;justify-content:space-between;align-items:center;gap:8px;margin:4px 0}
input,select{background:#000;color:var(--am);border:1px solid var(--ln);border-radius:4px;padding:4px 6px;width:110px;font:inherit}
input[type=checkbox]{width:auto}
button{background:var(--am);color:#000;border:0;border-radius:4px;padding:8px 12px;margin:6px 6px 0 0;font:inherit;font-weight:700;cursor:pointer}
button.alt{background:transparent;color:var(--am);border:1px solid var(--am)}
button:disabled{opacity:.35;cursor:default}
canvas,img.pv{width:100%;background:#000;border:1px solid var(--ln);border-radius:4px;display:block;margin-top:6px}
#log{height:130px;overflow:auto;white-space:pre-wrap;background:#000;border:1px solid var(--ln);padding:6px;font-size:12px;margin-top:6px}
progress{width:100%;margin-top:6px}
.full{grid-column:1/-1}
.hint{color:var(--dim);font-size:12px;margin:4px 0}
</style></head><body>
<header>TEMPAD // 3D SCANNER</header>
<main>

<section>
<h2>1 &middot; Camera</h2>
<img id="live" class="pv" alt="live view">
<label>Resolution <select id="fs"><option value="qvga">320x240</option><option value="vga" selected>640x480</option><option value="svga">800x600</option><option value="xga">1024x768</option></select></label>
<label>JPEG quality (lower = better) <select id="q"><option>8</option><option selected>12</option><option>16</option><option>24</option></select></label>
<button class="alt act" onclick="cfg()">Apply</button>
<button class="alt" id="bLive" onclick="toggleLive()">Live view</button>
<button class="alt act" onclick="lock(1)">Lock exposure</button>
<button class="alt act" onclick="lock(0)">Auto exposure</button>
<button class="alt act" onclick="api('/led?v=1')">Light on</button>
<button class="alt act" onclick="api('/led?v=0')">Light off</button>
<p class="hint">Use a plain, evenly lit background (white sheet) that contrasts with the object. Upright, not mirrored. Lock exposure once the image looks right.</p>
</section>

<section>
<h2>2 &middot; Geometry (millimetres)</h2>
<label>Camera horizontal FOV (deg) <input id="fov" type="number" value="65"></label>
<label>Camera to turntable axis <input id="dist" type="number" value="300"></label>
<label>Lens height above table <input id="camh" type="number" value="60"></label>
<label>Scan radius (half width) <input id="rad" type="number" value="60"></label>
<label>Scan height <input id="hgt" type="number" value="100"></label>
<label>Reverse rotation direction <input id="rev" type="checkbox"></label>
<p class="hint">Camera must be level, looking at the turntable centre. Turntable axis should sit in the middle of the image. Everything inside the radius/height must be visible in every view.</p>
</section>

<section>
<h2>3 &middot; Capture</h2>
<label>Views per revolution <input id="views" type="number" value="36"></label>
<label>Settle time (ms) <input id="settle" type="number" value="700"></label>
<label>Manual rotation (no motor) <input id="manual" type="checkbox"></label>
<label>Mask threshold <input id="thr" type="number" value="35"></label>
<label>Keep largest blob only <input id="blob" type="checkbox" checked></label>
<button class="act" onclick="capBg()">A. Capture empty background</button>
<button class="alt act" onclick="testMask()">B. Test mask</button>
<button class="alt act" onclick="api('/rotate?deg=10')">Rotate 10&deg;</button>
<button class="act" onclick="scan()">C. Start scan</button>
<button class="alt" onclick="nxt()">Next view (manual)</button>
<progress id="prog" value="0" max="1"></progress>
<div id="pt" class="hint">idle</div>
<canvas id="mask" width="320" height="240"></canvas>
<div id="thumbs"></div>
</section>

<section>
<h2>4 &middot; Build &amp; export</h2>
<label>Voxel grid (per side) <select id="res"><option>64</option><option selected>96</option><option>128</option><option>160</option></select></label>
<label>Smoothing passes <select id="smooth"><option>0</option><option selected>1</option><option>2</option></select></label>
<button class="act" onclick="build()">D. Build model</button>
<button class="act" id="bStl" onclick="saveStl()" disabled>E. Download STL</button>
<canvas id="view" width="640" height="480"></canvas>
<div class="hint">Drag to rotate the preview. Units are mm, Z is up, base sits on Z=0.</div>
</section>

<section class="full"><h2>Log</h2><div id="log"></div></section>
</main>

<script>
const $=id=>document.getElementById(id),sleep=ms=>new Promise(r=>setTimeout(r,ms));
const log=m=>{const l=$('log');l.textContent+=m+'\n';l.scrollTop=l.scrollHeight};
const num=id=>parseFloat($(id).value);
let bg=null,masks=[],W=0,H=0,busy=false,liveOn=false,nextResolve=null,mesh=null;

function setUi(){document.querySelectorAll('button.act').forEach(b=>b.disabled=busy)}
async function api(p){try{const r=await fetch(p+(p.includes('?')?'&':'?')+'t='+Date.now());if(!r.ok)throw new Error(p+' -> '+r.status);return r}catch(e){log('ERROR '+e.message);throw e}}
async function grab(){const b=await(await api('/capture')).blob();const bm=await createImageBitmap(b);const c=document.createElement('canvas');c.width=bm.width;c.height=bm.height;const x=c.getContext('2d',{willReadFrequently:true});x.drawImage(bm,0,0);return x.getImageData(0,0,c.width,c.height)}

/* ---------- live view ---------- */
function liveLoop(){if(!liveOn||busy)return;const im=$('live');im.onload=()=>setTimeout(liveLoop,80);im.onerror=()=>setTimeout(liveLoop,1000);im.src='/capture?t='+Date.now()}
function toggleLive(){liveOn=!liveOn;$('bLive').textContent=liveOn?'Stop live':'Live view';if(liveOn)liveLoop()}
async function cfg(){await api('/cfg?fs='+$('fs').value+'&q='+$('q').value);bg=null;masks=[];log('camera updated - recapture the background')}
async function lock(v){await api('/lock?v='+v);log(v?'exposure/white balance locked':'auto exposure on');bg=null}

/* ---------- silhouettes ---------- */
async function capBg(){
  busy=true;setUi();const was=liveOn;liveOn=false;
  try{const fr=[];for(let i=0;i<3;i++){fr.push(await grab());await sleep(150)}
    W=fr[0].width;H=fr[0].height;const o=new ImageData(W,H),n=o.data.length;
    for(let p=0;p<n;p++)o.data[p]=(fr[0].data[p]+fr[1].data[p]+fr[2].data[p])/3;
    bg=o;masks=[];log('background captured '+W+'x'+H)}catch(e){}
  busy=false;setUi();if(was){liveOn=true;liveLoop()}
}
function erode(m,w,h){const o=new Uint8Array(m.length);for(let y=1;y<h-1;y++)for(let x=1;x<w-1;x++){const i=y*w+x;if(m[i]&&m[i-1]&&m[i+1]&&m[i-w]&&m[i+w]&&m[i-w-1]&&m[i-w+1]&&m[i+w-1]&&m[i+w+1])o[i]=1}return o}
function dilate(m,w,h){const o=new Uint8Array(m.length);for(let y=1;y<h-1;y++)for(let x=1;x<w-1;x++){const i=y*w+x;if(m[i]||m[i-1]||m[i+1]||m[i-w]||m[i+w]||m[i-w-1]||m[i-w+1]||m[i+w-1]||m[i+w+1])o[i]=1}return o}
function largest(m,w,h){
  const lab=new Int32Array(m.length),st=new Int32Array(m.length);let best=0,bestN=0,id=0;
  for(let s=0;s<m.length;s++){if(!m[s]||lab[s])continue;id++;let sp=0,cnt=0;st[sp++]=s;lab[s]=id;
    while(sp){const p=st[--sp];cnt++;const x=p%w,y=(p/w)|0;
      if(x>0&&m[p-1]&&!lab[p-1]){lab[p-1]=id;st[sp++]=p-1}
      if(x<w-1&&m[p+1]&&!lab[p+1]){lab[p+1]=id;st[sp++]=p+1}
      if(y>0&&m[p-w]&&!lab[p-w]){lab[p-w]=id;st[sp++]=p-w}
      if(y<h-1&&m[p+w]&&!lab[p+w]){lab[p+w]=id;st[sp++]=p+w}}
    if(cnt>bestN){bestN=cnt;best=id}}
  const o=new Uint8Array(m.length);if(best)for(let i=0;i<m.length;i++)if(lab[i]===best)o[i]=1;return o}
function makeMask(img){
  const d=img.data,b=bg.data,t=num('thr');let m=new Uint8Array(W*H);
  for(let i=0,p=0;i<m.length;i++,p+=4){const a=Math.abs(d[p]-b[p]),g=Math.abs(d[p+1]-b[p+1]),c=Math.abs(d[p+2]-b[p+2]);m[i]=(a>t||g>t||c>t)?1:0}
  m=dilate(erode(m,W,H),W,H);          // remove speckle
  m=erode(dilate(m,W,H),W,H);          // close pin-holes
  if($('blob').checked)m=largest(m,W,H);
  return m}
function showMask(img,m){const c=$('mask');c.width=W;c.height=H;const x=c.getContext('2d'),o=new ImageData(new Uint8ClampedArray(img.data),W,H);
  for(let i=0,p=0;i<m.length;i++,p+=4)if(m[i]){o.data[p]=255;o.data[p+1]=o.data[p+1]*.5;o.data[p+2]=o.data[p+2]*.2}
  x.putImageData(o,0,0)}
function addThumb(m){const c=document.createElement('canvas');c.width=64;c.height=Math.round(64*H/W);const x=c.getContext('2d'),id=x.createImageData(c.width,c.height);
  for(let y=0;y<c.height;y++)for(let xx=0;xx<c.width;xx++){const v=m[((y*H/c.height)|0)*W+((xx*W/c.width)|0)]?255:0,p=(y*c.width+xx)*4;id.data[p]=v;id.data[p+1]=v*.7;id.data[p+2]=0;id.data[p+3]=255}
  x.putImageData(id,0,0);c.style.cssText='width:64px;margin:2px;display:inline-block';$('thumbs').appendChild(c)}
async function testMask(){if(!bg)return alert('Capture the empty background first');busy=true;setUi();const was=liveOn;liveOn=false;
  try{const img=await grab();showMask(img,makeMask(img));log('red = detected object. Adjust threshold until only the object is red.')}catch(e){}
  busy=false;setUi();if(was){liveOn=true;liveLoop()}}

/* ---------- scan ---------- */
function nxt(){if(nextResolve){nextResolve();nextResolve=null}}
async function scan(){
  if(!bg)return alert('Capture the empty background first (with the object removed).');
  liveOn=false;$('bLive').textContent='Live view';busy=true;setUi();
  const n=num('views'),step=360/n,manual=$('manual').checked,settle=num('settle');
  masks=[];$('thumbs').innerHTML='';
  try{
    for(let i=0;i<n;i++){
      $('prog').value=i/n;$('pt').textContent='view '+(i+1)+' / '+n;
      await sleep(settle);
      const img=await grab();
      if(img.width!==W||img.height!==H)throw new Error('frame size changed - recapture background');
      const m=makeMask(img);masks.push(m);showMask(img,m);addThumb(m);
      if(i<n-1){
        if(manual){$('pt').textContent='rotate the object '+step.toFixed(1)+' deg, then press "Next view"';await new Promise(r=>nextResolve=r)}
        else await api('/rotate?deg='+step)}}
    $('prog').value=1;$('pt').textContent='scan complete';log('scan done: '+n+' views');
  }catch(e){log('ERROR '+e.message)}
  busy=false;setUi();
}

/* ---------- visual hull ---------- */
async function build(){
  if(masks.length<3)return alert('Run a scan first');
  busy=true;setUi();await sleep(30);
  const N=num('res'),R=num('rad'),Ht=num('hgt'),D=num('dist'),Hc=num('camh'),n=masks.length;
  const dth=2*Math.PI/n*($('rev').checked?-1:1);
  const f=(W/2)/Math.tan(num('fov')*Math.PI/360),cx=W/2,cy=H/2,vs=2*R/N,NZ=Math.ceil(Ht/vs);
  const occ=new Uint8Array(N*N*NZ).fill(1),t0=performance.now();
  log('carving '+N+'x'+N+'x'+NZ+' grid ('+vs.toFixed(2)+' mm voxels) from '+n+' views...');
  for(let v=0;v<n;v++){
    const m=masks[v],c=Math.cos(v*dth),s=Math.sin(v*dth);
    for(let j=0;j<N;j++){const y=(j+.5-N/2)*vs;
      for(let i=0;i<N;i++){
        const x=(i+.5-N/2)*vs,xr=x*c-y*s,yr=x*s+y*c,d=D+yr,col=j*N+i;
        const u=d>1?Math.round(cx+f*xr/d):-1;
        if(u<0||u>=W){for(let k=0;k<NZ;k++)occ[k*N*N+col]=0;continue}
        for(let k=0;k<NZ;k++){const o=k*N*N+col;if(!occ[o])continue;
          const vv=Math.round(cy-f*((k+.5)*vs-Hc)/d);
          if(vv<0||vv>=H||!m[vv*W+u])occ[o]=0}}}
    $('prog').value=(v+1)/n;await sleep(0)}
  let cnt=0;for(let i=0;i<occ.length;i++)cnt+=occ[i];
  if(cnt<20){log('ERROR: model is empty. Check mask, geometry numbers, or try "Reverse rotation direction".');busy=false;setUi();return}
  log('carved in '+((performance.now()-t0)/1000).toFixed(1)+' s, '+cnt+' voxels');
  // padded scalar field
  const nx=N+2,ny=N+2,nz=NZ+2;let F=new Float32Array(nx*ny*nz);
  for(let k=0;k<NZ;k++)for(let j=0;j<N;j++)for(let i=0;i<N;i++)F[((k+1)*ny+j+1)*nx+i+1]=occ[(k*N+j)*N+i];
  for(let p=0;p<$('smooth').value*1;p++)F=blur(F,nx,ny,nz);
  const sn=surfaceNets(F,nx,ny,nz,.5),V=new Float32Array(sn.v.length);
  for(let i=0;i<V.length;i+=3){V[i]=(sn.v[i]-1+.5-N/2)*vs;V[i+1]=(sn.v[i+1]-1+.5-N/2)*vs;V[i+2]=Math.max(0,(sn.v[i+2]-1+.5)*vs)}
  const T=sn.t;let mn=[1e9,1e9,1e9],mx=[-1e9,-1e9,-1e9];
  for(let i=0;i<V.length;i+=3)for(let a=0;a<3;a++){mn[a]=Math.min(mn[a],V[i+a]);mx[a]=Math.max(mx[a],V[i+a])}
  let vol=0;for(let i=0;i<T.length;i+=3){const a=T[i]*3,b=T[i+1]*3,c=T[i+2]*3;
    vol+=(V[a]*(V[b+1]*V[c+2]-V[b+2]*V[c+1])-V[a+1]*(V[b]*V[c+2]-V[b+2]*V[c])+V[a+2]*(V[b]*V[c+1]-V[b+1]*V[c]))/6}
  mesh={V,T,cx:(mn[0]+mx[0])/2,cy:(mn[1]+mx[1])/2,cz:(mn[2]+mx[2])/2,maxDim:Math.max(mx[0]-mn[0],mx[1]-mn[1],mx[2]-mn[2])};
  log('mesh: '+(T.length/3)+' triangles, size '+(mx[0]-mn[0]).toFixed(1)+' x '+(mx[1]-mn[1]).toFixed(1)+' x '+(mx[2]-mn[2]).toFixed(1)+' mm, volume ~'+(Math.abs(vol)/1000).toFixed(1)+' cm3');
  $('bStl').disabled=false;busy=false;setUi();$('bStl').disabled=false;render();
}
function blur(src,nx,ny,nz){
  const st=[1,nx,nx*ny],ex=[nx,ny,nz];let a=src;
  for(let ax=0;ax<3;ax++){const o=new Float32Array(a.length),s=st[ax];
    for(let p=0;p<a.length;p++){const ci=[p%nx,((p/nx)|0)%ny,(p/(nx*ny))|0][ax];
      if(ci>=1&&ci<=ex[ax]-2)o[p]=(a[p-s]+2*a[p]+a[p+s])/4}
    a=o}
  return a}
function surfaceNets(f,nx,ny,nz,iso){
  const C=[[0,0,0],[1,0,0],[0,1,0],[1,1,0],[0,0,1],[1,0,1],[0,1,1],[1,1,1]];
  const E=[[0,1],[2,3],[4,5],[6,7],[0,2],[1,3],[4,6],[5,7],[0,4],[1,5],[2,6],[3,7]];
  const idx=(i,j,k)=>(k*ny+j)*nx+i,cell=new Int32Array(nx*ny*nz).fill(-1),v=[],t=[],val=new Float32Array(8);
  for(let k=0;k<nz-1;k++)for(let j=0;j<ny-1;j++)for(let i=0;i<nx-1;i++){
    let mask=0;for(let c=0;c<8;c++){val[c]=f[idx(i+C[c][0],j+C[c][1],k+C[c][2])];if(val[c]>=iso)mask|=1<<c}
    if(mask===0||mask===255)continue;
    let sx=0,sy=0,sz=0,n=0;
    for(let e=0;e<12;e++){const a=E[e][0],b=E[e][1],va=val[a],vb=val[b];
      if((va>=iso)!==(vb>=iso)){const tt=(iso-va)/(vb-va);
        sx+=C[a][0]+tt*(C[b][0]-C[a][0]);sy+=C[a][1]+tt*(C[b][1]-C[a][1]);sz+=C[a][2]+tt*(C[b][2]-C[a][2]);n++}}
    cell[idx(i,j,k)]=v.length/3;v.push(i+sx/n,j+sy/n,k+sz/n)}
  const dim=[nx,ny,nz];
  for(let k=0;k<nz;k++)for(let j=0;j<ny;j++)for(let i=0;i<nx;i++){
    const pos=[i,j,k],s0=f[idx(i,j,k)]>=iso;
    for(let a=0;a<3;a++){
      const q=[i,j,k];q[a]++;if(q[a]>=dim[a])continue;
      const s1=f[idx(q[0],q[1],q[2])]>=iso;if(s0===s1)continue;
      const b=(a+1)%3,c=(a+2)%3;if(pos[b]<1||pos[c]<1||pos[b]>=dim[b]-1||pos[c]>=dim[c]-1)continue;
      const cl=(db,dc)=>{const p=[i,j,k];p[b]+=db;p[c]+=dc;return cell[idx(p[0],p[1],p[2])]};
      let v0=cl(-1,-1),v1=cl(0,-1),v2=cl(0,0),v3=cl(-1,0);
      if(v0<0||v1<0||v2<0||v3<0)continue;
      if(!s0){let tmp=v1;v1=v3;v3=tmp}          // flip winding
      t.push(v0,v1,v2,v0,v2,v3)}}
  return{v:new Float32Array(v),t:new Uint32Array(t)}}

/* ---------- STL ---------- */
function saveStl(){
  if(!mesh)return;const{V,T}=mesh,nt=T.length/3,buf=new ArrayBuffer(84+50*nt),dv=new DataView(buf);
  const hd='Tempad 3D Scanner';for(let i=0;i<hd.length;i++)dv.setUint8(i,hd.charCodeAt(i));
  dv.setUint32(80,nt,true);let o=84;
  for(let i=0;i<T.length;i+=3){const a=T[i]*3,b=T[i+1]*3,c=T[i+2]*3;
    const ux=V[b]-V[a],uy=V[b+1]-V[a+1],uz=V[b+2]-V[a+2],wx=V[c]-V[a],wy=V[c+1]-V[a+1],wz=V[c+2]-V[a+2];
    let nx=uy*wz-uz*wy,ny=uz*wx-ux*wz,nz=ux*wy-uy*wx;const l=Math.hypot(nx,ny,nz)||1;nx/=l;ny/=l;nz/=l;
    dv.setFloat32(o,nx,true);dv.setFloat32(o+4,ny,true);dv.setFloat32(o+8,nz,true);o+=12;
    for(const p of[a,b,c]){dv.setFloat32(o,V[p],true);dv.setFloat32(o+4,V[p+1],true);dv.setFloat32(o+8,V[p+2],true);o+=12}
    dv.setUint16(o,0,true);o+=2}
  const bl=new Blob([buf],{type:'model/stl'}),a=document.createElement('a');
  a.href=URL.createObjectURL(bl);a.download='tempad_scan_'+Date.now()+'.stl';document.body.appendChild(a);a.click();a.remove();
  log('STL saved ('+(buf.byteLength/1048576).toFixed(1)+' MB)')}

/* ---------- preview (point splat, no internet / WebGL needed) ---------- */
let ang=.6,tilt=.35,drag=false,lx=0,ly=0;
function render(){
  const c=$('view'),x=c.getContext('2d'),w=c.width,h=c.height;x.fillStyle='#000';x.fillRect(0,0,w,h);if(!mesh)return;
  const img=x.getImageData(0,0,w,h),d=img.data,zb=new Float32Array(w*h).fill(1e9),V=mesh.V;
  const ca=Math.cos(ang),sa=Math.sin(ang),cb=Math.cos(tilt),sb=Math.sin(tilt),sc=Math.min(w,h)*.85/mesh.maxDim,rg=mesh.maxDim*.9;
  for(let i=0;i<V.length;i+=3){
    const X=V[i]-mesh.cx,Y=V[i+1]-mesh.cy,Z=V[i+2]-mesh.cz,x1=X*ca-Y*sa,y1=X*sa+Y*ca;
    const up=Z*cb+y1*sb,dep=y1*cb-Z*sb,sx=(w/2+x1*sc)|0,sy=(h/2-up*sc)|0;
    const b=Math.max(.2,Math.min(1,.65-dep/rg*.5))*255;
    for(let oy=0;oy<2;oy++)for(let ox=0;ox<2;ox++){const px=sx+ox,py=sy+oy;if(px<0||py<0||px>=w||py>=h)continue;
      const q=py*w+px;if(dep<zb[q]){zb[q]=dep;d[q*4]=b;d[q*4+1]=b*.69;d[q*4+2]=0;d[q*4+3]=255}}}
  x.putImageData(img,0,0)}
const vc=$('view');
vc.addEventListener('pointerdown',e=>{drag=true;lx=e.clientX;ly=e.clientY;vc.setPointerCapture(e.pointerId)});
vc.addEventListener('pointerup',()=>drag=false);
vc.addEventListener('pointermove',e=>{if(!drag)return;ang+=(e.clientX-lx)*.01;tilt=Math.max(-1.5,Math.min(1.5,tilt+(e.clientY-ly)*.01));lx=e.clientX;ly=e.clientY;render()});
setUi();
</script></body></html>
)rawliteral";

// ============================ FIRMWARE ============================
framesize_t fsFromName(const String &s) {
  if (s == "qvga") return FRAMESIZE_QVGA;
  if (s == "svga") return FRAMESIZE_SVGA;
  if (s == "xga")  return FRAMESIZE_XGA;
  return FRAMESIZE_VGA;
}

bool initCamera() {
  camera_config_t c = {};
  c.ledc_channel = LEDC_CHANNEL_0;
  c.ledc_timer   = LEDC_TIMER_0;
  c.pin_d0 = Y2_GPIO_NUM;  c.pin_d1 = Y3_GPIO_NUM;  c.pin_d2 = Y4_GPIO_NUM;  c.pin_d3 = Y5_GPIO_NUM;
  c.pin_d4 = Y6_GPIO_NUM;  c.pin_d5 = Y7_GPIO_NUM;  c.pin_d6 = Y8_GPIO_NUM;  c.pin_d7 = Y9_GPIO_NUM;
  c.pin_xclk = XCLK_GPIO_NUM;  c.pin_pclk = PCLK_GPIO_NUM;
  c.pin_vsync = VSYNC_GPIO_NUM; c.pin_href = HREF_GPIO_NUM;
  c.pin_sccb_sda = SIOD_GPIO_NUM; c.pin_sccb_scl = SIOC_GPIO_NUM;
  c.pin_pwdn = PWDN_GPIO_NUM;  c.pin_reset = RESET_GPIO_NUM;
  c.xclk_freq_hz = CAM_XCLK_HZ;
  c.pixel_format = PIXFORMAT_JPEG;
  c.frame_size = FRAMESIZE_VGA;
  c.jpeg_quality = 12;
  c.grab_mode = CAMERA_GRAB_LATEST;
  if (psramFound()) { c.fb_count = 2; c.fb_location = CAMERA_FB_IN_PSRAM; }
  else {
    Serial.println("WARNING: PSRAM not found. Set Tools > PSRAM > OPI PSRAM. Using small frames.");
    c.frame_size = FRAMESIZE_QVGA; c.fb_count = 1; c.fb_location = CAMERA_FB_IN_DRAM;
    c.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  }

  esp_err_t err = esp_camera_init(&c);
  if (err != ESP_OK) {
    Serial.printf("ERROR: camera init failed (0x%x). 0x105 = camera not detected: reseat the ribbon cable.\n", err);
    return false;
  }

  sensor_t *s = esp_camera_sensor_get();
  if (!s) { Serial.println("ERROR: no camera sensor handle"); return false; }
  Serial.printf("Camera OK, sensor PID 0x%02X\n", s->id.PID);
  if (s->id.PID == OV3660_PID) {          // OV3660 tuning (same as earlier fixes)
    s->set_brightness(s, 1);
    s->set_saturation(s, -2);
  }
  s->set_vflip(s, CAM_VFLIP);
  s->set_hmirror(s, CAM_HMIRROR);
  return true;
}

void stepMotor(long n, bool fwd) {
  digitalWrite(EN_PIN, LOW);
  digitalWrite(DIR_PIN, (fwd ^ (ROT_DIR == 0)) ? HIGH : LOW);
  delayMicroseconds(50);
  for (long i = 0; i < n; i++) {
    digitalWrite(STEP_PIN, HIGH); delayMicroseconds(STEP_US);
    digitalWrite(STEP_PIN, LOW);  delayMicroseconds(STEP_US);
    if ((i & 0xFF) == 0xFF) yield();
  }
}

void hRoot() { server.sendHeader("Cache-Control", "no-store"); server.send_P(200, "text/html", PAGE); }

void hCapture() {
  if (!camOk) { server.send(503, "text/plain", "camera not available"); return; }
  camera_fb_t *fb = esp_camera_fb_get();     // discard a possibly stale frame
  if (fb) esp_camera_fb_return(fb);
  fb = esp_camera_fb_get();
  if (!fb) { server.send(500, "text/plain", "camera error"); return; }
  server.sendHeader("Cache-Control", "no-store");
  server.setContentLength(fb->len);
  server.send(200, "image/jpeg", "");
  WiFiClient cl = server.client();
  size_t sent = 0; uint32_t last = millis();
  while (sent < fb->len && cl.connected() && millis() - last < 3000) {
    size_t n = cl.write(fb->buf + sent, min((size_t)1436, fb->len - sent));
    if (n) { sent += n; last = millis(); } else delay(1);
  }
  esp_camera_fb_return(fb);
}

void hRotate() {
  float deg = server.arg("deg").toFloat();
  float exact = deg / 360.0f * STEPS_PER_REV + stepRemainder;
  long steps = lroundf(exact);
  stepRemainder = exact - steps;             // keep fractional steps so 36 x 10 deg = exactly 360
  stepMotor(labs(steps), steps >= 0);
  server.send(200, "text/plain", "ok");
}

void hCfg() {
  if (!camOk) { server.send(503, "text/plain", "camera not available"); return; }
  sensor_t *s = esp_camera_sensor_get();
  if (server.hasArg("fs")) s->set_framesize(s, fsFromName(server.arg("fs")));
  if (server.hasArg("q"))  s->set_quality(s, constrain(server.arg("q").toInt(), 4, 63));
  delay(200);
  for (int i = 0; i < 3; i++) { camera_fb_t *fb = esp_camera_fb_get(); if (fb) esp_camera_fb_return(fb); }
  server.send(200, "text/plain", "ok");
}

void hLock() {
  if (!camOk) { server.send(503, "text/plain", "camera not available"); return; }
  bool lock = server.arg("v") == "1";
  sensor_t *s = esp_camera_sensor_get();
  s->set_exposure_ctrl(s, !lock);
  s->set_gain_ctrl(s, !lock);
  s->set_whitebal(s, !lock);
  server.send(200, "text/plain", "ok");
}

void hLed() {
#if LED_PIN >= 0
  digitalWrite(LED_PIN, server.arg("v") == "1" ? HIGH : LOW);
#endif
  server.send(200, "text/plain", "ok");
}

void setup() {
  Serial.begin(115200);
  unsigned long t_ser = millis();
  while (!Serial && millis() - t_ser < 3000) delay(10);   // wait for USB CDC serial
  delay(500);
  Serial.println("\n=== Tempad 3D scanner (ESP32-S3) ===");

  // Start the camera first, before touching any other GPIO
  camOk = initCamera();
  if (!camOk) Serial.println("Camera failed - web UI will still start. Check ribbon cable/power, then reset.");

  pinMode(STEP_PIN, OUTPUT); pinMode(DIR_PIN, OUTPUT);
  pinMode(EN_PIN, OUTPUT);   digitalWrite(EN_PIN, HIGH);   // motor off until first move
#if LED_PIN >= 0
  pinMode(LED_PIN, OUTPUT);  digitalWrite(LED_PIN, LOW);
#endif

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 12000) { delay(300); Serial.print("."); }
  if (WiFi.status() == WL_CONNECTED) {
    WiFi.setTxPower(WIFI_POWER_19_5dBm);
    Serial.printf("\nOpen http://%s  (or http://%s.local)\n", WiFi.localIP().toString().c_str(), HOSTNAME);
  } else {
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASS);
    Serial.printf("\nWiFi failed. Join '%s' (pass %s) then open http://%s\n", AP_SSID, AP_PASS, WiFi.softAPIP().toString().c_str());
  }
  MDNS.begin(HOSTNAME);

  server.on("/", hRoot);
  server.on("/capture", hCapture);
  server.on("/rotate", hRotate);
  server.on("/cfg", hCfg);
  server.on("/lock", hLock);
  server.on("/led", hLed);
  server.begin();
}

void loop() { server.handleClient(); }
