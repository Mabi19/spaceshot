#include "toplevel.h"
#include "log.h"
#include "picker/context.h"
#include "render/command.h"
#include "wayland/globals.h"
#include "wayland/overlay-surface.h"
#include "wayland/screen-capture.h"
#include "wayland/seat.h"
#include <assert.h>
#include <math.h>
#include <string.h>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

// TODO: make configurable?
constexpr double DEFAULT_COLUMN_WIDTH = 480;
constexpr double PADDING = 32;
constexpr double IMAGE_LABEL_PADDING = 8;
constexpr double HIGHLIGHT_PADDING = 16;
// TODO: somehow unify with the output picker?
constexpr double LABEL_PADDING_X = 6;
constexpr double LABEL_PADDING_Y = 4;

static void clamp_scroll(PickerSurface *picker) {
    ToplevelPicker *toplevel = &picker->toplevel;
    double max_scroll =
        fmax(toplevel->content_height - picker->surface->device_height, 0);
    toplevel->scroll_y = fmin(fmax(toplevel->scroll_y, 0), max_scroll);
}

static void recompute_grid(PickerSurface *picker) {
    ToplevelPicker *toplevel = &picker->toplevel;

    int columns;
    double scale = picker->surface->scale / 120.0;
    double column_width = round(DEFAULT_COLUMN_WIDTH * scale);
    double padding = round(PADDING * scale);
    double avail_width =
        fmax(picker->surface->device_width - 2.0 * padding, 1.0);
    if (column_width > avail_width) {
        column_width = avail_width;
        columns = 1;
    } else {
        columns = 1;
        avail_width -= column_width;
        // If there is more than one column, there will also be padding between
        // the columns.
        columns += floor(avail_width / (column_width + padding));
    }

    // The top-left X positions of each column.
    double *column_x = malloc(columns * sizeof(double));
    // The current Y positions of each column.
    double *column_y = malloc(columns * sizeof(double));

    double used_width = columns * column_width + (columns - 1) * padding;
    double left_space =
        round((picker->surface->device_width - used_width) / 2.0);
    for (int i = 0; i < columns; i++) {
        column_x[i] = left_space + i * (column_width + padding);
        column_y[i] = padding;
    }

    double label_padding_x = round(LABEL_PADDING_X * scale);
    double label_padding_y = round(LABEL_PADDING_Y * scale);
    double image_label_padding = round(IMAGE_LABEL_PADDING * scale);
    toplevel->label_text_style =
        RENDER_TEXT_STYLE_DEFAULT(picker->surface->scale);
    toplevel->label_text_style.max_width = column_width - 2.0 * label_padding_x;
    for (int i = 0; i < toplevel->entry_count; i++) {
        ToplevelPickerEntry *entry = &toplevel->entries[i];
        if (!entry->visible) {
            continue;
        }

        // Determine the least-used column.
        int this_column = 0;
        double this_column_y = INFINITY;
        for (int c = 0; c < columns; c++) {
            if (column_y[c] < this_column_y) {
                this_column_y = column_y[c];
                this_column = c;
            }
        }

        // image
        double frame_width = fmax((double)entry->frame->width, 1.0);
        double frame_height = fmax((double)entry->frame->height, 1.0);
        double image_height =
            round((column_width / frame_width) * frame_height);
        BBox image_bounds = (BBox){
            column_x[this_column],
            column_y[this_column],
            column_width,
            image_height
        };
        entry->image_bounds = image_bounds;
        entry->column = this_column;
        // label
        RenderTextMetrics label_size = picker->surface->renderer->measure_text(
            entry->title, -1, toplevel->label_text_style
        );
        entry->label_text_x = column_x[this_column] +
                              round((column_width - label_size.width) / 2.0);
        entry->label_text_y = column_y[this_column] + image_height +
                              image_label_padding + label_padding_y;
        entry->label_background_bounds = (BBox){
            entry->label_text_x - label_padding_x,
            entry->label_text_y - label_padding_y,
            label_size.width + 2.0 * label_padding_x,
            label_size.height + 2.0 * label_padding_y,
        };

        double total_height = image_height + image_label_padding +
                              entry->label_background_bounds.height;
        double highlight_padding = round(HIGHLIGHT_PADDING * scale);
        entry->full_bounds = (BBox){
            column_x[this_column] - highlight_padding,
            column_y[this_column] - highlight_padding,
            column_width + 2 * highlight_padding,
            total_height + 2 * highlight_padding,
        };
        column_y[this_column] += total_height + padding;
    }

    // The tallest column determines the content height
    // (column_y already includes the trailing padding).
    double content_height = 0;
    for (int c = 0; c < columns; c++) {
        content_height = fmax(content_height, column_y[c]);
    }
    toplevel->content_height = content_height;
    clamp_scroll(picker);

    free(column_x);
    free(column_y);
}

