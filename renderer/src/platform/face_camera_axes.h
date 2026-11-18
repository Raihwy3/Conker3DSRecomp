// Spatial ABXY camera input: Y left, A right, X up, B down.
#pragma once
namespace recomp3ds {
inline void face_camera_axes(bool left,bool right,bool up,bool down,float* x,float* y){
    *x=float(int(right)-int(left));*y=float(int(up)-int(down));
    if(*x && *y){*x*=0.70710678118f;*y*=0.70710678118f;}
}
}
