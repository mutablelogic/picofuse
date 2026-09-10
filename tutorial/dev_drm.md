# Linux DRM/KMS Display Backend

Design notes for a second Linux `pix` display backend, alongside `dev/sdl.h`
(see `README.md`) - this one targeting real display hardware directly via
the kernel's DRM/KMS API, rather than an SDL window. The motivating case is
the official Raspberry Pi 7" touch display over DSI: 800×480, XRGB8888,
60 FPS driven by the GPU's own vblank timing - no window manager, no X11,
just a framebuffer the kernel scans out to the panel.

Unlike SPI, there's no per-pixel transfer happening at all here - the GPU
reads directly out of system RAM each refresh, so "sending a frame" is
just handing the kernel a different buffer address to scan out from next.
That's DRM's *dumb buffer* + *page flip* model.

## Shape

Same `pix_display_ops_t` as `dev/sdl.h`: `dev_drm_init()` returns a
`pix_display_t *` via `_pix_display_alloc()`, and `lock()`/`unlock()`/
`poll()`/`deinit()` do the real work behind it. Two dumb buffers (the same
ping-pong idea as the SPI tutorial's frame streaming, just backed by GPU
memory instead of a wire): `lock()` hands back whichever one isn't
currently on screen, `unlock()` requests a page flip to it.

```c
typedef struct {
  uint32_t fb_id;    // drmModeAddFB2() handle - what page-flip actually swaps to
  uint32_t handle;   // Kernel buffer-object handle
  uint32_t pitch;    // Bytes per row - see the note on this below
  uint32_t size;
  uint32_t *pixels;  // mmap()'d userspace pointer
} _dev_drm_buf_t;

typedef struct {
  int fd;
  uint32_t crtc_id;
  uint32_t connector_id;
  uint16_t mode_w, mode_h;
  uint32_t mode_refresh;
  _dev_drm_buf_t buf[2];
  int front; // Index currently on screen; the other is lock()'s to draw into
  volatile bool flip_pending;
} _dev_drm_ctx_t;

static_assert(sizeof(_dev_drm_ctx_t) <= PIX_DISPLAY_CONTEXT_SIZE,
             "_dev_drm_ctx_t too large for pix_display_t's embedded context");
```

That deliberately stores just the three `drmModeModeInfo` fields actually
needed (width, height, refresh) rather than the ~70-byte struct itself -
`PIX_DISPLAY_CONTEXT_SIZE` defaults to 64 bytes, sized for SDL's own
context (see `include/picofuse/pix/display.h`), and embedding the whole
mode struct on top of two buffer records and the rest would blow past
that. If a real implementation still doesn't fit, that's the override to
reach for - not a bigger embedded struct - since it has to match across
every translation unit that touches `pix_display_t`.

## Setup: finding the display and allocating buffers

Opening the DRM node, finding the connected DSI connector and its CRTC,
and allocating a pair of dumb buffers mmap'd into userspace - this part of
the original sketch is basically right, just needs `drmModeAddFB2()`
instead of the legacy `drmModeAddFB()`:

```c
static bool _dev_drm_alloc_buffer(int fd, uint16_t w, uint16_t h,
                                  _dev_drm_buf_t *buf) {
  struct drm_mode_create_dumb create = {.width = w, .height = h, .bpp = 32};
  if (ioctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &create) < 0) {
    return false;
  }
  buf->handle = create.handle;
  buf->pitch = create.pitch;
  buf->size = create.size;

  // drmModeAddFB2() takes an explicit fourcc format rather than the
  // legacy depth/bpp pair - XRGB8888 says plainly that the top byte is
  // padding, not alpha, which the legacy call's "depth 24, bpp 32"
  // leaves you to infer. The primary plane doesn't blend alpha either
  // way, so ARGB8888 wouldn't actually composite against anything -
  // XRGB8888 says what actually happens.
  uint32_t handles[4] = {buf->handle}, pitches[4] = {buf->pitch}, offsets[4] = {0};
  if (drmModeAddFB2(fd, w, h, DRM_FORMAT_XRGB8888, handles, pitches, offsets,
                    &buf->fb_id, 0) != 0) {
    return false;
  }

  struct drm_mode_map_dumb map = {.handle = buf->handle};
  if (ioctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &map) < 0) {
    return false;
  }
  buf->pixels = mmap(NULL, buf->size, PROT_READ | PROT_WRITE, MAP_SHARED,
                     fd, map.offset);
  return buf->pixels != MAP_FAILED;
}
```

