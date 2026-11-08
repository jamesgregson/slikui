# Widget index

Every widget class, grouped by reference page. All are exported from
`slikui/widgets/all.hpp` in C++ and the `slikui` module in Python.

## Controls — [widgets-controls.md](widgets-controls.md)

| Widget | Purpose |
|--------|---------|
| `Button` | four variants: Default, Primary, Danger, Ghost |
| `Checkbox`, `Radio`, `Switch` | bool controls with `on_change` |
| `Slider` | float range, built-in label + live value row |
| `Segmented` | compact radio-style row |
| `ProgressBar` | clamped 0–1 fraction |
| `Spinner` | animated forever, driven by `tick` |
| `Label` | static text, size/color/wrap |

## Text inputs — [widgets-inputs.md](widgets-inputs.md)

| Widget | Purpose |
|--------|---------|
| `TextField` | single-line editing, selection, clipboard |
| `TextArea` | multiline editing |
| `ScrollArea` | scrolling viewport for one child |
| `SearchBox` | TextField + magnifier + clear button |
| `GroupBox` | titled panel |

## Editors — [widgets-editors.md](widgets-editors.md)

| Widget | Purpose |
|--------|---------|
| `DragValue` | drag-to-scrub number field (Blender-style) |
| `Vec2Edit`, `Vec3Edit`, `Vec4Edit` | per-axis editors over a borrowed `float[N]` / numpy array |
| `ColorEdit` | swatch + SV/hue popup |
| `Vec2Pad` | 2D pad with draggable dot |

## Data display — [widgets-data.md](widgets-data.md)

| Widget | Purpose |
|--------|---------|
| `ListView` | selectable item list |
| `Tabs` | tab bar |
| `Dropdown` | click-to-open selection popup |
| `Plot` | lines/bars/fill + markers, autoscale |
| `TreeView` | flat `(label, depth, expandable)` rows |
| `Table` | columns + header + selectable rows |
| `PropertyGrid` | inspector with fixed label column + sections |
| `Image` | PNG/JPEG/pixels, `Stretch`/`Contain`/`Cover` |

## Containers — [widgets-containers.md](widgets-containers.md)

| Widget | Purpose |
|--------|---------|
| `Column`, `Row` | main-axis stacks |
| `Panel` | bordered, rounded container |
| `Spacer` | soaks up remaining main-axis space |
| `Divider` | rule between siblings |
| `Expander` | titled collapsible section |
| `Accordion` | stack of expanders, exclusive by default |
| `Splitter` | two panes + draggable divider |

## Shells & dialogs — [widgets-shells.md](widgets-shells.md)

| Widget | Purpose |
|--------|---------|
| `Dialog`, `MessageBox` | floating shells, optional modal |
| `Toast` | auto-dismissing corner notice |
| `StatusBar` | status text + right-side widgets |
| `LogView` | severity-colored autoscrolling console |
| `FileBrowser` | real-directory Open/Save |
| `ImageView` | pan/zoom bitmap over a checkerboard |
| `CompareView` | two images split by a draggable divider |

## Menus & toolbars — [widgets-menus.md](widgets-menus.md)

| Widget | Purpose |
|--------|---------|
| `MenuBar`, `Menu`, `MenuItem` | menus, checkables, submenus, shortcut hints |
| `Toolbar`, `IconButton` | icon strips with a procedural icon set (`slikui::Icon`) |

## Base class

| Class | Purpose |
|-------|---------|
| `Widget` | tree, protocol (`measure`/`arrange`/`paint`/`on_event`/`tick`), state flags |
| `Container` | padding/gap/cross-axis base for layouts |
| `PaintEnv` | draw list + theme + font handed to `paint` |
