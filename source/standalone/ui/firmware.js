/* Complete-file inspection. No socket, flash writer, boot activation or reset. */
(function(root){
  'use strict';
  // FILE_CAP matches the device upload limit in ota-live.js (0x6a014 bytes);
  // ota.py keeps a looser 0x70000 prototype bound for offline analysis.
  const LOAD=0x10006000,RAM_SPAN=0x10070000-LOAD,FILE_CAP=0x6a014;
  // The FWR XOR key is derived per file from its plain 16-byte header (see ota.py fwr_key).
  const XOR_BASE=Uint8Array.from([0xca,0x59,0x23,0xe7,0xd7,0x9a,0x84,0xe8,0x43,0xb3,0xaf,0xe7,0x94,0xb1,0xaf,0xa7]);
  const tail=Uint8Array.from([0x52,0x54,0x4b,0x57,0x69,0x6e,0,0xff,0,1,1,0,0x95,0x81,1,1,0,0,0,0]);
  function failure(code,message){const error=Error(message);error.code=code;return error;}
  function inspect(bytes){
    const view=new DataView(bytes.buffer,bytes.byteOffset,bytes.byteLength),body=view.getUint32(0,true),load=view.getUint32(4,true);
    if(load!==LOAD||body<24||body>RAM_SPAN||body+20!==bytes.length)throw Error('header');
    if(bytes.slice(8,16).some(value=>value!==255))throw Error('header marker');
    const entry=view.getUint32(16,true);
    if(!(entry&1)||(entry&~1)<LOAD+24||(entry&~1)>=LOAD+body)throw Error('entry');
    if(tail.some((value,i)=>bytes[20+i]!==value))throw Error('start table');
    let checksum=0;for(let i=0;i<bytes.length-4;i++)checksum=(checksum+bytes[i])>>>0;
    if(checksum!==view.getUint32(bytes.length-4,true))throw Error('checksum');
    let versionsText='';for(let i=16;i<bytes.length-4;i++)versionsText+=bytes[i]>=32&&bytes[i]<=126?String.fromCharCode(bytes[i]):' ';
    const versions=[...new Set(versionsText.match(/(?<![0-9.])[0-9]{1,2}\.[0-9]{1,3}\.[0-9]{1,3}(?![0-9.])/g)||[])].sort().slice(0,8);
    return {body,load,entry,checksum,versions};
  }
  function validate(bytes){
    if(!(bytes instanceof Uint8Array)||bytes.length<44||bytes.length>FILE_CAP)throw failure('fwSize','파일 크기가 검사 범위를 벗어납니다.');
    const key=XOR_BASE.map((seed,i)=>((seed^bytes[i])+1)&255),decoded=Uint8Array.from(bytes);
    for(let i=16;i<decoded.length;i++)decoded[i]^=key[i%16];
    for(const [format,data] of [['Image2 BIN',bytes],['LG U+ FWR (XOR)',decoded]]){
      try{return {...inspect(data),format,size:bytes.length};}catch(error){/* try other representation */}
    }
    throw failure('fwImage','Image2 헤더·진입점·파일 길이·체크섬 검사에 실패했습니다.');
  }
  async function sha256(bytes,cryptoApi){
    const digest=await cryptoApi.subtle.digest('SHA-256',bytes);
    return Array.from(new Uint8Array(digest),b=>b.toString(16).padStart(2,'0')).join('');
  }
  function expectedHash(actual,expected){
    if(!expected)return;
    if(!/^[0-9a-fA-F]{64}$/.test(expected))throw failure('fwHashSyntax','예상 SHA-256은 64자리 16진수여야 합니다.');
    if(actual!==expected.toLowerCase())throw failure('fwHashMismatch','예상 SHA-256과 파일이 일치하지 않습니다.');
  }
  const api={validate,sha256,expectedHash,FILE_CAP};
  if(typeof module==='object'&&module.exports)module.exports=api;
  else root.MttlFirmware=api;
})(globalThis);