static double entry_target_y(const ToplevelPickerEntry *entry) {
    return entry->full_bounds.y + entry->full_bounds.height / 2.0;
}

/**
 * The entry the selection highlight is on.
 * The keyboard focus takes priority over the mouse focus.
 * -1 means nothing is highlighted.
 */
static int focused_entry(const ToplevelPicker *toplevel) {
    return toplevel->keyboard_focus_idx != -1 ? toplevel->keyboard_focus_idx
                                              : toplevel->mouse_focus_idx;
}

static int first_visible_entry(ToplevelPicker *toplevel) {
    for (int i = 0; i < toplevel->entry_count; i++) {
        if (toplevel->entries[i].visible) {
            return i;
        }
    }
    return -1;
}

/**
 * Recompute which entry the mouse cursor is over and update the highlight.
 *
 * The mouse focus is kept separate from the keyboard focus, which takes
 * priority. It is only given up when the pointer itself moves onto a new
 * entry; the view scrolling underneath a stationary cursor does not count.
 *
 * Returns whether the highlight changed (and a redraw is needed).
 */
static bool update_mouse_hover(PickerSurface *picker, bool pointer_moved) {
    ToplevelPicker *toplevel = &picker->toplevel;
    SeatDispatcher *seat = wayland_globals.seat_dispatcher;
    double scale = picker->surface->scale / 120.0;
    int hover_idx = -1;
    if (seat->pointer_data.focus == picker->surface->wl_surface) {
        double x = seat->pointer_data.surface_x * scale;
        // Undo scrolling
        double y = seat->pointer_data.surface_y * scale + toplevel->scroll_y;
        for (int i = 0; i < toplevel->entry_count; i++) {
            ToplevelPickerEntry *entry = &toplevel->entries[i];
            if (!entry->visible) {
                continue;
            }
            if (entry->full_bounds.x <= x &&
                x <= entry->full_bounds.x + entry->full_bounds.width &&
                entry->full_bounds.y <= y &&
                y <= entry->full_bounds.y + entry->full_bounds.height) {
                hover_idx = i;
                break;
            }
        }
    }

    if (hover_idx != toplevel->mouse_focus_idx) {
        toplevel->mouse_focus_idx = hover_idx;
        if (toplevel->keyboard_focus_idx != -1) {
            if (pointer_moved && hover_idx != -1) {
                // The mouse takes over the highlight.
                toplevel->keyboard_focus_idx = -1;
                toplevel->target_y =
                    entry_target_y(&toplevel->entries[hover_idx]);
                return true;
            }
            // The keyboard focus remains where it is
            return false;
        } else {
            if (hover_idx != -1) {
                toplevel->target_y =
                    entry_target_y(&toplevel->entries[hover_idx]);
            }
            return true;
        }
    }
    return false;
}

static void confirm_selection(PickerSurface *picker) {
    ToplevelPicker *toplevel = &picker->toplevel;
    int idx = focused_entry(toplevel);
    assert(idx != -1);
    ToplevelPickerEntry *entry = &toplevel->entries[idx];
    Image *img = capture_frame_steal_image(entry->frame);
    picker_context_finish(picker, PICKER_FINISH_REASON_SELECTED, img);
}

