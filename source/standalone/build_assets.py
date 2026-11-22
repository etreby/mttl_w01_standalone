"""Generate deterministic gzip C assets; output is a build artifact, not OTA."""
from pathlib import Path
import gzip
import zopfli.gzip
import hashlib
import json
import re
import argparse
import subprocess
import sys
sys.path.insert(0,str(Path(__file__).resolve().parent))
from minify_markup import minify_css,minify_html,usage
parser=argparse.ArgumentParser();parser.add_argument('--ota-test',action='store_true');options=parser.parse_args()
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'standalone/results/sdk'
if options.ota_test:OUT=ROOT/'standalone/results/network-ota-test/sdk'
OUT.mkdir(parents=True,exist_ok=True)
html=(ROOT/'standalone/ui/index.html').read_text(encoding='utf-8')
html=html.replace('<script src="firmware.js"></script>','').replace('<script src="app.js"></script>','<script src="live.js"></script>')
# Device boot starts in setup AP; show setup even before API/JS has loaded.
html=html.replace('aria-selected="true" aria-controls="device"','aria-selected="false" aria-controls="device"')
html=html.replace('aria-selected="false" aria-controls="wifi"','aria-selected="true" aria-controls="wifi"')
html=html.replace('<section id="device" class="active"','<section id="device"')
html=html.replace('<section id="wifi" role=','<section id="wifi" class="active" role=')
if options.ota_test:
    panel='''<section id="firmware" role="tabpanel" aria-labelledby="tab-firmware"><div class="panel">
<h2>Firmware</h2><div class="sensor"><span>Current version</span><b id="current-firmware-version">2.1.1</b></div>
<div class="sensor"><span>Build ID</span><b id="current-build-id">—</b></div>
<p id="ota-hint" class="muted"></p><form id="firmware-form">
<label id="ota-file-label" for="firmware-picker"></label><input id="firmware-file" type="file" accept=".fwr" hidden>
<div class="file-picker"><button id="firmware-picker" type="button">Choose file</button><span id="firmware-selected" class="muted" aria-live="polite"></span></div>
<label id="ota-sha-label" for="firmware-sha"></label><input id="firmware-sha" maxlength="64" spellcheck="false" autocomplete="off">
<div class="row"><button id="firmware-verify" type="button" hidden></button><button id="firmware-update" class="primary" disabled></button></div></form>
<div id="firmware-status" class="status" role="status" aria-live="polite"></div><p class="muted">Download firmware only from your OTA server and compare the file SHA-256 with the value it publishes before uploading.</p>
<h2>OTA server</h2><label for="pull-base">URL (http://private IPv4:port; empty = off)</label><input id="pull-base" maxlength="28" spellcheck="false" autocomplete="off">
<div class="row"><button id="pull-save" type="button">Save URL</button><button id="pull-check" type="button" disabled>Check for update</button><button id="pull-install" type="button" class="primary" hidden>Install</button></div>
<div id="pull-status" class="status" role="status" aria-live="polite"></div></div></section>'''
    html,count=re.subn(r'<section id="firmware".*?</section>',lambda _:panel,html,flags=re.S)
    assert count==1,'Missing firmware panel'
    # OTA initialization is bundled with the common controller below: it cannot
    # be lost independently while the rest of the dashboard appears healthy.
else:
    # The no-OTA device build keeps the inert firmware panel; drop or relabel
    # its preview-only copy.
    for pattern,replacement in [(r'<p class="muted">Current: standalone firmware preview.*?</p>',''),
                                (r'<progress id="firmware-progress".*?</progress>',''),
                                (r'<ol class="firmware-steps">.*?</ol>',''),
                                (r'>Simulate update<','>Update<'),
                                (r'(<div id="firmware-status"[^>]*>)[^<]*','\\1Not connected')]:
        html,count=re.subn(pattern,replacement,html,flags=re.S)
        assert count==1,'Missing preview firmware markup: '+pattern
# Device assets must not briefly display PC sample readings before JavaScript loads.
for field, value in {'ip':'—','mac':'—','online':'—','connected-ssid':'—',
                     'rssi':'— dBm','uptime':'—','total':'— W','current':'— A','all':'—'}.items():
    html, count = re.subn(r'(<(?:b|span) id="'+re.escape(field)+r'">)[^<]*(</(?:b|span)>)',
                         lambda match: match[1]+value+match[2], html)
    assert count == 1, 'Missing device placeholder: '+field
html=html.replace('<b>1.551 kWh</b>','<b>— kWh</b>').replace('<b>220.0 V</b>','<b>— V</b>')
html, count = re.subn(r'<div class="row"><button id="hold".*?</div>', '', html)
assert count == 1, 'Missing preview-only setup simulation'
for field in ('all-on','all-off','firmware-picker','firmware-verify'):
    html, count = re.subn(r'(<button id="'+field+r'")', r'\1 disabled', html)
    assert count == 1, 'Missing unavailable control: '+field
html=re.sub(r'<title>.*?</title>', '<title>MTTL-W01</title>',html,flags=re.S)
html=re.sub(r'<p class="note".*?</p>', '',html,flags=re.S)
html=re.sub(r'<span class="badge".*?</span>', '',html,flags=re.S)
html=html.replace('.note,','')
for before,after in [('>Simulate Wi-Fi scan<','>Scan Wi-Fi<'),('>Simulate connection<','>Connect<'),('>Simulate saving<','>Save to device<')]:
    assert html.count(before)==1,'Missing preview label: '+before
    html=html.replace(before,after)
