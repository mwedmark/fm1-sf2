// Part of the BFP8 against MP3 64 comparison (RUNNING.md, 2c). Reads and writes in test_fluid/cmp/ (the comparison's banks, WAVs and result files; git ignores it).
process.chdir(require('path').join(__dirname, 'cmp'));
const fs=require('fs');
function readWav(f){const b=fs.readFileSync(f);let o=12,data;while(o<b.length){const id=b.toString('latin1',o,o+4),n=b.readUInt32LE(o+4);if(id==='data')data=[o+8,n];o+=8+n+(n&1);}
 const fr=data[1]/8,m=new Float32Array(fr);for(let i=0;i<fr;i++)m[i]=(b.readFloatLE(data[0]+8*i)+b.readFloatLE(data[0]+8*i+4))/2;return m;}
const gB=-10.2,gM=-9.8; // dB gains printed by the two runs; undone below
const hb=fs.readFileSync('spectra_bfp8.txt','utf8').match(/gain for all: ([-+\d.]+)/)[1],hm=fs.readFileSync('spectra_mp3_64.txt','utf8').match(/gain for all: ([-+\d.]+)/)[1];
const a=readWav('out_bfp8/spectra_fm1.wav'),b=readWav('out_mp3/spectra_fm1.wav');
const ka=10**(-hb/20),kb=10**(-hm/20);for(let i=0;i<a.length;i++)a[i]*=ka;for(let i=0;i<b.length;i++)b[i]*=kb;
const NN=32768;
function fft(re,im){const n=re.length;for(let i=1,j=0;i<n;i++){let bit=n>>1;for(;j&bit;bit>>=1)j^=bit;j^=bit;if(i<j){[re[i],re[j]]=[re[j],re[i]];[im[i],im[j]]=[im[j],im[i]];}}
 for(let len=2;len<=n;len<<=1){const ang=-2*Math.PI/len,wr=Math.cos(ang),wi=Math.sin(ang);for(let i=0;i<n;i+=len){let cr=1,ci=0;for(let k=0;k<len/2;k++){const u=i+k,v=i+k+len/2;const xr=re[v]*cr-im[v]*ci,xi=re[v]*ci+im[v]*cr;re[v]=re[u]-xr;im[v]=im[u]-xi;re[u]+=xr;im[u]+=xi;const t=cr*wr-ci*wi;ci=cr*wi+ci*wr;cr=t;}}}}
const hann=Float64Array.from({length:NN},(_,i)=>0.5-0.5*Math.cos(2*Math.PI*i/NN));
const BANDS=[63,125,250,500,1000,2000,4000,8000,16000],RATE=44100;
function bandPow(x,from){const re=new Float64Array(NN),im=new Float64Array(NN);for(let i=0;i<NN;i++)re[i]=(x[from+i]||0)*hann[i];fft(re,im);
 const p=BANDS.map(()=>0);for(let k=1;k<NN/2;k++){const f=k*RATE/NN;BANDS.forEach((c,j)=>{if(f>=c/Math.SQRT2&&f<c*Math.SQRT2)p[j]+=re[k]*re[k]+im[k]*im[k];});}return p;}
const dB=x=>10*Math.log10(x);
const out=[];
for(let p=0;p<128;p++){const t0=0.1+p*(0.05+3*1.6);const sig=BANDS.map(()=>0),err=BANDS.map(()=>0);let es=0,ee=0;
 for(let k=0;k<3;k++){const from=Math.round((t0+0.05+k*1.6)*RATE);const d=new Float32Array(NN);for(let i=0;i<NN;i++){d[i]=(b[from+i]||0)-(a[from+i]||0);es+=(a[from+i]||0)**2;ee+=d[i]**2;}
  const ps=bandPow(a,from),pe=bandPow(d,0);ps.forEach((v,j)=>{sig[j]+=v;err[j]+=pe[j];});}
 const top=Math.max(...sig);out.push({p,snr:dB(es/ee),bands:sig.map((s,j)=>dB(s)>dB(top)-40?dB(s/err[j]):NaN)});}
fs.writeFileSync('snr_rows.json',JSON.stringify(out));
const f=v=>isNaN(v)?'   .':v.toFixed(0).padStart(4);
const sorted=[...out].sort((x,y)=>x.snr-y.snr);
console.log('SNR of the MP3 64 render against the BFP8 render (dB), 128 programs');
const s=out.map(r=>r.snr);console.log('mean',(s.reduce((x,y)=>x+y,0)/128).toFixed(1),'median',[...s].sort((x,y)=>x-y)[64].toFixed(1),'min',Math.min(...s).toFixed(1),'max',Math.max(...s).toFixed(1));
const hist={};for(const v of s){const k=v>=60?'>=60':v>=40?'40-60':v>=30?'30-40':v>=25?'25-30':v>=20?'20-25':'<20';hist[k]=(hist[k]||0)+1;}console.log(hist);
console.log('worst 25: prog SNR | per band 63..16k');for(const r of sorted.slice(0,25))console.log(String(r.p).padStart(3),r.snr.toFixed(1).padStart(6),'|',r.bands.map(f).join(''));
