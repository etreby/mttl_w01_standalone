"""Portable recovery GUI. No USB access until Start is clicked."""
from pathlib import Path
import json,queue,sys,threading,datetime
from core import RecoveryError
from backend import OpenOCD
from engine import recover
from runtime import verify_runtime
from prepare import ensure_dependencies

TEXT={'title':'MTTL-W01 Recovery · 0.1.0','heading':'MTTL-W01 Stock Firmware Recovery','file':'Stock 1.0.66 FWR','browse':'Choose file','safe':'Wi-Fi board is fully disconnected from the AC assembly. Isolated power, GND, 3.3V signals and ST Link V2 wiring are checked.','write':'Restore stock firmware (unchecked: diagnose and double-backup only)','warning':'Restore replaces both firmware slots and clears standalone settings. Boot/factory/calibration areas are preserved.\nOffline mock validation only; target writes have not been validated for this tool. Backups may contain passwords.','start':'Start','busy':'Working… Do not disconnect power or USB.','done':'Write/readback verified. Actual boot and setup AP still need checking.','backup':'Diagnosis and double-backup complete. CPU halted; power cycle the board to boot.','failed':'Failed. Keep backups/logs. If writing began, do not reboot.','need':'Confirm board isolation, power and wiring first.','close':'Do not close the window while working.','working':'Backup/log folder: '}

TEXT.update(title='MTTL-W01 Recovery · 1.1',folder='Backup / logs folder',warning='Restore installs stock 1.0.66 in both slots and clears standalone settings, preserving boot/factory/calibration areas.\nPhysical-board validation is still pending. Backups may contain passwords; keep them private.')
TEXT['warning']+='\nStock 1.0.66 firmware is included. Only OpenOCD is downloaded on first use.'
STAGES={'RUNTIME':'Checking runtime integrity','CONNECT':'Connecting ST Link V2 and identifying target','BACKUP 1/2':'Full flash backup 1/2','BACKUP 2/2':'Full flash backup 2/2','FULL READBACK':'Verifying full flash readback','VERIFIED; REBOOT REQUEST':'Verified · requesting reboot'}

def home():return Path(sys.executable).resolve().parent if getattr(sys,'frozen',False) else Path(__file__).resolve().parent

