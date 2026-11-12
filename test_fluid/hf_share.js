// RUNNING.md 2d: how much of each sample of 1mgm.sf2 (44.1 kHz source) lies above 16 kHz, and what a bank costs in BFP8 bytes when only
// the sounds over a threshold are kept at 44.1 kHz and the rest at 32 kHz.
const fs=require('fs'),path=require('path');const root=path.join(__dirname,'..');
const html=fs.readFileSync(path.join(root,'index.html'),'utf8');
eval(html.match(/<script id="core">([\s\S]*?)<\/script>/)[1]+'\nglobalThis.api={parseSf2};');
const buf=fs.readFileSync(path.join(root,'soundfonts/1mgm.sf2'));
const sf=globalThis.api.parseSf2(buf.buffer.slice(buf.byteOffset,buf.byteOffset+buf.length));
let N=4096,hann;const setN=n=>{N=n;hann=Float64Array.from({length:N},(_,i)=>0.5-0.5*Math.cos(2*Math.PI*i/N));};setN(4096);
function fft(re,im){const n=re.length;for(let i=1,j=0;i<n;i++){let bit=n>>1;for(;j&bit;bit>>=1)j^=bit;j^=bit;if(i<j){[re[i],re[j]]=[re[j],re[i]];[im[i],im[j]]=[im[j],im[i]];}}for(let len=2;len<=n;len<<=1){const ang=-2*Math.PI/len,wr=Math.cos(ang),wi=Math.sin(ang);for(let i=0;i<n;i+=len){let cr=1,ci=0;for(let k=0;k<len/2;k++){const u=i+k,v=i+k+len/2,xr=re[v]*cr-im[v]*ci,xi=re[v]*ci+im[v]*cr;re[v]=re[u]-xr;im[v]=im[u]-xi;re[u]+=xr;im[u]+=xi;const t=cr*wr-ci*wi;ci=cr*wi+ci*wr;cr=t;}}}}
const rows=[];
for(const h of sf.shdr.slice(0,-1)){const n=h.end-h.start;if(n<256)continue;setN(Math.min(4096,2**Math.floor(Math.log2(n))));const x=new Float64Array(n);for(let i=0;i<n;i++)x[i]=buf.readInt16LE(sf.smplOff+(h.start+i)*2);
 let hi=0,all=0;for(let from=0;from+N<=n;from+=N){const re=new Float64Array(N),im=new Float64Array(N);for(let i=0;i<N;i++)re[i]=x[from+i]*hann[i];fft(re,im);for(let k=1;k<N/2;k++){const p=re[k]**2+im[k]**2;all+=p;if(k*44100/N>=16000)hi+=p;}}
 rows.push({name:h.name,n,share:hi/all});}
const frames32=r=>r.n*32000/44100;const tot=rows.reduce((s,r)=>s+r.n,0);
console.log('samples >= 256 frames:',rows.length,'of',sf.shdr.length-1,'; frames',tot);
for(const th of [1,0.1,0.01]){const sel=rows.filter(r=>r.share>=th/100);const fsel=sel.reduce((s,r)=>s+r.n,0);
 const bytes32=tot*32000/44100*33/32,bytesAll44=tot*33/32,hyb=(tot-fsel)*32000/44100*33/32+fsel*33/32;
 console.log(`share above 16 kHz >= ${th}% (${(-10*Math.log10(100/th)).toFixed(0)} dB): ${sel.length} samples, ${(fsel/tot*100).toFixed(0)}% of the frames; BFP8 bytes all at 32k ${(bytes32/1024).toFixed(0)} KB, all at 44.1k ${(bytesAll44/1024).toFixed(0)} KB, only those at 44.1k ${(hyb/1024).toFixed(0)} KB`);}
console.log('top by share:',rows.sort((a,b)=>b.share-a.share).slice(0,8).map(r=>`${r.name} ${(r.share*100).toFixed(1)}%`).join(', '));
