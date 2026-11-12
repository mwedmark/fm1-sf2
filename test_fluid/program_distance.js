// Part of the BFP8 against MP3 64 comparison (RUNNING.md, 2c). Reads and writes in test_fluid/cmp/ (the comparison's banks, WAVs and result files; git ignores it).
process.chdir(require('path').join(__dirname, 'cmp'));
const fs=require('fs'),gm=require(require('path').join(__dirname, 'gm_names.js'));
const rows=JSON.parse(fs.readFileSync('diff_rows.json')),snr=JSON.parse(fs.readFileSync('snr_rows.json'));
const rms=v=>{const x=v.slice(1).filter(q=>q!=null);return Math.sqrt(x.reduce((s,q)=>s+q*q,0)/x.length);};
let sa=0,sb=0,n=0;const ch=[];
for(const r of rows){const a=rms(r.a),b=rms(r.b);sa+=a;sb+=b;n++;ch.push({p:r.p,a,b,d:b-a});}
console.log('mean RMS band distance to FluidSynth, 128 programs: BFP8',(sa/n).toFixed(2),'MP3 64',(sb/n).toFixed(2));
ch.sort((x,y)=>y.d-x.d);console.log('programs where MP3 is farther from FluidSynth (RMS over bands), top 12:');
for(const c of ch.slice(0,12))console.log(String(c.p).padStart(3),gm[c.p].padEnd(20),'BFP8',c.a.toFixed(1),'MP3',c.b.toFixed(1),'+'+c.d.toFixed(1));
console.log('programs closer to FluidSynth with MP3, top 5:');for(const c of ch.slice(-5).reverse())console.log(String(c.p).padStart(3),gm[c.p].padEnd(20),'BFP8',c.a.toFixed(1),'MP3',c.b.toFixed(1),c.d.toFixed(1));
const gt=(t)=>ch.filter(c=>c.d>=t).length;console.log('worse by >=0.5/1/2 dB:',gt(.5),gt(1),gt(2),'; better by >=0.5:',ch.filter(c=>c.d<=-.5).length);
const st=[...snr].sort((x,y)=>x.snr-y.snr);console.log('lowest SNR programs:');for(const r of st.slice(0,15))console.log(String(r.p).padStart(3),gm[r.p].padEnd(20),r.snr.toFixed(1));
// per-band mean for FS-distance
const bn=['63','125','250','500','1k','2k','4k','8k','16k'];const mean=(rs,k)=>{const x=rs.map(r=>r[k]).filter(q=>q!=null);return x.reduce((s,q)=>s+q,0)/x.length;};
console.log('band mean FM-1 minus FluidSynth: ',bn.map((b,i)=>b+' '+mean(rows,'a')?'':'').join(''));
for(const w of ['a','b']){console.log(w==='a'?'BFP8':'MP3 ',bn.map((b,i)=>{const x=rows.map(r=>r[w][i+1]).filter(q=>q!=null);return b+' '+(x.reduce((s,q)=>s+q,0)/x.length).toFixed(1);}).join('  '));}
const prog=(r,w)=>r[w].slice(1);
