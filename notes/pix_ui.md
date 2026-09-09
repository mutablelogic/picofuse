# Pix UI layout

Here we describe the layout system for the Pix UI framework.

## pix_ui_t

The `pix_ui_t` structure represents a main UI context. It's ultimately a canvas into which a root component, and subsequent
child components are added and managed. To create or destroy a component, use `pix_ui_init` and `pix_ui_deinit` respectively.

The main context requires:

* A size and top-left origin (pix_size_t and pix_point_t)
* The co-ordinate system is "arbitary" but the top-left is always (0,0)
* Points are integers (and can be negative) but don't necessarily need to map 1:1 to screen pixels.
* A root component will already be added to the main context, and accessible via the `root` member of the `pix_ui_t` structure.

```c

typedef void (*pix_ui_paint_callback_t)(pix_ui_t *ui, pix_component_t *component);

struct pix_ui_t {
    pix_size_t size;
    pix_point_t origin;
    pix_component_t *root;

    // Dirty flags
    bool needs_layout;
    bool needs_paint;

    // Callback to draw a component
    pix_ui_paint_callback_t paint_callback;
};

void pix_ui_set_callback(pix_ui_t *ui, pix_ui_paint_callback_t callback); 
```

Callbacks are necessary to handle events such as layout changes and painting, and to translate from component-relative coordinates to the main UI
context's coordinate system.

## pix_component_t

