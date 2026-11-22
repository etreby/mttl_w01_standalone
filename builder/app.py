"""Windows GUI and optional CLI for the portable builder."""
from pathlib import Path
import argparse,json,os,queue,sys,threading,time,traceback
from engine import Engine,Cancelled

def bundle():return Path(getattr(sys,'_MEIPASS',Path(__file__).parent))
def home():return Path(sys.executable).resolve().parent if getattr(sys,'frozen',False) else Path(__file__).parent/'local-test'

class App:
    def __init__(self,root,base,lock,source=''):
        import tkinter as tk
        from tkinter import ttk,filedialog,messagebox
        self.tk=tk;self.ttk=ttk;self.dialog=filedialog;self.message=messagebox;self.root=root;self.base=base;self.lock=lock
        self.events=queue.Queue();self.cancel=threading.Event();self.busy=False;self.close_after=False;self.logfile=None
        self.source=tk.StringVar(value=source);self.status=tk.StringVar(value='Select source and prepare the environment.');self.accept=tk.BooleanVar(value=False)
        root.title('MTTL-W01 Firmware Builder · 1.0.0');root.geometry('1100x740');root.minsize(1000,640)
        style=ttk.Style();style.theme_use('clam');style.configure('.',font=('Segoe UI',10));style.configure('TFrame',background='#f1f5f9');style.configure('TLabel',background='#f1f5f9',foreground='#172b44');style.configure('Title.TLabel',font=('Segoe UI',20,'bold'));style.configure('TButton',padding=(13,9));style.configure('TCheckbutton',background='#f1f5f9');style.configure('Accent.TButton',background='#172b44',foreground='white');style.map('Accent.TButton',background=[('active','#29435f')])
        main=ttk.Frame(root,padding=22);main.pack(fill='both',expand=True);main.columnconfigure(0,weight=1);main.rowconfigure(7,weight=1)
        ttk.Label(main,text='MTTL-W01 Firmware Builder',style='Title.TLabel').grid(row=0,column=0,sticky='w')
        ttk.Label(main,text='Windows x64').grid(row=1,column=0,sticky='w',pady=(4,18))
        ttk.Label(main,text='Source folder (contains mttl-source.json)').grid(row=2,column=0,sticky='w')
        picker=ttk.Frame(main);picker.grid(row=3,column=0,sticky='ew',pady=(6,10));picker.columnconfigure(0,weight=1)
        self.entry=ttk.Entry(picker,textvariable=self.source);self.entry.grid(row=0,column=0,sticky='ew',ipady=7)
        self.choose=ttk.Button(picker,text='Browse folder',command=self.choose_source);self.choose.grid(row=0,column=1,padx=(8,0))
        self.import_button=ttk.Button(picker,text='Import source ZIP',command=self.import_zip);self.import_button.grid(row=0,column=2,padx=(8,0))
        terms=ttk.Frame(main);terms.grid(row=4,column=0,sticky='ew')
        self.agree=ttk.Checkbutton(terms,text='I acknowledge upstream downloads and third-party license terms.',variable=self.accept);self.agree.pack(side='left')
        self.notices=ttk.Button(terms,text='License notices',command=lambda:os.startfile(str(self.base/'THIRD_PARTY_NOTICES.md')));self.notices.pack(side='right')
        actions=ttk.Frame(main);actions.grid(row=5,column=0,sticky='ew',pady=14)
        self.prepare=ttk.Button(actions,text='1. Prepare / check tools',command=lambda:self.start('prepare'));self.prepare.pack(side='left')
        self.build=ttk.Button(actions,text='2. Build firmware',style='Accent.TButton',command=lambda:self.start('build'));self.build.pack(side='left',padx=8)
        self.cancel_button=ttk.Button(actions,text='Cancel',state='disabled',command=self.cancel.set);self.cancel_button.pack(side='left')
        ttk.Button(actions,text='Output folder',command=lambda:self.open_folder('output')).pack(side='right')
        ttk.Button(actions,text='Logs folder',command=lambda:self.open_folder('logs')).pack(side='right',padx=8)
        self.bar=ttk.Progressbar(main,mode='indeterminate');self.bar.grid(row=6,column=0,sticky='ew',pady=(0,10))
        logframe=ttk.Frame(main);logframe.grid(row=7,column=0,sticky='nsew');logframe.columnconfigure(0,weight=1);logframe.rowconfigure(0,weight=1)
        self.text=tk.Text(logframe,wrap='word',font=('Consolas',10),background='#152238',foreground='#e2e8f0',relief='flat',padx=12,pady=10,state='disabled');self.text.grid(row=0,column=0,sticky='nsew');scroll=ttk.Scrollbar(logframe,command=self.text.yview);scroll.grid(row=0,column=1,sticky='ns');self.text.configure(yscrollcommand=scroll.set)
        ttk.Label(main,textvariable=self.status,wraplength=920).grid(row=8,column=0,sticky='w',pady=(12,5))
        ttk.Label(main,text='Local folders only · No persistent PATH/environment/registry changes · Use a short ASCII path').grid(row=9,column=0,sticky='w')
        total=sum(x['bytes'] for x in lock['artifacts'])/1048576
        self.append(f'Working folder: {base}\nInitial download approximately {total:.1f} MiB · Offline builds available after preparation\nSource can execute code. Only select trusted source.\n')
        if Engine(base,lock).ready():self.status.set('Tools ready · Select source and build.')
        root.protocol('WM_DELETE_WINDOW',self.close);root.after(100,self.pump)
    def append(self,line):
        self.text.configure(state='normal');self.text.insert('end',line+'\n');self.text.see('end');self.text.configure(state='disabled')
    def choose_source(self):
        value=self.dialog.askdirectory(title='Select the source folder containing mttl-source.json')
        if value:self.source.set(value)
    def open_folder(self,name):
        p=self.base/name;p.mkdir(parents=True,exist_ok=True);os.startfile(str(p))
    def import_zip(self):
        value=self.dialog.askopenfilename(title='Select source ZIP',filetypes=[('Source ZIP','*.zip')])
        if not value:return
        try:
            dest=self.base/'source'/('import-'+time.strftime('%Y%m%d-%H%M%S'))
            if dest.exists():raise RuntimeError('An import with this timestamp exists. Try again shortly.')
            Engine(self.base,self.lock).extract(Path(value),dest,'source')
            candidates=list(dest.rglob('mttl-source.json'))
            if len(candidates)!=1:raise RuntimeError('The ZIP must contain exactly one source manifest.')
            self.source.set(str(candidates[0].parent));self.append('Source imported: '+str(candidates[0].parent))
        except Exception as error:self.message.showerror('Import failed',str(error))
    def start(self,action):
        if self.busy:return
        if not self.accept.get():self.message.showinfo('Acknowledgement required','Read the license notices and select the acknowledgement checkbox.');return
        source=self.source.get()
        if action=='build' and not source:self.message.showinfo('Select source','Select a source folder first.');return
        self.busy=True;self.cancel.clear();self.status.set('Preparing tools…' if action=='prepare' else 'Building…');self.bar.start(12)
        for widget in (self.entry,self.choose,self.import_button,self.prepare,self.build,self.agree):widget.configure(state='disabled')
        self.cancel_button.configure(state='normal')
        logdir=self.base/'logs';logdir.mkdir(parents=True,exist_ok=True);self.logfile=(logdir/(time.strftime('%Y%m%d-%H%M%S')+'-'+action+'.log')).open('w',encoding='utf-8')
        def worker():
            try:
                engine=Engine(self.base,self.lock,log=lambda s:self.events.put(('log',s)),cancel=self.cancel)
                if action=='prepare':engine.prepare();result='Tools ready · You can now build firmware.'
                else:result='Build succeeded: '+str(engine.build(source))
                self.events.put(('done',result))
            except Cancelled as e:self.events.put(('cancelled',str(e)))
            except Exception as e:self.events.put(('log',traceback.format_exc()));self.events.put(('error',str(e)))
        threading.Thread(target=worker,daemon=True).start()
    def pump(self):
        while True:
            try:kind,value=self.events.get_nowait()
            except queue.Empty:break
            if kind=='log':
                self.append(value)
                if self.logfile:self.logfile.write(value+'\n');self.logfile.flush()
            else:
                self.append(value);self.status.set(value);self.busy=False;self.bar.stop()
                for widget in (self.entry,self.choose,self.import_button,self.prepare,self.build,self.agree):widget.configure(state='normal')
                self.cancel_button.configure(state='disabled')
                if self.logfile:self.logfile.write(value+'\n');self.logfile.close();self.logfile=None
                if self.close_after:self.root.destroy();return
        self.root.after(100,self.pump)
    def close(self):
        if self.busy:
            if self.message.askyesno('Task running','Cancel the running task and exit?'):
                self.close_after=True;self.cancel.set();self.status.set('Cancelling before exit…')
        else:self.root.destroy()