static void
toplevel_picker_handle_mouse(PickerSurface *picker, MouseEvent event) {
    ToplevelPicker *toplevel = &picker->toplevel;

    bool scrolled = (event.scroll_x != 0 || event.scroll_y != 0) &&
                    event.focus == picker->surface->wl_surface;
    if (scrolled) {
        toplevel->scroll_y += event.scroll_y;
        clamp_scroll(picker);
    }

    // Scrolling is not the mouse moving; it must not take over
    // keyboard focus.
    bool redraw = update_mouse_hover(picker, !scrolled);

    if (event.buttons_pressed & POINTER_BUTTON_LEFT) {
        // A click always resolves through the mouse cursor's position.
        if (toplevel->mouse_focus_idx != -1) {
            toplevel->keyboard_focus_idx = -1;
            confirm_selection(picker);
            return;
        } else if (toplevel->keyboard_focus_idx != -1) {
            toplevel->keyboard_focus_idx = -1;
            redraw = true;
        } else {
            picker_context_finish(picker, PICKER_FINISH_REASON_CANCELLED, NULL);
            return;
        }
    }

    if (redraw || scrolled) {
        overlay_surface_queue_draw(picker->surface);
    }
}

static void
toplevel_picker_handle_keyboard(PickerSurface *picker, KeyboardEvent event) {
    ToplevelPicker *toplevel = &picker->toplevel;

    char name[64];
    xkb_keysym_get_name(event.keysym, name, 64);
    log_debug("keysym: %x (%s)\n", event.keysym, name);

    bool update_target_y = false;
    int start_idx = focused_entry(toplevel);
    int new_idx = start_idx;
    if (event.type == KEYBOARD_EVENT_PRESS) {
        switch (event.keysym) {
        case XKB_KEY_Return:
            if (start_idx != -1) {
                confirm_selection(picker);
            }
            break;
        case XKB_KEY_Tab:
        case XKB_KEY_ISO_Left_Tab: {
            // Advance to the next (or previous if Shift) visible entry.
            if (toplevel->entry_count == 0) {
                break;
            }
            bool shift = xkb_state_mod_name_is_active(
                event.state, XKB_MOD_NAME_SHIFT, XKB_STATE_EFFECTIVE
            );
            int delta = shift ? -1 : 1;
            int test_idx = start_idx;
            if (start_idx == -1) {
                test_idx = shift ? 0 : -1;
            }
            for (int i = 0; i < toplevel->entry_count; i++) {
                test_idx = (test_idx + delta + toplevel->entry_count) %
                           toplevel->entry_count;
                if (toplevel->entries[test_idx].visible) {
                    new_idx = test_idx;
                    break;
                }
            }
            break;
        }
        case XKB_KEY_Up:
        case XKB_KEY_Down: {
            // same column, smaller/bigger Y
            if (start_idx == -1) {
                new_idx = first_visible_entry(toplevel);
                toplevel->target_y = 0.0;
                break;
            }

            update_target_y = true;
            int delta = event.keysym == XKB_KEY_Up ? -1 : 1;
            int target_column = toplevel->entries[start_idx].column;
            for (int i = start_idx + delta; i >= 0 && i < toplevel->entry_count;
                 i += delta) {
                ToplevelPickerEntry *entry = &toplevel->entries[i];
                if (entry->visible && entry->column == target_column) {
                    new_idx = i;
                    break;
                }
            }
            break;
        }
        case XKB_KEY_Left:
        case XKB_KEY_Right: {
            if (start_idx == -1) {
                new_idx = first_visible_entry(toplevel);
                toplevel->target_y = 0.0;
                break;
            }

            int start_column = toplevel->entries[start_idx].column;
            // Find the nearest column with visible entries on the
            // corresponding side.
            int best_column = -1;
            for (int i = 0; i < toplevel->entry_count; i++) {
                ToplevelPickerEntry *entry = &toplevel->entries[i];
                if (!entry->visible) {
                    continue;
                }
                bool correct_side = event.keysym == XKB_KEY_Left
                                        ? entry->column < start_column
                                        : entry->column > start_column;
                if (!correct_side) {
                    continue;
                }
                if (best_column == -1 || (event.keysym == XKB_KEY_Left
                                              ? entry->column > best_column
                                              : entry->column < best_column)) {
                    best_column = entry->column;
                }
            }
            if (best_column == -1) {
                break;
            }

            // Then, find the entry in that column nearest to the virtual
            // Y position. Entries containing it count as a distance of zero.
            int best_idx = start_idx;
            double best_dist = INFINITY;
            double best_center_dist = INFINITY;
            for (int i = 0; i < toplevel->entry_count; i++) {
                ToplevelPickerEntry *entry = &toplevel->entries[i];
                if (!entry->visible || entry->column != best_column) {
                    continue;
                }

                double top = entry->full_bounds.y;
                double bottom = top + entry->full_bounds.height;
                double dist;
                if (toplevel->target_y < top) {
                    dist = top - toplevel->target_y;
                } else if (toplevel->target_y > bottom) {
                    dist = toplevel->target_y - bottom;
                } else {
                    dist = 0;
                }
                double center_dist =
                    fabs((top + bottom) / 2.0 - toplevel->target_y);
                if (dist < best_dist ||
                    (dist == best_dist && center_dist < best_center_dist)) {
                    best_idx = i;
                    best_dist = dist;
                    best_center_dist = center_dist;
                }
            }
            new_idx = best_idx;
            break;
        }
        }
    }

    if (start_idx != new_idx) {
        toplevel->keyboard_focus_idx = new_idx;
        if (update_target_y) {
            toplevel->target_y = entry_target_y(&toplevel->entries[new_idx]);
        }

        // Make sure the newly focused entry is on screen.
        BBox bounds = toplevel->entries[new_idx].full_bounds;
        double view_height = picker->surface->device_height;
        if (bounds.y < toplevel->scroll_y) {
            toplevel->scroll_y = bounds.y;
        } else if (
            bounds.y + bounds.height > toplevel->scroll_y + view_height
        ) {
            toplevel->scroll_y = bounds.y + bounds.height - view_height;
        }
        // The entry may be taller than the view.
        clamp_scroll(picker);

        overlay_surface_queue_draw(picker->surface);
    }
}

