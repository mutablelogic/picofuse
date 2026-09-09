# Pix Displays

The "pix" module will be designed to be platform-independent and event-driven, like the rest of the system.
Generally "pix" will encompass displays, bitmaps, fonts, display components and layout, in a highly resource-constrained environment.
It's not yet known if this will even be feasible, but let's see!

## Displays

Displays will be created by third party libraries or drivers ("dev") on the whole, and be bitmap-based (but with several pixel formats).
For example, an SDL backend. It will provide a few methods to initialize, update, and manage the display:

```c
pix_display_t* display = dev_sdl_create_display(const char* title, pix_size_t size, pix_format_t format, dev_sdl_window_flags_t flags);
```

Any pix_display_t has a number of trampoline functions that the backend must implement, such as
locking, unlocking, and updating the display:

```c
struct pix_display_ops_t {
    pix_bitmap_t* (*lock)(pix_display_t* display);
    void (*unlock)(pix_display_t* display);
    void (*poll)(pix_display_t* display); 
    void (*deinit)(pix_display_t* display);

    // Mark the rectangle which is dirty (needs to be updated on the display)
    pix_point_t dirty_origin;
    pix_size_t dirty_size;

    // Timestamp of the last update to the display, in ms.
    uint64_t ts; 
}
```

Locking a display will return a pointer to the underlying bitmap operations, which can then be modified directly.
After making changes, the display must be unlocked to apply the updates. For SDL specifically, there is also
the ability to create an "off screen" bitmap for rendering, but later on in the development process.

## Bitmaps

A bitmap represents a two-dimensional array of pixels, which can be manipulated directly through the pix module. The
actual pixel data is stored in a format defined by the pix_format_t, and operations on the bitmap are performed through
trampoline operations, which may be (for example) GPU accelerated.

```c
struct pix_bitmap_t {
    pix_display_t* display; // NULL if no attached display
    pix_size_t size; // Dimensions of the bitmap
    pix_format_t format; // Format of the pixel data
    void* data; // Pointer to the raw pixel data
    pix_bitmap_ops_t* ops; // Operations for manipulating this bitmap
};

// How a new color combines with what's already in the bitmap.
typedef enum {
    PIX_OP_SET, // Overwrite the destination pixel with the source color (default)
} pix_op_t;

struct pix_bitmap_ops_t {
    bool (*set_op)(pix_bitmap_t* bitmap, pix_op_t op); // Compositing op applied by all draws below, until changed
    void (*set_pixel)(pix_bitmap_t* bitmap, pix_point_t point, pix_color_t color);
    void (*fill_rect)(pix_bitmap_t* bitmap, pix_point_t origin, pix_size_t size, pix_color_t color);
    void (*fill_geom)(pix_bitmap_t* bitmap, pix_path_t* path, pix_color_t color);
    void (*draw_rect)(pix_bitmap_t* bitmap, pix_point_t origin, pix_size_t size, pix_color_t color);
    void (*draw_geom)(pix_bitmap_t* bitmap, pix_path_t* path, pix_color_t color);
    void (*draw_line)(pix_bitmap_t* bitmap, pix_point_t a, pix_point_t b, pix_color_t color);
    void (*blit)(pix_bitmap_t* bitmap, pix_point_t origin, pix_bitmap_t* src, pix_point_t src_origin, pix_size_t size);
}
```

## Polling

Assuming there's only one display at a time, polling it for events and updates is straightforward. This would typically be done as part of the main application loop, and perform the following:

* If the display does not have a dirty region, no update is necessary.
* If the expected framerate has not been reached, no update is necessary.
* If the display has a poll method, call that.
* If the display should be updated:
  * Lock the display to obtain the underlying bitmap.
  * Call the display's draw callback with the pixmap (underlying bitmap).
  * Unlock the display to apply the updates.
  * Update the timestamp for the display.
  * If the framerate is under the expected framerate, report that.

```c
pix_poll(pix_display_t* display); // Poll the display for events and updates
```

## Interaction with the layout

TODO
