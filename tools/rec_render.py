import numpy as np, pickle, sys, os
from PIL import Image
d=os.environ.get('REC_DIR','record/pump-rec')  # a PPCGPU_RECORD directory with index.pkl (tools/rec_load.py)
rows=pickle.load(open(d+'/index.pkl','rb'))
mm=np.memmap(d+'/draws.bin',dtype=np.uint8,mode='r')
W,H=512,384
def render(lo,hi,col=1):
    img=np.zeros((H,W,3),np.float32); zb=np.full((H,W),2.0,np.float32)
    for r in rows[lo:hi]:
        n=r['nv']
        if n<=6 or r['prim']!=0: continue
        V=np.frombuffer(mm,dtype='<f4',count=n*56,offset=r['voff']).reshape(n,56)
        P=V[:,:4]; C=np.clip(V[:,4+4*col:7+4*col],0,1)
        T=n//3
        for t in range(T):
            p=P[3*t:3*t+3]
            if (p[:,3]<=1e-6).any(): continue
            x=(p[:,0]/p[:,3]*0.5+0.5)*W; y=(0.5-p[:,1]/p[:,3]*0.5)*H; z=p[:,2]/p[:,3]
            x0,x1=int(max(0,np.floor(x.min()))),int(min(W-1,np.ceil(x.max())))
            y0,y1=int(max(0,np.floor(y.min()))),int(min(H-1,np.ceil(y.max())))
            if x0>x1 or y0>y1: continue
            den=(y[1]-y[2])*(x[0]-x[2])+(x[2]-x[1])*(y[0]-y[2])
            if abs(den)<1e-9: continue
            gx,gy=np.meshgrid(np.arange(x0,x1+1)+0.5,np.arange(y0,y1+1)+0.5)
            a=((y[1]-y[2])*(gx-x[2])+(x[2]-x[1])*(gy-y[2]))/den
            b=((y[2]-y[0])*(gx-x[2])+(x[0]-x[2])*(gy-y[2]))/den
            c=1-a-b
            m=(a>=0)&(b>=0)&(c>=0)
            zz=a*z[0]+b*z[1]+c*z[2]
            sub=zb[y0:y1+1,x0:x1+1]; ok=m&(zz<sub)
            if not ok.any(): continue
            sub[ok]=zz[ok]
            col3=(a[...,None]*C[3*t]+b[...,None]*C[3*t+1]+c[...,None]*C[3*t+2])
            img[y0:y1+1,x0:x1+1][ok]=col3[ok]
    return Image.fromarray((img*255).astype(np.uint8))
if __name__=='__main__':
    lo=int(sys.argv[1]); step=int(sys.argv[2]); k=int(sys.argv[3]); col=int(sys.argv[4]); out=sys.argv[5]
    ims=[render(lo+i*step,lo+(i+1)*step,col) for i in range(k)]
    sheet=Image.new('RGB',(W*3,H*((k+2)//3)))
    for i,im in enumerate(ims): sheet.paste(im,((i%3)*W,(i//3)*H))
    sheet.save(out)