class App:
    def __init__(self,root):
        import tkinter as tk
        from tkinter import ttk,filedialog,messagebox
        self.root=root;self.tk=tk;self.ttk=ttk;self.dialog=filedialog;self.message=messagebox;self.base=home();self.busy=False;self.events=queue.Queue();self.last_session=None
        self.safety=tk.BooleanVar(value=False);self.write=tk.BooleanVar(value=False)
        root.geometry('1040x760');root.minsize(900,650);root.protocol('WM_DELETE_WINDOW',self.close)
        style=ttk.Style();style.theme_use('clam');style.configure('.',font=('Segoe UI',10));style.configure('Title.TLabel',font=('Segoe UI',21,'bold'))
        box=ttk.Frame(root,padding=20);box.pack(fill='both',expand=True);box.columnconfigure(0,weight=1);box.rowconfigure(8,weight=1)
        self.title=ttk.Label(box,style='Title.TLabel');self.title.grid(row=0,column=0,sticky='w')
        self.label=ttk.Label(box);self.label.grid(row=1,column=0,sticky='w',pady=(20,6))
        self.ack=ttk.Checkbutton(box,variable=self.safety);self.ack.grid(row=3,column=0,columnspan=2,sticky='w',pady=(20,8))
        self.mode=ttk.Checkbutton(box,variable=self.write);self.mode.grid(row=4,column=0,columnspan=2,sticky='w')
        self.warning=ttk.Label(box,wraplength=950,foreground='#9a3412');self.warning.grid(row=5,column=0,columnspan=2,sticky='w',pady=14)
        self.start=ttk.Button(box,command=self.run);self.start.grid(row=6,column=0,columnspan=2,pady=8)
        self.folder=ttk.Button(box,command=self.open_folder);self.folder.grid(row=6,column=1,sticky='e')
        self.progress=ttk.Progressbar(box,maximum=100);self.progress.grid(row=7,column=0,columnspan=2,sticky='ew',pady=8)
        self.log=tk.Text(box,background='#152338',foreground='#e2e8f0',font=('Consolas',10),state='disabled',wrap='word');self.log.grid(row=8,column=0,columnspan=2,sticky='nsew')
        self.status=ttk.Label(box,wraplength=950);self.status.grid(row=9,column=0,columnspan=2,sticky='w',pady=12)
        self.label_widgets();root.after(80,self.pump)
    def label_widgets(self):
        w=TEXT;self.root.title(w['title']);self.title.configure(text=w['heading'])
        for widget,key in ((self.label,'file'),(self.ack,'safe'),(self.mode,'write'),(self.warning,'warning'),(self.start,'start')):widget.configure(text=w[key])
        self.folder.configure(text=w['folder'])
    def open_folder(self):
        import os
        path=self.last_session or self.base/'sessions';path.mkdir(parents=True,exist_ok=True);os.startfile(path)
    def close(self):
        if self.busy:self.message.showwarning('MTTL-W01',TEXT['close'])
        else:self.root.destroy()
    def run(self):
        if self.busy:return
        w=TEXT
        if not self.safety.get():self.message.showwarning('MTTL-W01',w['need']);return
        self.busy=True
        for widget in (self.start,self.ack,self.mode):widget.configure(state='disabled')
        self.status.configure(text=w['busy']);firmware=self.base/'firmware/comMTTL-W01_1.0.66.fwr';writing=self.write.get()
        self.progress['value']=0
        def worker():
            session=None
            try:
                session=self.base/'sessions'/(datetime.datetime.now().strftime('%Y%m%d-%H%M%S')+'-'+__import__('uuid').uuid4().hex[:8]);session.mkdir(parents=True,exist_ok=False)
                self.last_session=session
                with (session/'openocd.log').open('x',encoding='utf-8') as logfile:
                    lock=threading.Lock()
                    def emit(line):
                        with lock:logfile.write(line+'\n');logfile.flush()
                        self.events.put(('log',line))
                        if line in STAGES:self.events.put(('stage',STAGES[line]))
                        elif line.startswith('WRITE '):self.events.put(('stage','Writing and verifying: '+line[6:]))
                    self.events.put(('log',w['working']+str(session)))
                    emit('RUNTIME');ensure_dependencies(self.base,emit);verify_runtime(self.base)
                    backend=OpenOCD(self.base,session,emit)
                    recover(backend,session,firmware,writing,emit,lambda n,total,phase:self.events.put(('progress',(phase-1)*25+n/total*25)))
                self.events.put(('progress',100));self.events.put(('done',w['done'] if writing else w['backup']))
            except Exception as error:
                if session:
                    try:(session/'error.txt').write_text(str(error),encoding='utf-8')
                    except OSError:pass
                self.events.put(('log','ERROR: '+str(error)));self.events.put(('done',w['failed']+'\n'+str(error)))
        threading.Thread(target=worker,daemon=True).start()
    def pump(self):
        while not self.events.empty():
            kind,value=self.events.get()
            if kind=='log':self.log.configure(state='normal');self.log.insert('end',value+'\n');self.log.see('end');self.log.configure(state='disabled')
            elif kind=='progress':self.progress['value']=value
            elif kind=='stage':self.status.configure(text=value+' · '+TEXT['busy'])
            else:
                self.busy=False;self.status.configure(text=value)
                for widget in (self.start,self.ack,self.mode):widget.configure(state='normal')
        self.root.after(80,self.pump)

if __name__=='__main__':
    import tkinter as tk
    if '--self-test' in sys.argv:
        import subprocess
        from core import decode_fwr,sha
        base=home();ensure_dependencies(base,lambda _:None);raw=decode_fwr((base/'firmware/comMTTL-W01_1.0.66.fwr').read_bytes())
        checked=verify_runtime(base)
        version=subprocess.run([str(base/'tools/openocd/bin/openocd.exe'),'--version'],capture_output=True,timeout=15,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
        if version.returncode:raise RecoveryError('OpenOCD version check failed')
        root=tk.Tk();root.withdraw();App(root);root.update_idletasks();root.destroy()
        report={'self_test':'passed','runtime_files_verified':checked,'firmware_decoded_bytes':len(raw),'openocd_version_only':True,'usb_accessed':False,'device_written':False,'home':str(base)}
        (base/'self-test.json').write_text(json.dumps(report,indent=2,ensure_ascii=False),encoding='utf-8')
    else:
        root=tk.Tk();App(root);root.mainloop()