Iterate rows using `buf->pitch`, not `w * 4` - the kernel is free to pad
each row for GPU alignment, and a `pix_bitmap_t` pointed at this memory
needs `stride` set to that same `pitch`, not a computed `w * 4`, for
exactly the reason `pix_bitmap_t::stride` exists as its own field rather
than being derived from `size.w`.

## Page flipping: waiting for the kernel, not the clock

This is the part of the original sketch worth fixing rather than just
reformatting: it calls `drmModePageFlip()` every loop iteration and then
`usleep(16666)`, hoping that's close enough to 60 Hz. Two problems with
that - `drmModePageFlip()` returns `-EBUSY` if the *previous* flip hasn't
been acknowledged yet, and even when it succeeds, nothing confirms the
GPU has actually switched buffers before the code goes on to draw into
"the other" one. A fixed sleep can only approximate the display's real
refresh timing, and drifts.

The fix is to actually wait for the kernel's completion event rather than
guessing at its timing - request `DRM_MODE_PAGE_FLIP_EVENT`, and use
`drmHandleEvent()` with a callback to clear the pending flag:

```c
static void _dev_drm_on_flip(int fd, unsigned int frame, unsigned int sec,
                             unsigned int usec, void *userdata) {
  (void)fd, (void)frame, (void)sec, (void)usec;
  _dev_drm_ctx_t *ctx = userdata;
  ctx->flip_pending = false;
}

// ops->unlock() - called once the caller's draw is done with the back buffer.
static void _dev_drm_ops_unlock(pix_display_t *display) {
  _dev_drm_ctx_t *ctx = _pix_display_context(display);
  int back = ctx->front ^ 1;

  ctx->flip_pending = true;
  if (drmModePageFlip(ctx->fd, ctx->crtc_id, ctx->buf[back].fb_id,
                      DRM_MODE_PAGE_FLIP_EVENT, ctx) != 0) {
    ctx->flip_pending = false; // Request itself failed - nothing to wait for
    return;
  }
  ctx->front = back;
}

// ops->poll() - called every pix_poll() round; see pix_display_ops_t's own
// doc on why every allocated display gets this call regardless of whether
// it ends up drawn. Drains any pending flip-completion events without
// blocking, and reports "due" once the front buffer is free to draw into.
static bool _dev_drm_ops_poll(pix_display_t *display) {
  _dev_drm_ctx_t *ctx = _pix_display_context(display);

  struct pollfd pfd = {.fd = ctx->fd, .events = POLLIN};
  if (poll(&pfd, 1, 0) > 0 && (pfd.revents & POLLIN)) {
    drmEventContext ev = {.version = 2, .page_flip_handler = _dev_drm_on_flip};
    drmHandleEvent(ctx->fd, &ev);
  }
  return !ctx->flip_pending;
}
```

`ops->lock()` then just returns a `pix_bitmap_t` over
`ctx->buf[ctx->front ^ 1].pixels` (the buffer *not* currently on screen) -
safe to draw into unconditionally, since `poll()` already established
there's no flip still in flight for it.

## Pixel format

One thing the original sketch elides: `pix_bitmap_t`'s own `PIX_FMT_RGBA32`
stores bytes `R, G, B, A` in memory (see `_pix_bitmap_rgba32_set_pixel()`
in `src/picofuse/pix/rgba32.c`), but `DRM_FORMAT_XRGB8888` on a
little-endian target (the Pi included) lays a 32-bit little-endian word
`0xXXRRGGBB` out in memory as `B, G, R, X` - the reverse order, with the
top byte unused rather than alpha. Pointing a `PIX_FMT_RGBA32` bitmap
straight at this buffer would swap red and blue and misread the alpha
byte as nothing. Either add a pixel format that matches DRM's actual byte
order, or have `lock()`/`unlock()` convert - the latter costs a full-frame
CPU pass every flip, which rather defeats the "GPU reads it directly, zero
transfer cost" point of using DRM in the first place, so a dedicated
format is the better answer here. Needs deciding before this goes further.

## Cleanup and lifetime

The original sketch's `main()` never exits its render loop and never frees
anything - fine for a standalone demo, not for `ops->deinit()`, which
needs the mirror image of setup: `munmap()` each buffer, `drmModeRmFB()`,
`DRM_IOCTL_MODE_DESTROY_DUMB` on each handle, and `drmDropMaster()` before
`close()`. Same teardown-vs-in-flight-work question as the SPI tutorial's
async transfers applies here too - `deinit()` shouldn't tear down a buffer
while a page flip for it is still pending; wait for `flip_pending` to
clear (bounded - it resolves on the next vblank) before freeing anything.
