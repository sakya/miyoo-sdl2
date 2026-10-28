#include <SDL.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#define W 320
#define H 240
static FILE *f;
static int failures;
static SDL_Renderer *r;
static SDL_Texture *t;
static void result(const char *name,int rc){if(rc<0)failures++;fprintf(f,"test,%s,%s,%s\n",name,rc<0?"FAIL":"PASS",rc<0?SDL_GetError():"");fflush(f);}
static void pause_scene(void){result("scene_flush",SDL_RenderFlush(r));SDL_RenderPresent(r);Uint32 start=SDL_GetTicks();SDL_Event e;while(SDL_GetTicks()-start<1800){while(SDL_PollEvent(&e)){if(e.type==SDL_QUIT||(e.type==SDL_KEYDOWN&&e.key.keysym.sym==SDLK_ESCAPE)){fclose(f);SDL_Quit();exit(0);}}SDL_Delay(10);}}
static void scene(int i){SDL_SetRenderDrawBlendMode(r,SDL_BLENDMODE_NONE);SDL_SetRenderDrawColor(r,20,20,35,255);result("clear",SDL_RenderClear(r));
if(i==0){for(int k=0;k<4;k++){SDL_SetRenderDrawColor(r,k==0?255:0,k==1?255:0,k==2?255:0,255);SDL_Rect q={k*80,0,80,240};result("opaque_fill",SDL_RenderFillRect(r,&q));}}
if(i==1){SDL_SetRenderDrawBlendMode(r,SDL_BLENDMODE_BLEND);SDL_SetRenderDrawColor(r,255,0,0,128);SDL_Rect a={20,20,180,140};result("alpha_fill",SDL_RenderFillRect(r,&a));SDL_SetRenderDrawColor(r,0,255,0,128);SDL_Rect b={100,70,180,140};result("alpha_overlap",SDL_RenderFillRect(r,&b));}
if(i==2){SDL_SetRenderDrawColor(r,255,255,0,255);for(int k=0;k<30;k++)result("line",SDL_RenderDrawLine(r,-20,k*8,340,239-k*7));}
if(i==3){SDL_Rect a={0,0,64,64},b={180,80,120,120},c={-20,100,100,100};result("copy",SDL_RenderCopy(r,t,NULL,&a));result("scale",SDL_RenderCopy(r,t,NULL,&b));result("clip_copy",SDL_RenderCopy(r,t,NULL,&c));}
if(i==4){result("color_mod",SDL_SetTextureColorMod(t,255,80,80));result("alpha_mod",SDL_SetTextureAlphaMod(t,160));result("blend_mode",SDL_SetTextureBlendMode(t,SDL_BLENDMODE_BLEND));SDL_Rect a={40,20,240,200};result("mod_copy",SDL_RenderCopy(r,t,NULL,&a));SDL_SetTextureColorMod(t,255,255,255);SDL_SetTextureAlphaMod(t,255);SDL_SetTextureBlendMode(t,SDL_BLENDMODE_NONE);}
if(i==5){SDL_Rect a={20,20,130,130},b={170,60,130,130};result("flip_h",SDL_RenderCopyEx(r,t,NULL,&a,0,NULL,SDL_FLIP_HORIZONTAL));result("flip_v",SDL_RenderCopyEx(r,t,NULL,&b,0,NULL,SDL_FLIP_VERTICAL));}
if(i==6){SDL_Rect vp={40,30,200,160},clip={15,15,160,120},a={-40,-40,300,260};result("viewport",SDL_RenderSetViewport(r,&vp));result("clip",SDL_RenderSetClipRect(r,&clip));SDL_SetRenderDrawColor(r,0,255,255,255);result("clipped_fill",SDL_RenderFillRect(r,&a));SDL_RenderSetClipRect(r,NULL);SDL_RenderSetViewport(r,NULL);}
fprintf(f,"scene,%d,shown\n",i);pause_scene();}
static void bench(const char *name,int kind){Uint32 start=SDL_GetTicks();int ops=0;for(int frame=0;frame<120;frame++){SDL_SetRenderDrawBlendMode(r,SDL_BLENDMODE_NONE);SDL_SetRenderDrawColor(r,0,0,0,255);SDL_RenderClear(r);for(int j=0;j<24;j++){SDL_Rect q={(j*17)%W,(j*13)%H,32,24};if(kind==0){SDL_SetRenderDrawColor(r,j*9,j*5,j*3,255);SDL_RenderFillRect(r,&q);}else if(kind==1)SDL_RenderCopy(r,t,NULL,&q);else{SDL_SetRenderDrawBlendMode(r,SDL_BLENDMODE_BLEND);SDL_SetRenderDrawColor(r,255,100,0,128);SDL_RenderFillRect(r,&q);}ops++;}if(SDL_RenderFlush(r)<0)result("benchmark_flush",-1);SDL_RenderPresent(r);}Uint32 elapsed=SDL_GetTicks()-start;fprintf(f,"benchmark,%s,frames=120,elapsed_ms=%u,operations=%d,fps=%.2f\n",name,elapsed,ops,elapsed?120000.0/elapsed:0.0);fflush(f);}
int main(int argc,char **argv){int status=2;f=fopen(argc>1?argv[1]:"miyoo-results.csv","w");if(!f)return 2;setvbuf(f,NULL,_IOLBF,0);fprintf(f,"type,name,status,detail,extra1,extra2\n");if(SDL_Init(SDL_INIT_VIDEO)<0){result("SDL_Init",-1);fclose(f);return 2;}fprintf(f,"info,video_driver,%s\n",SDL_GetCurrentVideoDriver());SDL_Window *w=SDL_CreateWindow("Miyoo test",SDL_WINDOWPOS_UNDEFINED,SDL_WINDOWPOS_UNDEFINED,W,H,0);if(!w){result("window",-1);goto end;}r=SDL_CreateRenderer(w,-1,SDL_RENDERER_ACCELERATED);if(!r){result("renderer",-1);goto end;}SDL_RendererInfo ri;if(SDL_GetRendererInfo(r,&ri)==0)fprintf(f,"info,renderer,%s,flags=%u\n",ri.name,ri.flags);t=SDL_CreateTexture(r,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STREAMING,64,64);if(!t){result("texture",-1);goto end;}uint32_t pix[64*64];for(int y=0;y<64;y++)for(int x=0;x<64;x++)pix[y*64+x]=((uint32_t)(x*4)<<24)|((uint32_t)(x*4)<<16)|((uint32_t)(y*4)<<8)|(x^y)*4;result("update",SDL_UpdateTexture(t,NULL,pix,64*4));for(int i=0;i<7;i++)scene(i);bench("opaque_fill",0);bench("texture_copy",1);bench("alpha_fill",2);status=failures?1:0;
end:if(t)SDL_DestroyTexture(t);if(r)SDL_DestroyRenderer(r);if(w)SDL_DestroyWindow(w);SDL_Quit();fclose(f);return status;}
