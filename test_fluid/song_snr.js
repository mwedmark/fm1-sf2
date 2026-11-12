// Part of the BFP8 against MP3 64 comparison (RUNNING.md, 2c). Reads and writes in test_fluid/cmp/ (the comparison's banks, WAVs and result files; git ignores it).
process.chdir(require('path').join(__dirname, 'cmp'));
const fs=require('fs');
function readWav(f){const b=fs.readFileSync(f);let o=12,data;while(o<b.length){const id=b.toString('latin1',o,o+4),n=b.readUInt32LE(o+4);if(id==='data')data=[o+8,n];o+=8+n+(n&1);}
 const fr=data[1]/8,m=new Float32Array(fr);for(let i=0;i<fr;i++)m[i]=(b.readFloatLE(data[0]+8*i)+b.readFloatLE(data[0]+8*i+4))/2;return m;}
const songs=["TOTO.Africa K","ABBA_-_Dancing_Queen","Star-Wars-Theme-(From-'Star-Wars')","Eine-Kleine-Nachtmusik-2","bach_846_format0","IntoTheGroove"];
const rows=[];
for(let j=1;j<=6;j++){const t={};for(const b of ['bfp8','mp3_64']){const s=fs.readFileSync(`midi_${b}_${j}.txt`,'utf8');
 t[b]={name:s.split(':')[0],gain:+s.match(/gain to match ([-+\d.]+)/)[1],cut:+s.match(/(\d+) cut for the limit/)[1],lim:+s.match(/limiter held ([\d.]+)/)[1],corr:+s.match(/correlation ([\d.]+)/)[1],mean:+s.match(/mean \|diff\| ([\d.]+)/)[1],
  bands:s.match(/FM-1 - FS \(dB\)\s+(.*)/)[1].trim().split(/\s+/).map(Number),dist:+s.match(/spectral distance ([\d.]+)/)[1]};}
 const nm=t.bfp8.name.replace(/[^\w.-]+/g,'_').replace(/\.mid$/,'');
 const dir=b=>b==='bfp8'?'out_bfp8':'out_mp3';
 const a=readWav(`${dir('bfp8')}/${nm}_fm1.wav`),m=readWav(`${dir('mp3')}/${nm}_fm1.wav`);
 const ka=10**(-t.bfp8.gain/20),km=10**(-t.mp3_64.gain/20);let es=0,ee=0;const n=Math.min(a.length,m.length);for(let i=0;i<n;i++){const x=a[i]*ka,y=m[i]*km;es+=x*x;ee+=(y-x)**2;}
 t.snr=10*Math.log10(es/ee);t.nm=nm;rows.push(t);}
for(const t of rows){console.log(t.nm.slice(0,28).padEnd(28),'SNR(MP3 vs BFP8) %s dB'.replace('%s',t.snr.toFixed(1)),'| dist BFP8',t.bfp8.dist,'MP3',t.mp3_64.dist,'| level mean|diff| BFP8',t.bfp8.mean,'MP3',t.mp3_64.mean,'| corr',t.bfp8.corr,t.mp3_64.corr,'| cut',t.bfp8.cut,t.mp3_64.cut,'| gain',t.bfp8.gain,t.mp3_64.gain);
 console.log('   bands BFP8',t.bfp8.bands.join(' '),'\n   bands MP3 ',t.mp3_64.bands.join(' '));}
fs.writeFileSync('songs.json',JSON.stringify(rows));
