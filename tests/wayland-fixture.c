#define _POSIX_C_SOURCE 200809L
#include <wayland-server.h>
#include <wayland-server-protocol.h>
#include "layer-shell-server.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static struct wl_display *display;
static struct wl_resource *layer,*buffer,*callback;
static uint32_t *screen;
static int configured,frames,rectangles;
static struct {int x,y,w,h;} damage[4096];
static void destroy(struct wl_client*c,struct wl_resource*r){(void)c;wl_resource_destroy(r);}
static void region_rect(struct wl_client*c,struct wl_resource*r,int32_t x,int32_t y,int32_t w,int32_t h){(void)c;(void)r;(void)x;(void)y;(void)w;(void)h;}
static const struct wl_region_interface region_impl={.destroy=destroy,.add=region_rect,.subtract=region_rect};
static void make_region(struct wl_client*c,struct wl_resource*r,uint32_t id){(void)r;struct wl_resource*n=wl_resource_create(c,&wl_region_interface,1,id);wl_resource_set_implementation(n,&region_impl,NULL,NULL);}
static void attach(struct wl_client*c,struct wl_resource*r,struct wl_resource*b,int32_t x,int32_t y){(void)c;(void)r;assert(x==0&&y==0);if(buffer&&buffer!=b)wl_buffer_send_release(buffer);buffer=b;}
static void mark(struct wl_client*c,struct wl_resource*r,int32_t x,int32_t y,int32_t w,int32_t h){(void)c;(void)r;assert(rectangles<4096);assert(x>=0&&y>=0&&w>0&&h>0&&x+w<=1920&&y+h<=1080);damage[rectangles++]=(typeof(damage[0])){x,y,w,h};}
static void frame(struct wl_client*c,struct wl_resource*r,uint32_t id){(void)r;assert(!callback);callback=wl_resource_create(c,&wl_callback_interface,1,id);}
static void set_region(struct wl_client*c,struct wl_resource*r,struct wl_resource*g){(void)c;(void)r;(void)g;}
static void scalar(struct wl_client*c,struct wl_resource*r,int32_t s){(void)c;(void)r;(void)s;}
static void commit(struct wl_client*c,struct wl_resource*r){(void)c;(void)r;if(!configured){configured=1;assert(layer);zwlr_layer_surface_v1_send_configure(layer,wl_display_next_serial(display),1920,1080);return;}if(!buffer)return;struct wl_shm_buffer*b=wl_shm_buffer_get(buffer);assert(b);assert(wl_shm_buffer_get_width(b)==1920&&wl_shm_buffer_get_height(b)==1080);int stride=wl_shm_buffer_get_stride(b);wl_shm_buffer_begin_access(b);unsigned char*pixels=wl_shm_buffer_get_data(b);for(int i=0;i<rectangles;i++)for(int y=damage[i].y;y<damage[i].y+damage[i].h;y++)memcpy(screen+y*1920+damage[i].x,pixels+y*stride+damage[i].x*4,(size_t)damage[i].w*4);assert(!memcmp(screen,pixels,1920u*1080*4));wl_shm_buffer_end_access(b);rectangles=0;frames++;FILE*f=fopen("frames","w");assert(f);fprintf(f,"%d\n",frames);fclose(f);if(callback){wl_callback_send_done(callback,(uint32_t)frames*83);wl_resource_destroy(callback);callback=NULL;}if(frames>=36){puts("PASS: 36 real Wayland frame commits reconstruct pixel-perfect output from partial damage and alternating shm buffers.");fflush(stdout);wl_display_terminate(display);}}
static const struct wl_surface_interface surface_impl={.destroy=destroy,.attach=attach,.damage=mark,.frame=frame,.set_opaque_region=set_region,.set_input_region=set_region,.commit=commit,.set_buffer_transform=scalar,.set_buffer_scale=scalar};
static void make_surface(struct wl_client*c,struct wl_resource*r,uint32_t id){configured=0;frames=0;rectangles=0;buffer=NULL;callback=NULL;layer=NULL;memset(screen,0,1920u*1080*4);struct wl_resource*n=wl_resource_create(c,&wl_surface_interface,wl_resource_get_version(r),id);wl_resource_set_implementation(n,&surface_impl,NULL,NULL);}
static const struct wl_compositor_interface compositor_impl={.create_surface=make_surface,.create_region=make_region};
static void bind_compositor(struct wl_client*c,void*d,uint32_t version,uint32_t id){(void)d;struct wl_resource*r=wl_resource_create(c,&wl_compositor_interface,(int)version,id);wl_resource_set_implementation(r,&compositor_impl,NULL,NULL);}
static void bind_output(struct wl_client*c,void*d,uint32_t version,uint32_t id){(void)d;struct wl_resource*r=wl_resource_create(c,&wl_output_interface,(int)version,id);wl_resource_set_implementation(r,NULL,NULL,NULL);wl_output_send_geometry(r,0,0,500,300,0,"Test","Headless",0);wl_output_send_mode(r,3,1920,1080,60000);if(version>=2){wl_output_send_scale(r,1);wl_output_send_done(r);}}
static void pair(struct wl_client*c,struct wl_resource*r,uint32_t a,uint32_t b){(void)c;(void)r;(void)a;(void)b;}
static void u32(struct wl_client*c,struct wl_resource*r,uint32_t n){(void)c;(void)r;(void)n;}
static void i32(struct wl_client*c,struct wl_resource*r,int32_t n){(void)c;(void)r;(void)n;}
static const struct zwlr_layer_surface_v1_interface layer_impl={.set_size=pair,.set_anchor=u32,.set_exclusive_zone=i32,.set_keyboard_interactivity=u32,.ack_configure=u32,.destroy=destroy};
static void make_layer(struct wl_client*c,struct wl_resource*r,uint32_t id,struct wl_resource*s,struct wl_resource*o,uint32_t level,const char*name){(void)r;(void)s;(void)o;assert(level==1&&!strcmp(name,"stillwater-wallpaper"));layer=wl_resource_create(c,&zwlr_layer_surface_v1_interface,1,id);wl_resource_set_implementation(layer,&layer_impl,NULL,NULL);}
static const struct zwlr_layer_shell_v1_interface shell_impl={.get_layer_surface=make_layer,.destroy=destroy};
static void bind_shell(struct wl_client*c,void*d,uint32_t version,uint32_t id){(void)d;struct wl_resource*r=wl_resource_create(c,&zwlr_layer_shell_v1_interface,(int)version,id);wl_resource_set_implementation(r,&shell_impl,NULL,NULL);}
int main(void){display=wl_display_create();assert(display);screen=calloc(1920u*1080,4);assert(screen);assert(wl_display_init_shm(display)==0);assert(wl_global_create(display,&wl_compositor_interface,4,NULL,bind_compositor));assert(wl_global_create(display,&wl_output_interface,2,NULL,bind_output));assert(wl_global_create(display,&zwlr_layer_shell_v1_interface,1,NULL,bind_shell));assert(wl_display_add_socket(display,"stillwater-test")==0);puts("READY");fflush(stdout);wl_display_run(display);wl_display_destroy_clients(display);wl_display_destroy(display);free(screen);}