def main():
    if sys.stdout and hasattr(sys.stdout,'reconfigure'):sys.stdout.reconfigure(encoding='utf-8',errors='replace')
    parser=argparse.ArgumentParser();parser.add_argument('--home',type=Path);parser.add_argument('--source',default='');parser.add_argument('--prepare',action='store_true');parser.add_argument('--build',action='store_true');parser.add_argument('--accept-licenses',action='store_true');args=parser.parse_args()
    base=(args.home or home()).resolve();lock=json.loads((bundle()/'dependencies.lock.json').read_text(encoding='utf-8'))
    if args.prepare or args.build:
        if not args.accept_licenses:raise SystemExit('Use --accept-licenses after reading the notices.')
        logdir=base/'logs';logdir.mkdir(parents=True,exist_ok=True)
        with (logdir/(time.strftime('%Y%m%d-%H%M%S')+'-cli.log')).open('w',encoding='utf-8') as out:
            def log(s):
                if sys.stdout:print(s,flush=True)
                out.write(s+'\n');out.flush()
            engine=Engine(base,lock,log=log)
            try:
                if args.prepare:engine.prepare()
                if args.build:engine.build(args.source)
            except Exception:log(traceback.format_exc());raise SystemExit(1)
    else:
        import tkinter as tk
        root=tk.Tk();App(root,base,lock,args.source);root.mainloop()
if __name__=='__main__':main()