assert not any(sample in html for sample in ('192.0.2.10','02:00:00:00:00:01','SIMULATED_AP','1.551 kWh','220.0 V'))
html=re.sub(r'<!--.*?-->','',html,flags=re.S)
html=re.sub(r'>\s+<','><',html)
sources=[('/', 'text/html; charset=utf-8', html.encode()),
         ('/i18n.js','application/javascript; charset=utf-8',(ROOT/'standalone/ui/i18n.js').read_bytes()),
         ('/live.js','application/javascript; charset=utf-8',(ROOT/'standalone/ui/live.js').read_bytes())]
# Bundle editor with live controller to share gzip dictionary and avoid another
# request/callback. Preview keeps its separate script for local-file use.
html=html.replace('<script src="scheduler.js"></script>','')
html=html.replace('<script src="timer.js"></script>','')
sources[0]=('/', 'text/html; charset=utf-8', html.encode())
sources[-1]=('/live.js','application/javascript; charset=utf-8',(ROOT/'standalone/ui/scheduler.js').read_bytes()+b'\n'+(ROOT/'standalone/ui/timer.js').read_bytes()+b'\n'+sources[-1][2])
if options.ota_test:
    sources[-1]=('/live.js','application/javascript; charset=utf-8',sources[-1][2]+b'\n'+(ROOT/'standalone/ui/ota-live.js').read_bytes())
# Load dependencies sequentially and retry failed downloads on every dashboard
# tab. Keep this small bootstrap inline so it needs no additional HTTP request.
def minify_js(data):
    # Drop only a leading explanatory JS block comment and whole-line comments.
    data=re.sub(rb"^/\*.*?\*/\s*", b"", data, count=1, flags=re.S)
    data=re.sub(rb"^\s*//[^\r\n]*",b"",data,flags=re.M)
    return subprocess.check_output(['node',str(ROOT/'standalone/minify_ui.cjs')],input=data)
loader=minify_js((ROOT/'standalone/ui/device-loader.js').read_bytes()).decode('utf-8')
assert '</script' not in loader
html=html.replace('<script src="i18n.js"></script>','').replace('<script src="live.js"></script>','<script>'+loader+'</script>')
sources[0]=('/', 'text/html; charset=utf-8',html.encode())
# Preview-only messages do not belong in the RAM-resident device image.
# Device scripts look messages up only through literal t('key') calls, plus
# the computed t('netState'+n) state names.
messages=json.loads(subprocess.check_output(['node','-e',
    'console.log(JSON.stringify(require(process.argv[1]).messages))',
    str(ROOT/'standalone/ui/i18n.js')],text=True,encoding='utf-8'))
used='\n'.join(data.decode('utf-8') for path,kind,data in sources if path!='/i18n.js')
keys=set(re.findall(r"\bt\(\s*['\"](\w+)['\"]\s*[,)]",used))
assert keys<=messages.keys(),'Missing messages: '+', '.join(sorted(keys-messages.keys()))
messages={key:value for key,value in messages.items() if key in keys or key.startswith('netState')}
compact='globalThis.MttlI18n={messages:'+json.dumps(messages,ensure_ascii=False,separators=(',',':'))+',text:(key,args={})=>{const value=MttlI18n.messages[key];if(value===undefined)throw Error("Missing translation: "+key);return value.replace(/\\{(\\w+)\\}/g,(_,name)=>String(args[name]??""));}};'
sources=[(path,kind,compact.encode() if path=='/i18n.js' else data) for path,kind,data in sources]
sources=[(path,kind,minify_js(data) if path.endswith('.js') else data) for path,kind,data in sources]
# Minify markup last, once every script that can add classes or elements is final.
scripts='\n'.join(data.decode('utf-8') for path,kind,data in sources if path.endswith('.js'))+'\n'+loader
html=re.sub(r'<style>(.*?)</style>',lambda m:'<style>'+minify_css(m[1],usage(html,scripts))+'</style>',html,flags=re.S)
sources[0]=('/', 'text/html; charset=utf-8',minify_html(html).encode())
lines=['/* Generated gzip UI assets. Do not edit by hand. */','#include <stddef.h>',
       'typedef struct { const char *path,*type; const unsigned char *data; size_t size; } mttl_asset;']
meta=[]
for i,(path,kind,data) in enumerate(sources):
    text=data.decode('utf-8')
    assert not re.search('[\u1100-\u11ff\u3130-\u318f\uac00-\ud7a3]',text),'Non-English text in device asset: '+path
    assert not any(word in text.lower() for word in ('draft','simulat','experimental')),'Preview text in device asset: '+path
    zipped=zopfli.gzip.compress(data,numiterations=30)
    assert gzip.decompress(zipped)==data
    lines.append('static const unsigned char mttl_asset_%d[]={%s};'%(i,','.join(map(str,zipped))))
    meta.append({'path':path,'raw_bytes':len(data),'gzip_bytes':len(zipped),'sha256':hashlib.sha256(data).hexdigest()})
assert sum(x['gzip_bytes'] for x in meta)<20000,'UI asset budget exceeded'
lines.append('static const mttl_asset mttl_assets[]={'+','.join('{"%s","%s",mttl_asset_%d,sizeof(mttl_asset_%d)}'%(p,k,i,i) for i,(p,k,d) in enumerate(sources))+'};')
(OUT/'ui_assets.h').write_text('\n'.join(lines)+'\n',encoding='utf-8')
(OUT/'ui_assets.json').write_text(json.dumps(meta,indent=2),encoding='utf-8')
print(json.dumps(meta,indent=2))
