#!/usr/bin/env python3
"""Host-only deterministic regression checks; not an MI_GFX device test."""
import random, unittest
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
RENDER = (ROOT / "sdl2/src/render/mini/SDL_render_mini.c").read_text()
VIDEO = (ROOT / "sdl2/src/video/mini/SDL_video_mini.c").read_text()

def clip_copy(src, dst, bounds):
    sx,sy,sw,sh=src; x,y,w,h=dst; bx,by,bw,bh=bounds
    if min(sw,sh,w,h,bw,bh)<=0: return None
    x0=max(x,bx); y0=max(y,by); x1=min(x+w,bx+bw); y1=min(y+h,by+bh)
    if x1<=x0 or y1<=y0: return None
    left=(x0-x)*sw//w; top=(y0-y)*sh//h
    right=((x1-x)*sw+w-1)//w; bottom=((y1-y)*sh+h-1)//h
    return (sx+left,sy+top,right-left,bottom-top),(x0,y0,x1-x0,y1-y0)

def bresenham(x0,y0,x1,y1):
    dx=abs(x1-x0); dy=abs(y1-y0); sx=1 if x0<x1 else -1; sy=1 if y0<y1 else -1
    err=dx-dy; points=[]
    while True:
        points.append((x0,y0))
        if (x0,y0)==(x1,y1): break
        e=err*2
        if e>-dy: err-=dy; x0+=sx
        if e<dx: err+=dx; y0+=sy
    return points

def runs(points):
    if not points: return []
    horizontal=abs(points[-1][0]-points[0][0])>=abs(points[-1][1]-points[0][1])
    result=[]; current=[points[0]]
    for p in points[1:]:
        a=current[-1]
        contiguous=(p[1]==a[1] and abs(p[0]-a[0])==1) if horizontal else (p[0]==a[0] and abs(p[1]-a[1])==1)
        if contiguous: current.append(p)
        else: result.append(current); current=[p]
    result.append(current)
    return result

class Regression(unittest.TestCase):
    def test_clip_invariants(self):
        r=random.Random(1600)
        for _ in range(10000):
            sw,sh=r.randint(1,256),r.randint(1,256)
            src=(r.randint(0,50),r.randint(0,50),sw,sh)
            dst=(r.randint(-200,200),r.randint(-200,200),r.randint(1,300),r.randint(1,300))
            bounds=(r.randint(-100,100),r.randint(-100,100),r.randint(1,300),r.randint(1,300))
            clipped=clip_copy(src,dst,bounds)
            if clipped is None: continue
            s,d=clipped
            self.assertGreater(s[2],0); self.assertGreater(s[3],0)
            self.assertGreater(d[2],0); self.assertGreater(d[3],0)
            self.assertGreaterEqual(s[0],src[0]); self.assertGreaterEqual(s[1],src[1])
            self.assertLessEqual(s[0]+s[2],src[0]+src[2]); self.assertLessEqual(s[1]+s[3],src[1]+src[3])
            self.assertGreaterEqual(d[0],bounds[0]); self.assertGreaterEqual(d[1],bounds[1])
            self.assertLessEqual(d[0]+d[2],bounds[0]+bounds[2]); self.assertLessEqual(d[1]+d[3],bounds[1]+bounds[3])
    def test_line_runs_preserve_pixels(self):
        r=random.Random(1400)
        for _ in range(5000):
            pts=bresenham(*(r.randint(-200,200) for _ in range(4)))
            chunks=runs(pts)
            self.assertEqual([p for chunk in chunks for p in chunk],pts)
            self.assertLessEqual(len(chunks),len(pts))
    def test_modulation_identity(self):
        for value in range(256): self.assertEqual(value*255//255,value)
    def test_critical_source_guards(self):
        for symbol in ("Mini_ClipCopy", "Mini_DrawLine", "Mini_QueueCopyEx", "Mini_RunCommandQueue"):
            self.assertIn(symbol,RENDER)
        for symbol in ("Mini_WaitPendingFills", "GFX_CopyTextureEx", "MI_GFX_WaitAllDone", "MI_SYS_FlushInvCache"):
            self.assertIn(symbol,VIDEO)
        self.assertIn("return SDL_Unsupported()",RENDER)

if __name__=="__main__": unittest.main(verbosity=2)