static void toplevel_picker_handle_resize(PickerSurface *picker) {
    ToplevelPicker *toplevel = &picker->toplevel;
    recompute_grid(picker);
    // The old target Y position is meaningless in the new layout.
    int idx = focused_entry(toplevel);
    if (idx != -1) {
        toplevel->target_y = entry_target_y(&toplevel->entries[idx]);
    }
    update_mouse_hover(picker, false);
    // The picker is going to get redrawn anyway
}

static void toplevel_picker_draw(PickerSurface *picker, RenderDisplayList *dl) {
    ToplevelPicker *toplevel = &picker->toplevel;
    OverlaySurface *surface = picker->surface;

    // TODO: Make configurable (also see output and region pickers)
    RENDER_RECT(
        *dl,
        .bounds = (BBox){0, 0, surface->device_width, surface->device_height},
        .color = {0.025, 0.025, 0.025, 0.4}
    );

    double scroll = toplevel->scroll_y;
    int focused = focused_entry(toplevel);
    for (int i = 0; i < toplevel->entry_count; i++) {
        ToplevelPickerEntry *entry = &toplevel->entries[i];
        if (!entry->visible) {
            continue;
        }

        // Cull entries which are fully outside the screen
        if (entry->full_bounds.y + entry->full_bounds.height < scroll ||
            entry->full_bounds.y > scroll + surface->device_height) {
            continue;
        }

        if (focused == i) {
            // highlight
            RENDER_RECT(
                *dl,
                .bounds = bbox_translate(entry->full_bounds, 0, -scroll),
                .color = {0.5, 0.5, 0.5, 0.5},
                .border_radius =
                    RENDER_BORDER_RADIUS(16 * surface->scale / 120.0),
            );
        }

        // image preview
        RENDER_RECT(
            *dl,
            .bounds = bbox_translate(entry->image_bounds, 0, -scroll),
            .color = RENDER_COLOR_DEFAULT,
            .texture = capture_frame_get_texture(entry->frame),
            .border_radius = RENDER_BORDER_RADIUS(
                8 * surface->scale / 120.0
            ), // TODO: make configurable?
            .border_color = {1.0, 1.0, 1.0, 1.0},
            .border_width = RENDER_BORDER_WIDTH(1 * surface->scale / 120.0),
            .uv = RENDER_UV_DEFAULT,
        );
        // label background
        RENDER_RECT(
            *dl,
            .bounds =
                bbox_translate(entry->label_background_bounds, 0, -scroll),
            .color = {0, 0, 0, 0.75},
            .border_radius = RENDER_BORDER_RADIUS(4),
        );
        // label
        RENDER_TEXT(
            *dl,
            .x = entry->label_text_x,
            .y = entry->label_text_y - scroll,
            .content = entry->title,
            .length = -1,
            .style = toplevel->label_text_style,
            .color = RENDER_COLOR_DEFAULT,
        );
    }
}

