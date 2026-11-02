#define _GNU_SOURCE
#include "art.h"
#include "layer-shell.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>

#define MAX_OUTPUTS 16
static struct wl_display *display;
static struct wl_compositor *compositor;
static struct wl_shm *shm;
static struct zwlr_layer_shell_v1 *layer_shell;
static int scene = 0, fps = 12, theme = 0;
static double speed = 1;
static int wake_pipe[2] = {-1, -1};
static volatile sig_atomic_t running = 1, paused = 0, advance = 0;
static double now(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec + ts.tv_nsec / 1e9;
}
static void signal_handler(int n) {
  if (n == SIGUSR1)
    paused = !paused;
  else if (n == SIGUSR2)
    advance = 1;
  else
    running = 0;
  int saved = errno;
  if (wake_pipe[1] >= 0) {
    char byte = 0;
    ssize_t ignored = write(wake_pipe[1], &byte, 1);
    (void)ignored;
  }
  errno = saved;
}
typedef struct {
  struct wl_buffer *wl;
  void *data;
  size_t size;
  Cell *previous;
  bool busy;
} Buffer;
typedef struct {
  uint32_t id;
  struct wl_output *output;
  struct wl_surface *surface;
  struct zwlr_layer_surface_v1 *layer;
  struct wl_callback *frame;
  int width, height, scale;
  bool ready, configured, removed;
  Art art;
  Cell *presented;
  Buffer buffers[2];
} Output;
static Output outputs[MAX_OUTPUTS];
static void buffer_release(void *data, struct wl_buffer *buffer) {
  (void)buffer;
  ((Buffer *)data)->busy = false;
}
static const struct wl_buffer_listener buffer_listener = {buffer_release};
static void buffer_free(Buffer *b) {
  if (b->wl)
    wl_buffer_destroy(b->wl);
  if (b->data)
    munmap(b->data, b->size);
  free(b->previous);
  memset(b, 0, sizeof(*b));
}
static int buffer_init(Buffer *b, Output *o) {
  int pw = o->width * o->scale, ph = o->height * o->scale;
  int stride = cairo_format_stride_for_width(CAIRO_FORMAT_RGB24, pw);
  if (stride < 0 || ph < 1)
    return -1;
  b->size = (size_t)stride * ph;
  int fd = memfd_create("stillwater", MFD_CLOEXEC);
  if (fd < 0)
    return -1;
  if (ftruncate(fd, (off_t)b->size) < 0) {
    close(fd);
    return -1;
  }
  b->data = mmap(NULL, b->size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (b->data == MAP_FAILED) {
    b->data = NULL;
    close(fd);
    return -1;
  }
  struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, (int)b->size);
  b->wl = wl_shm_pool_create_buffer(pool, 0, pw, ph, stride,
                                    WL_SHM_FORMAT_XRGB8888);
  wl_shm_pool_destroy(pool);
  close(fd);
  wl_buffer_add_listener(b->wl, &buffer_listener, b);
  b->previous = calloc((size_t)o->art.cols * o->art.rows, sizeof(Cell));
  if (!b->previous)
    return -1;
  uint32_t bg = 0xff000000u | art_bg(&o->art);
  for (size_t i = 0; i < b->size / 4; i++)
    ((uint32_t *)b->data)[i] = bg;
  return 0;
}
static void frame_done(void *data, struct wl_callback *cb, uint32_t ms) {
  (void)ms;
  Output *o = data;
  wl_callback_destroy(cb);
  o->frame = NULL;
  o->ready = true;
}
static const struct wl_callback_listener frame_listener = {frame_done};
static void output_reset(Output *o) {
  if (o->frame) {
    wl_callback_destroy(o->frame);
    o->frame = NULL;
  }
  for (int i = 0; i < 2; i++)
    buffer_free(&o->buffers[i]);
  art_free(&o->art);
  free(o->presented);
  o->presented = NULL;
  if (art_init(&o->art, o->width, o->height, scene) < 0) {
    running = 0;
    return;
  }
  art_set_theme(&o->art, theme);
  size_t bytes = (size_t)o->art.cols * o->art.rows * sizeof(Cell);
  o->presented = malloc(bytes);
  if (!o->presented) {
    running = 0;
    return;
  }
  memset(o->presented, 0xff, bytes);
  o->ready = true;
}
static void configure(void *data, struct zwlr_layer_surface_v1 *layer,
                      uint32_t serial, uint32_t w, uint32_t h) {
  Output *o = data;
  zwlr_layer_surface_v1_ack_configure(layer, serial);
  if (!w || !h)
    return;
  if (w > 16384 || h > 16384) {
    fprintf(stderr, "Unsupported output size\n");
    running = 0;
    return;
  }
  if (!o->configured || o->width != (int)w || o->height != (int)h) {
    o->width = (int)w;
    o->height = (int)h;
    output_reset(o);
  }
  o->configured = true;
}
static void closed(void *data, struct zwlr_layer_surface_v1 *layer) {
  (void)layer;
  ((Output *)data)->removed = true;
}
static const struct zwlr_layer_surface_v1_listener layer_listener = {configure,
                                                                     closed};
