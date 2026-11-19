# slikui patches to the vendored nanobind

Reapply these when upgrading nanobind. Every patched spot is marked
`[slikui patch]` in the source; `grep -rn "slikui patch" third_party/nanobind`
lists them.

## `nb_inst_forget_refs`: detach wrappers of objects C++ destroyed

**Why.** The C++ widget tree owns built-in widgets; Python holds non-owning
wrappers (`rv_policy::reference`). When the tree destroys a widget
(`Widget::remove`, `clear_children`, `App.close()`), nanobind keeps the wrapper
registered at the dead address. Using it then reads freed memory, and a new
widget allocated at the same address is handed back as the old wrapper, with the
wrong Python type (geetar issue #15). nanobind has no public API for unregistering
an instance.

**What.**

- `src/nb_internals.h`: the unused `nb_inst_state::pad` bit becomes `unmapped`.
  The struct layout does not change.
- `src/nb_type.cpp`:
  - The three places that initialise instance state clear `unmapped` instead of
    `pad`.
  - New `void nb_inst_forget_refs(PyTypeObject *tp, void *ptr) noexcept`. It
    finds every wrapper registered for `ptr` that is a pure reference (not
    internal, no `destruct`, no `cpp_delete`). Each one is unlinked from
    `inst_c2p`, marked uninitialised, and flagged `unmapped`. `tp` is any bound
    type, used only to reach the internals. The caller must hold the GIL.
  - `inst_dealloc` skips the `inst_c2p` unmapping, and its "unknown instance"
    check, for instances flagged `unmapped`.

The function is not a backend ABI slot. slikui builds nanobind with `NB_STATIC`,
so the bindings call it directly and declare it themselves
(`python/src/slikui_py.cpp`). The bindings install
`slikui::Widget::destroy_hook`, which calls it from every `~Widget`.

**Effect.** Using a dead wrapper raises `TypeError` after a `RuntimeWarning`
("attempted to access an uninitialized instance"). A new object at the same
address gets a fresh wrapper of its real type.