static int entry_compare(const void *a, const void *b) {
    const ToplevelPickerEntry *entry_a = a;
    const ToplevelPickerEntry *entry_b = b;
    return strcoll(entry_a->title, entry_b->title);
}

static void toplevel_picker_init(PickerSurface *picker) {
    struct wl_list *captures = picker->ctx->captures;
    int toplevel_captures = 0;
    CaptureFrame *capture;
    wl_list_for_each(capture, captures, link) {
        if (capture->type == CAPTURE_FRAME_TYPE_TOPLEVEL) {
            toplevel_captures++;
        }
    }

    picker->toplevel.entries =
        calloc(toplevel_captures, sizeof(ToplevelPickerEntry));
    picker->toplevel.entry_count = toplevel_captures;
    picker->toplevel.keyboard_focus_idx = -1;
    picker->toplevel.mouse_focus_idx = -1;

    int i = 0;
    wl_list_for_each(capture, captures, link) {
        if (capture->type == CAPTURE_FRAME_TYPE_TOPLEVEL) {
            picker->toplevel.entries[i] = (ToplevelPickerEntry){
                .frame = capture,
                // In practice the title should always be here,
                // but don't crash if it isn't
                .title = strdup(
                    capture->toplevel->title ? capture->toplevel->title : "???"
                ),
                .visible = true,
            };

            i++;
        }
    }

    // We want alphabetical order.
    qsort(
        picker->toplevel.entries,
        toplevel_captures,
        sizeof(ToplevelPickerEntry),
        entry_compare
    );
}

static void toplevel_picker_enter(PickerSurface *picker) {
    if (update_mouse_hover(picker, false)) {
        overlay_surface_queue_draw(picker->surface);
    }
}

void toplevel_picker_destroy(PickerSurface *picker) {
    ToplevelPicker *toplevel = &picker->toplevel;
    for (int i = 0; i < toplevel->entry_count; i++) {
        capture_frame_destroy(toplevel->entries[i].frame);
        free(toplevel->entries[i].title);
    }
    free(toplevel->entries);
}

const PickerVTable toplevel_picker_vtable = {
    .per_output = false,
    .init = toplevel_picker_init,
    .enter = toplevel_picker_enter,
    .draw = toplevel_picker_draw,
    .mouse = toplevel_picker_handle_mouse,
    .keyboard = toplevel_picker_handle_keyboard,
    .resize = toplevel_picker_handle_resize,
};
