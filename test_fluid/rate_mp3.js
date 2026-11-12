// RUNNING.md 2d. Needs ffmpeg with libmp3lame on PATH.
// Does MP3 at 44.1 kHz keep more than MP3 at 32 kHz? Every used sample of 1mgm.sf2 (44.1 kHz source), joined with 0.1 s gaps, is coded by
// ffmpeg/libmp3lame mono at the given rate and bit rate, decoded, and compared with the source at 44.1 kHz.
const fs=require('fs'),path=require('path'),{execFileSync}=require('child_process');
const root=path.join(__dirname,'..');
const html=fs.readFileSync(path.join(root,'index.html'),'utf8');
eval(html.match(/<script id="core">([\s\S]*?)<\/script>/)[1]+'\nglobalThis.api={parseSf2};');
const buf=fs.readFileSync(path.join(root,'soundfonts/1mgm.sf2'));
const sf=globalThis.api.parseSf2(buf.buffer.slice(buf.byteOffset,buf.byteOffset+buf.length));
const parts=[];let total=0;
for(const h of sf.shdr.slice(0,-1)){const n=h.end-h.start;if(n<64||h.type!==1&&h.type!==undefined&&h.type!==0&&false)continue;const a=new Int16Array(n);for(let i=0;i<n;i++)a[i]=buf.readInt16LE(sf.smplOff+(h.start+i)*2);parts.push(a);total+=n+4410;}
const src=new Int16Array(total);let o=0;for(const a of parts){src.set(a,o);o+=a.length+4410;}
console.log('samples',parts.length,'frames',total,'=',(total/44100).toFixed(1),'s at 44.1 kHz');
const raw=Buffer.from(src.buffer);
const ff=(args,input)=>execFileSync('ffmpeg',['-hide_banner','-loglevel','error',...args],{input,maxBuffer:1<<29});
const toF=b=>{const i=new Int16Array(b.buffer.slice(b.byteOffset,b.byteOffset+b.length));return Float64Array.from(i);};
const N=8192,hann=Float64Array.from({length:N},(_,i)=>0.5-0.5*Math.cos(2*Math.PI*i/N));
function fft(re,im){const n=re.length;for(let i=1,j=0;i<n;i++){let bit=n>>1;for(;j&bit;bit>>=1)j^=bit;j^=bit;if(i<j){[re[i],re[j]]=[re[j],re[i]];[im[i],im[j]]=[im[j],im[i]];}}for(let len=2;len<=n;len<<=1){const ang=-2*Math.PI/len,wr=Math.cos(ang),wi=Math.sin(ang);for(let i=0;i<n;i+=len){let cr=1,ci=0;for(let k=0;k<len/2;k++){const u=i+k,v=i+k+len/2,xr=re[v]*cr-im[v]*ci,xi=re[v]*ci+im[v]*cr;re[v]=re[u]-xr;im[v]=im[u]-xi;re[u]+=xr;im[u]+=xi;const t=cr*wr-ci*wi;ci=cr*wi+ci*wr;cr=t;}}}}
const EDGES=[0,4000,8000,12000,14000,16000,18000,20000,22050];
function bands(x){const p=EDGES.slice(1).map(()=>0);for(let from=0;from+N<=x.length;from+=N*2){const re=new Float64Array(N),im=new Float64Array(N);for(let i=0;i<N;i++)re[i]=x[from+i]*hann[i];fft(re,im);for(let k=1;k<N/2;k++){const f=k*44100/N;for(let b=0;b<p.length;b++)if(f>=EDGES[b]&&f<EDGES[b+1])p[b]+=re[k]**2+im[k]**2;}}return p;}
const S=toF(raw),sb=bands(S),dB=x=>10*Math.log10(x);
const tot=sb.reduce((a,b)=>a+b,0);
console.log('source energy by band (share of total):',EDGES.slice(0,-1).map((e,i)=>`${e/1000}-${EDGES[i+1]/1000}k ${(sb[i]/tot*100).toFixed(3)}%`).join('  '));
const cases=[[32000,64],[32000,128],[44100,64],[44100,128],[44100,192]];
console.log('\nrate  kbit |  SNR vs 44.1k source | decoded energy above 12k / 16k vs source (dB) | MP3 bytes per s of audio');
for(const [r,k] of cases){
  // as the bank is made: down to the rate first (as the converter), then MP3, then decoded and back to 44.1 kHz for the comparison
  const pre=r===44100?raw:ff(['-f','s16le','-ar','44100','-ac','1','-i','-','-ar',String(r),'-f','s16le','-'],raw);
  const mp3=ff(['-f','s16le','-ar',String(r),'-ac','1','-i','-','-c:a','libmp3lame','-b:a',k+'k','-ar',String(r),'-f','mp3','-'],pre);
  const dec=ff(['-f','mp3','-i','-','-ar','44100','-f','s16le','-'],mp3);
  const D=toF(dec);let delay=0,best=-1;// align: libmp3lame/decoder delay
  for(let l=0;l<3000;l+=1){let s=0;for(let i=0;i<20000;i+=7)s+=S[44100+i]*D[44100+i+l];if(s>best){best=s;delay=l;}}
  const n=Math.min(S.length,D.length-delay);let es=0,ee=0;for(let i=0;i<n;i++){es+=S[i]**2;ee+=(D[i+delay]-S[i])**2;}
  const lp=b=>toF(ff(['-f','s16le','-ar','44100','-ac','1','-i','-','-af','lowpass=f=15500:poles=2,lowpass=f=15500:poles=2,lowpass=f=15500:poles=2','-f','s16le','-'],b));const Sl=lp(raw),Dl=lp(dec);let e2=0,f2=0;for(let i=0;i<n;i++){e2+=Sl[i]**2;f2+=(Dl[i+delay]-Sl[i])**2;}
  const db=bands(D.subarray(delay,delay+n));const a12=dB(db.slice(3).reduce((a,b)=>a+b,0)/sb.slice(3).reduce((a,b)=>a+b,0)),a16=dB(db.slice(5).reduce((a,b)=>a+b,0)/sb.slice(5).reduce((a,b)=>a+b,0));
  console.log(String(r).padStart(5),String(k).padStart(4),'|',dB(es/ee).toFixed(1).padStart(6),'dB, under 15.5k',dB(e2/f2).toFixed(1).padStart(5),'dB | ',a12.toFixed(1).padStart(6),a16.toFixed(1).padStart(7),'|',(mp3.length/(total/44100)).toFixed(0));
}