static void output_geometry(void *d, struct wl_output *o, int32_t x, int32_t y,
                            int32_t pw, int32_t ph, int32_t sub,
                            const char *make, const char *model,
                            int32_t transform) {
  (void)d;
  (void)o;
  (void)x;
  (void)y;
  (void)pw;
  (void)ph;
  (void)sub;
  (void)make;
  (void)model;
  (void)transform;
}
static void output_mode(void *d, struct wl_output *o, uint32_t flags, int32_t w,
                        int32_t h, int32_t refresh) {
  (void)d;
  (void)o;
  (void)flags;
  (void)w;
  (void)h;
  (void)refresh;
}
static void output_done(void *d, struct wl_output *o) {
  (void)d;
  (void)o;
}
static void output_scale(void *d, struct wl_output *output, int32_t scale) {
  (void)output;
  Output *o = d;
  if (scale > 0 && scale <= 8 && o->scale != scale) {
    o->scale = scale;
    if (o->configured)
      output_reset(o);
  }
}
static const struct wl_output_listener output_listener = {
    .geometry = output_geometry,
    .mode = output_mode,
    .done = output_done,
    .scale = output_scale};
static void make_surface(Output *o) {
  if (o->surface || o->removed || !compositor || !layer_shell || !shm)
    return;
  o->surface = wl_compositor_create_surface(compositor);
  struct wl_region *empty = wl_compositor_create_region(compositor);
  wl_surface_set_input_region(o->surface, empty);
  wl_region_destroy(empty);
  o->layer = zwlr_layer_shell_v1_get_layer_surface(
      layer_shell, o->surface, o->output, ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM,
      "stillwater-wallpaper");
  zwlr_layer_surface_v1_set_anchor(o->layer, 15);
  zwlr_layer_surface_v1_set_exclusive_zone(o->layer, -1);
  zwlr_layer_surface_v1_set_keyboard_interactivity(o->layer, 0);
  zwlr_layer_surface_v1_set_size(o->layer, 0, 0);
  zwlr_layer_surface_v1_add_listener(o->layer, &layer_listener, o);
  wl_surface_commit(o->surface);
}
static void output_free(Output *o) {
  if (o->frame)
    wl_callback_destroy(o->frame);
  for (int i = 0; i < 2; i++)
    buffer_free(&o->buffers[i]);
  art_free(&o->art);
  free(o->presented);
  if (o->layer)
    zwlr_layer_surface_v1_destroy(o->layer);
  if (o->surface)
    wl_surface_destroy(o->surface);
  if (o->output)
    wl_output_destroy(o->output);
  memset(o, 0, sizeof(*o));
}
static void global_add(void *d, struct wl_registry *r, uint32_t id,
                       const char *interface, uint32_t version) {
  (void)d;
  if (!strcmp(interface, "wl_compositor"))
    compositor = wl_registry_bind(r, id, &wl_compositor_interface,
                                  version < 4 ? version : 4);
  else if (!strcmp(interface, "wl_shm"))
    shm = wl_registry_bind(r, id, &wl_shm_interface, 1);
  else if (!strcmp(interface, "zwlr_layer_shell_v1"))
    layer_shell = wl_registry_bind(r, id, &zwlr_layer_shell_v1_interface, 1);
  else if (!strcmp(interface, "wl_output")) {
    for (int i = 0; i < MAX_OUTPUTS; i++)
      if (!outputs[i].output) {
        Output *o = &outputs[i];
        o->id = id;
        o->scale = 1;
        o->output = wl_registry_bind(r, id, &wl_output_interface,
                                     version < 2 ? version : 2);
        wl_output_add_listener(o->output, &output_listener, o);
        make_surface(o);
        break;
      }
  }
}
static void global_remove(void *d, struct wl_registry *r, uint32_t id) {
  (void)d;
  (void)r;
  for (int i = 0; i < MAX_OUTPUTS; i++)
    if (outputs[i].id == id)
      outputs[i].removed = true;
}
static const struct wl_registry_listener registry_listener = {global_add,
                                                              global_remove};
