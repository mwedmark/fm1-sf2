// Part of the BFP8 against MP3 64 comparison (RUNNING.md, 2c). Reads and writes in test_fluid/cmp/ (the comparison's banks, WAVs and result files; git ignores it).
process.chdir(require('path').join(__dirname, 'cmp'));
const fs=require('fs');
function parse(f){const m={};for(const l of fs.readFileSync(f,'utf8').split('\n')){const r=l.match(/^\s*(\d+)\s+all\s+(.*)$/);if(!r)continue;
 const cols=l.slice(l.indexOf('all')+3).match(/.{1,6}/g)||[];const nums=r[2].trim().split(/\s+/).map(x=>x==='.'?null:+x);m[+r[1]]=nums;}return m;}
const a=parse('spectra_bfp8.txt'),b=parse('spectra_mp3_64.txt');
const bands=['level','63','125','250','500','1k','2k','4k','8k','16k'];
const rows=[];
for(const p in a){const x=a[p],y=b[p];if(!y)continue;const d=x.map((v,i)=>v==null||y[i]==null?null:y[i]-v);rows.push({p:+p,d,a:x,b:y});}
// mean abs diff per band
console.log('programs',rows.length);
console.log('band mean(MP3-BFP8) mean|diff| max|diff|');
bands.forEach((n,i)=>{const v=rows.map(r=>r.d[i]).filter(v=>v!=null);console.log(n.padStart(5),(v.reduce((s,x)=>s+x,0)/v.length).toFixed(2).padStart(7),(v.reduce((s,x)=>s+Math.abs(x),0)/v.length).toFixed(2).padStart(7),Math.max(...v.map(Math.abs)).toFixed(1).padStart(6));});
// worst programs by max |diff| in bands 63..8k (not level), and by treble 4k-8k
const score=r=>Math.max(...r.d.slice(1,9).filter(v=>v!=null).map(Math.abs));
const hf=r=>Math.max(...r.d.slice(7,10).filter(v=>v!=null).map(Math.abs),0);
rows.sort((p,q)=>score(q)-score(p));
console.log('\nworst 25 by max |MP3-BFP8| in any band 63..8k:');
for(const r of rows.slice(0,25))console.log(String(r.p).padStart(3),r.d.map(v=>v==null?'   .':(v>=0?'+':'')+v.toFixed(1)).map(s=>s.padStart(6)).join(''));
const big=rows.filter(r=>score(r)>=3).length, mid=rows.filter(r=>score(r)>=1.5).length;
console.log('\nprograms with a band differing >=3 dB:',big,'; >=1.5 dB:',mid,'; of',rows.length);
const lvl=rows.map(r=>r.d[0]);console.log('level diff mean',(lvl.reduce((s,x)=>s+x,0)/lvl.length).toFixed(2),'max |',Math.max(...lvl.map(Math.abs)).toFixed(1));
fs.writeFileSync('diff_rows.json',JSON.stringify(rows));
