#!/usr/bin/env python3
"""Algorithmic operation-count model, NOT device timing or FPS."""
from phase16_regression import bresenham, runs
samples=[((0,0,100,10),'shallow'),((0,0,100,100),'diagonal'),((0,0,10,100),'steep'),((0,0,100,0),'horizontal')]
print('case,old_pixel_fills,new_run_fills,estimated_waits_with_batch8')
for (x0,y0,x1,y1),name in samples:
    pts=bresenham(x0,y0,x1,y1); count=len(runs(pts))
    print(f'{name},{len(pts)},{count},{(count+7)//8}')