static void damage_surface(void *data, int x, int y, int w, int h) {
  Output *o = data;
  wl_surface_damage(o->surface, x, y, w, h);
}
static int render(Output *o, double t) {
  if (!o->configured || !o->ready || o->removed)
    return 0;
  Buffer *b = NULL;
  for (int i = 0; i < 2; i++)
    if (!o->buffers[i].busy) {
      b = &o->buffers[i];
      break;
    }
  if (!b)
    return 0;
  if (!b->wl && buffer_init(b, o) < 0) {
    fprintf(stderr, "Buffer allocation failed\n");
    return -1;
  }
  art_frame(&o->art, t);
  if (!art_damage(&o->art, o->presented, damage_surface, o))
    return 0;
  if (art_blit(&o->art, b->data, o->width * o->scale * 4, o->scale,
               b->previous) < 0) {
    fprintf(stderr, "Character tile allocation failed\n");
    return -1;
  }
  wl_surface_set_buffer_scale(o->surface, o->scale);
  wl_surface_attach(o->surface, b->wl, 0, 0);
  o->frame = wl_surface_frame(o->surface);
  wl_callback_add_listener(o->frame, &frame_listener, o);
  o->ready = false;
  b->busy = true;
  wl_surface_commit(o->surface);
  return 0;
}
static int theme_id(const char *name) {
  const char *names[] = {"midnight", "paper", "dawn",
                         "mist", "ember", "moonlight",
                         "sage", "sandstone", "ocean", "plum"};
  for (int i = 0; i < ART_THEMES; i++)
    if (!strcmp(name, names[i]))
      return i;
  return -1;
}
static int scene_id(const char *name) {
  if (!strcmp(name, "pond"))
    return 0;
  if (!strcmp(name, "sky"))
    return 1;
  if (!strcmp(name, "mountains"))
    return 2;
  if (!strcmp(name, "forest"))
    return 3;
  if (!strcmp(name, "rain"))
    return 4;
  if (!strcmp(name, "dunes"))
    return 5;
  return -1;
}
int main(int argc, char **argv) {
  const char *png = NULL;
  int width = 1920, height = 1080, bench = 0;
  double t = 0;
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--scene") && i + 1 < argc)
      scene = scene_id(argv[++i]);
    else if (!strcmp(argv[i], "--theme") && i + 1 < argc)
      theme = theme_id(argv[++i]);
    else if (!strcmp(argv[i], "--speed") && i + 1 < argc)
      speed = strtod(argv[++i], NULL);
    else if (!strcmp(argv[i], "--fps") && i + 1 < argc)
      fps = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--render") && i + 1 < argc)
      png = argv[++i];
    else if (!strcmp(argv[i], "--benchmark"))
      bench = 1;
    else if (!strcmp(argv[i], "--width") && i + 1 < argc)
      width = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--height") && i + 1 < argc)
      height = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--time") && i + 1 < argc)
      t = strtod(argv[++i], NULL);
    else {
      fprintf(stderr,
              "Usage: stillwater-native [--scene "
              "pond|sky|mountains|forest|rain|dunes] [--theme "
              "midnight|paper|dawn|mist|ember|moonlight|sage|sandstone|ocean|plum] [--speed 0.25..2] "
              "[--fps 1..30] [--render file.png | --benchmark] "
              "[--width N --height N --time seconds]\n");
      return 2;
    }
  }
  if (scene < 0 || theme < 0 || !isfinite(speed) || speed < .25 || speed > 2 ||
      fps < 1 || fps > 30 || width < 1 || width > 16384 || height < 1 ||
      height > 16384 || !isfinite(t))
    return 2;
  if (png || bench) {
    Art art;
    if (art_init(&art, width, height, scene) < 0)
      return 1;
    cairo_surface_t *image =
        cairo_image_surface_create(CAIRO_FORMAT_RGB24, width, height);
    cairo_t *cr = cairo_create(image);
    art_set_theme(&art, theme);
    uint32_t bg = art_bg(&art);
    cairo_set_source_rgb(cr, ((bg >> 16) & 255) / 255.,
                         ((bg >> 8) & 255) / 255., (bg & 255) / 255.);
    cairo_paint(cr);
    Cell *prev = calloc((size_t)art.cols * art.rows, sizeof(Cell));
    if (!prev)
      return 1;
    double start = now();
    int n = bench ? 240 : 1;
    for (int i = 0; i < n; i++) {
      art_frame(&art, t + i * speed / (double)fps);
      cairo_surface_flush(image);
      if (art_blit(&art, cairo_image_surface_get_data(image),
                   cairo_image_surface_get_stride(image), 1, prev) < 0)
        return 1;
      cairo_surface_mark_dirty(image);
    }
    double elapsed = now() - start;
    int result = 0;
    if (png)
      result = cairo_surface_write_to_png(image, png) != CAIRO_STATUS_SUCCESS;
    else {
      struct rusage ru;
      getrusage(RUSAGE_SELF, &ru);
      printf("scene=%d render_ms=%.3f estimated_core_percent_at_%dfps=%.2f "
             "max_rss_mb=%.2f frames=%d\n",
             scene, elapsed / n * 1000, fps, elapsed / n * fps * 100,
             ru.ru_maxrss / 1024., n);
    }
    free(prev);
    cairo_destroy(cr);
    cairo_surface_destroy(image);
    art_free(&art);
    return result;
  }
  if (pipe2(wake_pipe, O_NONBLOCK | O_CLOEXEC) < 0)
    return 1;
  struct sigaction sa = {.sa_handler = signal_handler};
  sigemptyset(&sa.sa_mask);
  sigaction(SIGINT, &sa, NULL);
  sigaction(SIGTERM, &sa, NULL);
  sigaction(SIGUSR1, &sa, NULL);
  sigaction(SIGUSR2, &sa, NULL);
  display = wl_display_connect(NULL);
  if (!display) {
    fprintf(stderr, "No Wayland desktop. Run inside your Omarchy session.\n");
    return 1;
  }
  struct wl_registry *registry = wl_display_get_registry(display);
  wl_registry_add_listener(registry, &registry_listener, NULL);
  if (wl_display_roundtrip(display) < 0 || !compositor || !shm ||
      !layer_shell) {
    fprintf(stderr, "Wayland compositor lacks layer-shell support\n");
    return 1;
  }
  for (int i = 0; i < MAX_OUTPUTS; i++)
    if (outputs[i].output)
      make_surface(&outputs[i]);
  wl_display_roundtrip(display);
  double previous = now(), next = previous, clock = t;
  int status = 0;
  while (running) {
    if (advance) {
      advance = 0;
      scene = (scene + 1) % ART_SCENES;
      for (int i = 0; i < MAX_OUTPUTS; i++)
        if (outputs[i].configured)
          output_reset(&outputs[i]);
    }
    double current = now();
    if (!paused)
      clock += fmin(current - previous, .25) * speed;
    previous = current;
    bool ready = false;
    for (int i = 0; i < MAX_OUTPUTS; i++) {
      Output *o = &outputs[i];
      if (o->removed) {
        output_free(o);
        continue;
      }
      if (o->configured && o->ready)
        ready = true;
    }
    if (ready && !paused && current >= next) {
      for (int i = 0; i < MAX_OUTPUTS; i++)
        if (render(&outputs[i], clock) < 0)
          running = 0;
      next = current + 1. / fps;
    }
    if (!running)
      break;
    while (wl_display_prepare_read(display) != 0)
      if (wl_display_dispatch_pending(display) < 0) {
        running = 0;
        break;
      }
    if (!running)
      break;
    int flush = wl_display_flush(display);
    if (flush < 0 && errno != EAGAIN) {
      wl_display_cancel_read(display);
      status = 1;
      break;
    }
    int timeout =
        paused || !ready ? -1 : (int)ceil(fmax(0, next - now()) * 1000);
    struct pollfd fds[2] = {
        {wl_display_get_fd(display), POLLIN | (flush < 0 ? POLLOUT : 0), 0},
        {wake_pipe[0], POLLIN, 0}};
    int result = poll(fds, 2, timeout);
    if (fds[1].revents & POLLIN) {
      char bytes[64];
      while (read(wake_pipe[0], bytes, sizeof(bytes)) > 0) {
      }
    }
    if (result > 0 && (fds[0].revents & POLLIN)) {
      if (wl_display_read_events(display) < 0) {
        status = 1;
        break;
      }
    } else
      wl_display_cancel_read(display);
    if (result > 0 && (fds[0].revents & (POLLERR | POLLHUP))) {
      status = 1;
      break;
    }
    if (result < 0 && errno != EINTR) {
      status = 1;
      break;
    }
    if (wl_display_dispatch_pending(display) < 0) {
      status = 1;
      break;
    }
  }
  for (int i = 0; i < MAX_OUTPUTS; i++) {
    output_free(&outputs[i]);
  }
  wl_registry_destroy(registry);
  wl_display_disconnect(display);
  close(wake_pipe[0]);
  close(wake_pipe[1]);
  return status;
}
