// Browser-side .fwr inspector must accept real firmware packages: the fixtures
// in tests/fixtures by default, or the files listed in MTTL_TEST_FWR
// (path-delimiter separated; the release workflow passes the built image).
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const firmware=require('../ui/firmware.js');
const dir=path.join(__dirname,'fixtures');
const files=process.env.MTTL_TEST_FWR?process.env.MTTL_TEST_FWR.split(path.delimiter).filter(Boolean).map(name=>path.resolve(name)):
 fs.readdirSync(dir).filter(name=>name.endsWith('.fwr')).sort().map(name=>path.join(dir,name));
assert.ok(files.length>0,'No .fwr files found');
assert.equal(firmware.FILE_CAP,0x6a014,'FILE_CAP must match the device upload limit in ota-live.js');
for(const file of files){
 const name=path.basename(file),bytes=new Uint8Array(fs.readFileSync(file));
 const version=name.match(/_([0-9]+\.[0-9]+\.[0-9]+)(?:-build-[0-9a-f]{16})?\.fwr$/)?.[1];
 assert.ok(version,'Version missing from file name: '+name);
 const result=firmware.validate(bytes);
 assert.equal(result.format,'LG U+ FWR (XOR)',name);
 assert.equal(result.size,bytes.length,name);
 assert.ok(result.versions.includes(version),name+' versions '+result.versions.join(','));
}
// Negative cases: one flipped body byte breaks the checksum; oversize is rejected.
const sample=new Uint8Array(fs.readFileSync(files[files.length-1]));
const corrupt=Uint8Array.from(sample);corrupt[corrupt.length>>1]^=0x01;
assert.throws(()=>firmware.validate(corrupt),error=>error.code==='fwImage');
const header=Uint8Array.from(sample);header[3]^=0x01;
assert.throws(()=>firmware.validate(header),error=>error.code==='fwImage');
assert.throws(()=>firmware.validate(new Uint8Array(firmware.FILE_CAP+1)),error=>error.code==='fwSize');
console.log(JSON.stringify({passed:true,files:files.length,scope:'validate() over real .fwr files, corrupted body/header and oversize rejection'}));
