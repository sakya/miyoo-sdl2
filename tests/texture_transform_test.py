#!/usr/bin/env python3
"""Compare real staging transforms to scalar reference pixels and test cache reuse."""
import argparse
from dma_texture_test import run_test

TEST = r'''
static Uint32 reference(Uint32 c, Uint32 format, SDL_Color mod)
{
    unsigned a,r,g,b;
    if(mod.r==255 && mod.g==255 && mod.b==255 && mod.a==255)return c;
    if(format==SDL_PIXELFORMAT_ARGB8888){
        a=((c>>24)&255)*mod.a/255;
        r=((c>>16)&255)*mod.r/255;
        g=((c>>8)&255)*mod.g/255;
        b=(c&255)*mod.b/255;
        return (a<<24)|(r<<16)|(g<<8)|b;
    }
    r=(((c>>11)&31)*255/31)*mod.r/255;
    g=(((c>>5)&63)*255/63)*mod.g/255;
    b=((c&31)*255/31)*mod.b/255;
    return ((r*31/255)<<11)|((g*63/255)<<5)|(b*31/255);
}

static void check_pixels(const void *data,int pitch,Uint32 format,
                         SDL_Rect src,SDL_Color mod,SDL_RendererFlip flip)
{
    int y,x,bpp=SDL_BYTESPERPIXEL(format);
    for(y=0;y<src.h;++y)for(x=0;x<src.w;++x){
        int sx=src.x+((flip&SDL_FLIP_HORIZONTAL)?src.w-1-x:x);
        int sy=src.y+((flip&SDL_FLIP_VERTICAL)?src.h-1-y:y);
        Uint32 input=0,output=0;
        memcpy(&input,(const Uint8 *)data+(size_t)sy*pitch+sx*bpp,bpp);
        memcpy(&output,(Uint8 *)gfx.tmp.virAddr+((size_t)y*src.w+x)*bpp,bpp);
        assert(output==reference(input,format,mod));
    }
}

int main(void)
{
    Uint32 argb[256], pixels[16];
    Uint16 *rgb;
    unsigned char raw[300];
    unsigned i,m,flip,fmt,x,y;
    SDL_Color mods[]={ {255,255,255,255},{255,255,255,0},{255,255,255,128},
        {0,255,255,255},{1,127,254,1},{128,254,1,127},{0,0,0,0} };
    SDL_Rect crop={0,0,5,3}, full={0,0,4,4}, dst={0,0,4,4};
    SDL_Color neutral={255,255,255,255}, color={128,255,255,255};
    SDL_Texture tex={SDL_PIXELFORMAT_ARGB8888,4,4,NULL};
    Mini_TextureData *t;
    unsigned before_flush,before_copy,before_wait;
    void *locked=NULL;
    int pitch;
    gfx.tmp.virAddr=calloc(1,TMP_SIZE);gfx.tmp.phyAddr=100;
    assert(sizeof(mini_stage_cache)<=64);
    for(i=0;i<sizeof(raw);++i)raw[i]=(unsigned char)(i*71+13);
    for(fmt=0;fmt<2;++fmt)for(flip=0;flip<4;++flip)
        for(m=0;m<sizeof(mods)/sizeof(mods[0]);++m)
            for(x=0;x<3;++x)for(y=0;y<2;++y){
                Uint32 format=fmt?SDL_PIXELFORMAT_RGB565:SDL_PIXELFORMAT_ARGB8888;
                int stride=7*SDL_BYTESPERPIXEL(format)+1;
                crop.x=x;crop.y=y;
                Mini_StageTexture(raw+1,stride,format,crop,mods[m],(SDL_RendererFlip)flip);
                check_pixels(raw+1,stride,format,crop,mods[m],(SDL_RendererFlip)flip);
            }
    crop.x=crop.y=0;crop.w=256;crop.h=1;
    for(i=0;i<256;++i)argb[i]=i*0x01010101u;
    for(m=0;m<256;++m){
        SDL_Color all={(Uint8)m,(Uint8)m,(Uint8)m,(Uint8)m};
        SDL_Color alpha={255,255,255,(Uint8)m};
        Mini_StageTexture(argb,1024,SDL_PIXELFORMAT_ARGB8888,crop,all,SDL_FLIP_HORIZONTAL);
        check_pixels(argb,1024,SDL_PIXELFORMAT_ARGB8888,crop,all,SDL_FLIP_HORIZONTAL);
        Mini_StageTexture(argb,1024,SDL_PIXELFORMAT_ARGB8888,crop,alpha,SDL_FLIP_NONE);
        check_pixels(argb,1024,SDL_PIXELFORMAT_ARGB8888,crop,alpha,SDL_FLIP_NONE);
    }
    rgb=(Uint16 *)malloc(65536*sizeof(*rgb));assert(rgb);
    for(i=0;i<65536;++i)rgb[i]=(Uint16)i;
    crop.h=256;
    for(m=0;m<sizeof(mods)/sizeof(mods[0]);++m)for(flip=0;flip<4;++flip){
        Mini_StageTexture(rgb,512,SDL_PIXELFORMAT_RGB565,crop,mods[m],(SDL_RendererFlip)flip);
        check_pixels(rgb,512,SDL_PIXELFORMAT_RGB565,crop,mods[m],(SDL_RendererFlip)flip);
    }
    free(rgb);

    assert(Mini_CreateTexture(NULL,&tex)==0);t=(Mini_TextureData *)tex.driverdata;
    for(i=0;i<16;++i)pixels[i]=0xff804020+i;
    assert(Mini_UpdateTexture(NULL,&tex,&full,pixels,16)==0);
    before_flush=flushes;before_copy=copies;before_wait=waits;
    for(i=0;i<96;++i)
        assert(GFX_CopyTextureResident(t->data,4,4,16,t->fmt,full,dst,
            SDL_BLENDMODE_NONE,color,SDL_FLIP_NONE,t->physical,&t->dirty)==0);
    assert(flushes==before_flush+1 && waits==before_wait+12 && copies==before_copy);
    assert(!pending && mini_stage_cache.valid);
    check_pixels(pixels,16,t->fmt,full,color,SDL_FLIP_NONE);
    for(i=0;i<96;++i)
        assert(GFX_CopyTextureResident(t->data,4,4,16,t->fmt,full,dst,
            SDL_BLENDMODE_BLEND,color,SDL_FLIP_NONE,t->physical,&t->dirty)==0);
    assert(flushes==before_flush+1); /* Blend is not baked into staged pixels. */
    /* Alpha and source-rectangle changes must rebuild the shared image. */
    color.a=127;
    assert(GFX_CopyTextureResident(t->data,4,4,16,t->fmt,full,dst,
        SDL_BLENDMODE_BLEND,color,SDL_FLIP_NONE,t->physical,&t->dirty)==0);
    check_pixels(pixels,16,t->fmt,full,color,SDL_FLIP_NONE);
    crop.x=crop.y=1;crop.w=crop.h=2;
    assert(GFX_CopyTextureResident(t->data,4,4,16,t->fmt,crop,dst,
        SDL_BLENDMODE_BLEND,color,SDL_FLIP_NONE,t->physical,&t->dirty)==0);
    check_pixels(pixels,16,t->fmt,crop,color,SDL_FLIP_NONE);
    /* Failed staging sync must not leave a usable cache entry. */
    fail_flush=1;color.r=127;
    assert(GFX_CopyTextureResident(t->data,4,4,16,t->fmt,full,dst,
        SDL_BLENDMODE_BLEND,color,SDL_FLIP_NONE,t->physical,&t->dirty)<0);
    assert(!mini_stage_cache.valid && !pending);
    fail_flush=0;
    before_flush=flushes;before_copy=copies;
    for(i=0;i<96;++i)
        assert(GFX_CopyTextureResident(t->data,4,4,16,t->fmt,full,dst,
            SDL_BLENDMODE_NONE,neutral,SDL_FLIP_HORIZONTAL,t->physical,&t->dirty)==0);
    assert(flushes==before_flush+1 && copies==before_copy);
    check_pixels(pixels,16,t->fmt,full,neutral,SDL_FLIP_HORIZONTAL);
    assert(Mini_UpdateTexture(NULL,&tex,&full,pixels,16)==0 && !mini_stage_cache.valid);
    assert(GFX_CopyTextureResident(t->data,4,4,16,t->fmt,full,dst,
        SDL_BLENDMODE_NONE,color,SDL_FLIP_NONE,t->physical,&t->dirty)==0);
    assert(Mini_LockTexture(NULL,&tex,&full,&locked,&pitch)==0 && !mini_stage_cache.valid);
    *(Uint32 *)locked=0xff112233;pixels[0]=0xff112233;
    Mini_UnlockTexture(NULL,&tex);
    assert(GFX_CopyTextureResident(t->data,4,4,16,t->fmt,full,dst,
        SDL_BLENDMODE_NONE,color,SDL_FLIP_NONE,t->physical,&t->dirty)==0);
    check_pixels(pixels,16,t->fmt,full,color,SDL_FLIP_NONE);
    /* A legacy/stack-pixel upload must evict the transformed image. */
    assert(GFX_CopyTextureEx(pixels,4,4,16,t->fmt,full,dst,
        SDL_BLENDMODE_NONE,neutral,SDL_FLIP_NONE)==0 && !mini_stage_cache.valid);
    assert(GFX_CopyTextureResident(t->data,4,4,16,t->fmt,full,dst,
        SDL_BLENDMODE_NONE,color,SDL_FLIP_NONE,t->physical,&t->dirty)==0);
    Mini_DestroyTexture(NULL,&tex);assert(!mini_stage_cache.valid && !pending);
    free(gfx.tmp.virAddr);
    puts("PASS: exact ARGB/alpha channels, all RGB565 pixels, cropped/unaligned flips, staging cache keys/reuse/invalidation and failed sync; cache metadata <=64 bytes");
    return 0;
}
'''

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default='gcc')
    args = parser.parse_args()
    run_test(TEST, args.cc)
