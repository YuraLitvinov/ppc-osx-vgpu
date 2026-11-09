import numpy as np, sys
from rec_render import rows, mm, W, H
from PIL import Image
def render_id(lo,hi,W=1024,H=768):
    zb=np.full((H,W),2.0,np.float32); ids=np.full((H,W),-1,np.int32); sh=np.zeros((H,W),np.float32)
    for k in range(lo,hi):
        r=rows[k]; n=r['nv']
        if n<=6 or r['prim']!=0: continue
        V=np.frombuffer(mm,dtype='<f4',count=n*56,offset=r['voff']).reshape(n,56)
        P=V[:,:4]; C=np.clip(V[:,8:9],0,1)[:,0]
        for t in range(n//3):
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
            c=1-a-b; m=(a>=0)&(b>=0)&(c>=0); zz=a*z[0]+b*z[1]+c*z[2]
            sub=zb[y0:y1+1,x0:x1+1]; ok=m&(zz<sub)
            if not ok.any(): continue
            sub[ok]=zz[ok]; ids[y0:y1+1,x0:x1+1][ok]=k
            sh[y0:y1+1,x0:x1+1][ok]=(a*C[3*t]+b*C[3*t+1]+c*C[3*t+2])[ok]
    return ids,sh
if __name__=='__main__':
    anch=[i for i,r in enumerate(rows) if r['nv']==3288]
    p=int(sys.argv[1]); lo,hi=anch[p],anch[p+1]
    ids,sh=render_id(lo,hi)
    np.save('ids%d.npy'%p,ids)
    Image.fromarray((np.clip(sh,0,1)*255).astype(np.uint8)).save('sh%d.png'%p)
    print(lo,hi)
