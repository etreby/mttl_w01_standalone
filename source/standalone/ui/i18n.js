(function(root){
  'use strict';
  // UI messages that scripts look up by key. Static page text lives in index.html.
  const messages={
    title:'MTTL-W01 standalone firmware preview',
    deviceDefault:'Living room power strip',channelOneDefault:'Living room fan',channelDefault:'Channel {number}',
    editChannel:'Edit channel {number} name',
    nameInvalid:'Enter a name for the device and every channel. Each name must fit within 64 UTF-8 bytes and contain no control characters.',
    namesSaved:'Names saved.',namesSession:'Names updated. This environment keeps them only for the current page session.',browserNames:'Names saved in this browser.',
    online:'Online · simulated',duration:'{days}d {hours}h {minutes}m {seconds}s',
    partial:'PARTIAL',power:'Power',channelMeter:'Meter',current:'Current',temperature:'Temp',timer:'Timer',turnOff:'Turn OFF',turnOn:'Turn ON',
    apReady:'Setup AP ready · 192.168.4.1',ssidInvalid:'Select an SSID. It must fit within 32 UTF-8 bytes.',
    wifiConnected:'Connection and DHCP simulated · 192.0.2.10',enabled:'Enabled · simulated connection',disabled:'Disabled',
    noSelectedFile:'No file selected',unknown:'Unknown',match:'matches',chooseFile:'Choose a file, then inspect it.',
    hashCopied:'SHA-256 copied.',copyFailed:'Copy failed. Select the value manually.',
    noFile:'Choose a firmware file.',fwSize:'The file size is outside the inspection budget.',fwCrypto:'SHA-256 inspection is unavailable in this browser context.',
    inspecting:'Inspecting file…',inspectionPassed:'Format and checksum passed. You can simulate the update process.',
    fwImage:'Image2 header, entry point, file length or checksum validation failed.',fwHashSyntax:'Expected SHA-256 must contain 64 hexadecimal digits.',
    fwHashMismatch:'The file does not match the expected SHA-256.',fwRead:'The file could not be read or inspected.',
    uploading:'Simulating upload…',rechecking:'Simulating verification after upload…',rebooting:'Simulating restart and reconnection…',
    updateComplete:'Simulation complete. The real device and firmware version have not changed.',
    log_started:'Preview simulation started',log_relay:'Channel {number}: {state}',log_allOn:'All ON',log_allOff:'All OFF',
    log_provision:'Setup mode requested',log_scan:'Wi-Fi scan completed',log_wifi:'Wi-Fi connection simulation completed',log_mqttOn:'MQTT enabled',log_mqttOff:'MQTT disabled',
    log_fwPassed:'Firmware inspection passed',log_fwFailed:'Firmware inspection failed',log_fwComplete:'Firmware update simulation completed',log_names:'Device and channel names changed',
    apiUnavailable:'No response',scanProgress:'Scanning…',scanDone:'Scan completed',
    scanFailed:'Wi-Fi scan could not start. Close other device tabs and retry.',scanTimeout:'Wi-Fi scan timed out. Please retry shortly.',
    connectAccepted:'Connection requested. After the AP closes, open the device at its new IP.',
    netState0:'Stopped',netState1:'Stopping',netState2:'Setup AP',netState3:'Connecting',netState4:'Obtaining IP',netState5:'Connected',netState6:'Fault'
  };
  const api={messages,text:(key,args={})=>{
    const message=messages[key];
    if(message===undefined)throw Error('Missing translation: '+key);
    return message.replace(/\{(\w+)\}/g,(_,name)=>String(args[name]??''));
  }};
  if(typeof module==='object'&&module.exports)module.exports=api;else root.MttlI18n=api;
})(globalThis);
