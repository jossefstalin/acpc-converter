const assert=require('node:assert/strict'),a=require('../viewer/codec.js');
const m={p:[[0,0,0],[1,0,0],[0,1,0]],n:[[0,0,1],[0,0,1],[0,0,1]],uv:[[0,0],[1,0],[0,1]],light:[[.1,.2],[.3,.4],[.5,.6]],idx:[0,1,2],groups:[{start:0,count:3,mat:0}],materials:['test']};
let msh=a.readMsh(a.writeMsh(m));assert.equal(msh.idx.length,3);assert(a.alignment(msh,msh.idx)>0);assert(Math.abs(msh.light[0][0]-.1)<1e-6);
let bmf=a.readBmf(a.writeBmf(m));assert.equal(bmf.idx.length,3);assert(a.alignment(bmf,bmf.idx)<0);
let rgba=new Uint8Array([128,64,32,255]);assert.deepEqual(a.readDds(a.rgbaDds(1,1,rgba)).data,rgba);
let obj=a.readObj('v 0 0 0\nv 1 0 0\nv 0 1 0\nvt 0 0\nvt 1 0\nvt 0 1\nf 1/1 2/2 3/3');assert.equal(obj.idx.length,3);assert(obj.p.every(p=>p.every(Number.isFinite)));
console.log('JavaScript codec tests passed');
