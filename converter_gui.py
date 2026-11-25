"""Small offline GUI. Run with Python 3.10+; no external packages required."""
import tkinter as tk
from tkinter import ttk,filedialog,messagebox
from pathlib import Path
import json,threading
import ac_formats as ac

def main():
    root=tk.Tk();root.title('Air Conflicts 座舱转换器 0.1');root.geometry('790x560')
    root.columnconfigure(1,weight=1)
    src=tk.StringVar();dest=tk.StringVar();target=tk.StringVar(value='db3');mode=tk.StringVar(value='bmf')
    def pick(var,title):
        p=filedialog.askdirectory(title=title)
        if p:var.set(p)
    for row,label,var in [(0,'源飞机文件夹',src),(1,'输出文件夹（独立目录）',dest)]:
        ttk.Label(root,text=label).grid(row=row,column=0,padx=12,pady=12,sticky='w')
        ttk.Entry(root,textvariable=var).grid(row=row,column=1,sticky='ew')
        ttk.Button(root,text='浏览',command=lambda v=var,l=label:pick(v,l)).grid(row=row,column=2,padx=12)
    ttk.Label(root,text='游戏内飞机目录名').grid(row=2,column=0,padx=12,pady=10,sticky='w')
    ttk.Entry(root,textvariable=target).grid(row=2,column=1,sticky='ew')
    ttk.Label(root,text='例如 db3、ki45、fw190；与现有 INI 内路径一致。').grid(row=3,column=1,sticky='w')
    ttk.Radiobutton(root,text='旧式座舱：cockpit.bmf + cockpitlight.bmf → MSH 双 UV',variable=mode,value='bmf').grid(row=4,column=0,columnspan=3,padx=12,pady=10,sticky='w')
    ttk.Radiobutton(root,text='现有 MSH：保留网格，修复材质路径与 INI 启用项',variable=mode,value='msh').grid(row=5,column=0,columnspan=3,padx=12,pady=5,sticky='w')
    ttk.Label(root,text='本工具输出覆盖包，不直接修改源文件或游戏。\n请先解压资源 ZIP；转换结果还需在完整游戏中实测。').grid(row=6,column=0,columnspan=3,padx=12,pady=10,sticky='w')
    log=tk.Text(root,height=12,wrap='word');log.grid(row=8,column=0,columnspan=3,padx=12,pady=12,sticky='nsew');root.rowconfigure(8,weight=1)
    def run():
        source,output,plane,selected=src.get(),dest.get(),target.get(),mode.get()
        if not source or not output:messagebox.showerror('缺少目录','请选择源文件夹和输出文件夹。');return
        button.config(state='disabled');log.delete('1.0','end')
        def done(result,error=None):
            button.config(state='normal');log.insert('end',result)
            if error:messagebox.showerror('转换未完成',error)
        def worker():
            try:
                report=ac.port_cockpit(source,output,plane,selected)
                result='转换完成。输出位置：'+str(Path(output).resolve())+'\n\n'+json.dumps(report,ensure_ascii=False,indent=2)
                root.after(0,lambda:done(result))
            except Exception as e:
                msg=str(e);root.after(0,lambda:done(msg,msg))
        threading.Thread(target=worker,daemon=True).start()
    button=ttk.Button(root,text='生成转换覆盖包',command=run);button.grid(row=7,column=0,columnspan=3,pady=6)
    root.mainloop()

if __name__=='__main__':main()