The `pix_component_t` structure represents a UI component, which has a parent (unless it's a root component) and one or more child components.
We'd likely have a pool of `pix_component_t` structures instead of allocations:

```c
#define PIX_UI_CAPACITY 80
pix_component_t component_pool[PIX_UI_CAPACITY];

pix_component_t* pix_ui_alloc(pix_ui_t* ui, pix_size_t size, pix_ui_rule_t rules, pix_ui_display_t display,void* userdata);
void pix_ui_free(pix_ui_t* ui, pix_component_t* component);
```

There are several key attributes within a `pix_component_t` structure that define its layout and relationship with other components:

```c
struct pix_component_t {
    pix_ui_t *ui; // Owner
    pix_size_t size; // Requested layout size of the component
    pix_point_t origin; // Requested origin - only used when rules.position is pix_ui_position_absolute
    pix_ui_rule_t rules; // Layout rules for the component
    pix_ui_display_t display; // Display properties for the component
    void* userdata; // User-defined data associated with the component

    // Resolved layout attributes
    pix_size_t resolved_size; // Actual size after layout resolution
    pix_point_t resolved_origin; // Actual origin after layout resolution (absolute, relative to the main UI context's origin)

    // Tree structure 
    pix_component_t *parent;       // NULL for the root component
    pix_component_t *first_child;  // NULL if this component has none
    pix_component_t *next_sibling; // NULL if this is its own parent's last child

    // Dirty flags
    bool needs_layout;
    bool needs_paint;
};
```

The following methods could typically be used to manage child components within a `pix_component_t`:

```c
bool pix_ui_component_add(pix_ui_component_t *parent, pix_ui_component_t *child);             // append a child
bool pix_ui_component_remove(pix_ui_component_t* parent,pix_ui_component_t *child);           // detach from current parent
bool pix_ui_component_remove_all(pix_ui_component_t* parent);                                 // remove all children

bool pix_ui_component_insert_before(pix_ui_component_t* parent,pix_ui_component_t *child, pix_ui_component_t *sibling);
bool pix_ui_component_insert_after(pix_ui_component_t* parent,pix_ui_component_t *child, pix_ui_component_t *sibling);
bool pix_ui_component_move(pix_ui_component_t* parent,pix_ui_component_t *child, pix_ui_component_t *new_parent);
```

If you insert a component, they will mark needs_layout on the old and new parent (or just the parent,
if they are the same). A measurement and layout pass may be required to resolve the new component's size and
position within its parent (see below).

Paint (and hit-test) order is defined purely by sibling order - a later sibling paints on top of an earlier one, and a component's own children paint on top of it. The reordering methods above already change that order; two thin wrappers make the common "bring this to the front" case explicit:

```c
bool pix_ui_component_bring_to_front(pix_component_t *component); // re-insert as its parent's last child
bool pix_ui_component_send_to_back(pix_component_t *component);   // re-insert as its parent's first child
```

## pix_ui_display_t

The rule on displaying the component is defined by the `pix_ui_display_t` structure:

```c
enum pix_ui_display_t {
    pix_ui_display_visible, // The component is visible and participates in the layout flow
    pix_ui_display_hidden, // The component is hidden but still takes up space in the layout
    pix_ui_display_none    // The component is not displayed and does not take up space
};
```

## pix_ui_rule_t

The `pix_ui_rule_t` structure defines the layout rules for a component, such as alignment, margins, and size constraints,
and flow direction within the layout.

Flow within a parent is either vertical or horizontal within the parent, similar to flexbox in CSS.

```c
enum pix_ui_direction_t {
    pix_ui_direction_horizontal,
    pix_ui_direction_vertical
};
```

There are actually two distinct kinds of alignment, the same way CSS flexbox splits `justify-content` from `align-items`/`align-self` - conflating them into one enum doesn't let a component say "centered along the flow, but stretched across it", which is a very common combination (e.g. a vertically-flowing list whose rows are horizontally centered as a group but each stretch to the full row width).

**Cross-axis alignment** - `pix_ui_alignment_t` - is per-component: how *this* component is positioned within the space its parent gives it, perpendicular to the parent's flow direction. `stretch` is listed first (value `0`) so a zero-initialized `pix_ui_rule_t` already behaves sensibly - the common case - without every component needing to set this explicitly:

```c
enum pix_ui_alignment_t {
    pix_ui_alignment_stretch, // Fill the available cross-axis space (default)
    pix_ui_alignment_start,   // Align to the start of the cross axis
    pix_ui_alignment_center,  // Center along the cross axis
    pix_ui_alignment_end      // Align to the end of the cross axis
};
```

**Main-axis distribution** - `pix_ui_justify_t` - is set on a *parent's* own rule, not each child's: how it packs its children along its own flow direction, once their sizes (and any grow/shrink weighting) are resolved. There's no `stretch` option here - growing a child to fill leftover main-axis space is a separate per-child grow weight, not a justification mode. `start` is value `0` for the same zero-init reason as above, matching CSS's own `flex-start` default:

```c
enum pix_ui_justify_t {
    pix_ui_justify_start,         // Pack children at the start (default)
    pix_ui_justify_center,        // Center the group of children
    pix_ui_justify_end,           // Pack children at the end
    pix_ui_justify_space_between  // Spread children with equal gaps, none at the ends
};
```

There's also margin and padding which define the spacing around a component within its parent, and padding inside the component to the layout area. These are both in `pix_point_t` units (which allows for negative values).

A component can also opt out of its parent's flow entirely - useful for something like an overlapping window or popup, positioned explicitly rather than flowed alongside its siblings:

```c
enum pix_ui_position_t {
    pix_ui_position_flow,     // Positioned by the parent's flow layout (default)
    pix_ui_position_absolute  // Positioned explicitly via the component's own `origin`, ignoring the parent's flow
};
```

An `absolute` component takes up no space when its parent distributes flow space among its other children, and its own `alignment`/`justify` don't apply - its `resolved_origin` comes directly from `parent->resolved_origin + origin` instead. It still follows normal sibling z-order (see above) and `pix_ui_hit_test`, so a popup still paints - and is clicked - on top of whatever it overlaps.

Putting the pieces above together - each enum is stored as `uint8_t` rather than its own named type, since none has more than a handful of values and `pix_ui_rule_t` is embedded directly in every `pix_component_t`, not held by pointer, so its own size matters. That packs the three enums into 4 bytes total (3 bytes + 1 byte alignment padding ahead of `margin`, rather than the 12 bytes three default-`int`-sized enums would cost), for **12 bytes** overall:

```c
struct pix_ui_rule_t {
    pix_ui_direction_t direction; // 
    pix_ui_alignment_t  alignment; // cross-axis (this component's own)
    pix_ui_justify_t justify;   // main-axis (only meaningful once this component has children)
    pix_point_t margin;  // Spacing around this component, within its parent, can be negative
    pix_point_t padding; // Spacing inside this component, around its own children, can be negative
};
```

## Layout & Paint passes

Two entry points, normally called together:

```c
// Resolve size/origin for every visible component
void pix_ui_layout(pix_ui_t *ui); 

// Lock the display, invoke the registered paint callback for everything marked needs_paint, then unlock the display
void pix_ui_paint(pix_ui_t *ui,pix_display_t *display);  
```

Two methods used to mark components as needing layout or paint are:

```c
// Marks needs_layout on component, and needs_layout on component->ui
void pix_ui_invalidate_layout(pix_component_t *component); 

 // Marks needs_paint on component, and needs_paint on component->ui - without forcing re-layout
void pix_ui_invalidate_paint(pix_component_t *component); 
```

## Polling

Polling will trigger a layout and paint pass if needed, based on the `needs_layout` and `needs_paint` flags.
This needs to be called regularly, and will invoke the callback to paint any components marked as needing paint.

```c
void pix_ui_poll(pix_ui_t *ui); // pix_ui_layout(ui) then pix_ui_paint(ui), each only if its own flag is set
```

## Hit testing

To route a touch/click to a component, walk the tree front-to-back - the reverse of paint order, since a later sibling (or a child) paints on top of whatever came before it - and return the first match, which is visible:

```c
// Topmost visible component containing an absolute point, or NULL.
pix_component_t *pix_ui_hit_test(pix_ui_t *ui, pix_point_t point);
```

## Multicore considerations

`pix_ui_layout` and `pix_ui_paint` should be guarded so they can't be called concurrently from multiple threads.
As it's assumed `pix_ui_poll` is called from the same thread in the background and serially, that's unlikely to be
an issue, but the guard should also cover any updates to existing components or adding new components to the UI.

Calling the painting pass should ultimately lock the display it is associated with before that occurs, and then
unlocked afterwards, so it will then refresh the display.

## Resource considerations

Per-component size (32-bit): roughly 56-60 bytes. Assuming around 80-100 components in the pool:

32-bit (Pico) 64-bit (host)
80 components ~4.5 KB ~7 KB
100 components ~5.6 KB ~9 KB

No heap allocations required.
