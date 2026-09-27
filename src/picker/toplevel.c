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

static void recompute_grid(PickerSurface *picker) {
    ToplevelPicker *toplevel = &picker->toplevel;

    int columns;
    double scale = picker->surface->scale / 120.0;
    double column_width = round(DEFAULT_COLUMN_WIDTH * scale);
    double padding = round(PADDING * scale);
    double avail_width = picker->surface->device_width - 2.0 * padding;
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
        // TODO: cap the scale here?
        double image_height =
            round((column_width / entry->frame->width) * entry->frame->height);
        BBox image_bounds = (BBox){
            column_x[this_column],
            column_y[this_column],
            column_width,
            image_height
        };
        entry->image_bounds = image_bounds;
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

    free(column_x);
    free(column_y);
}

static void recalculate_hover(PickerSurface *picker) {
    ToplevelPicker *toplevel = &picker->toplevel;
    SeatDispatcher *seat = wayland_globals.seat_dispatcher;
    double scale = picker->surface->scale / 120.0;
    int new_idx = -1;
    if (seat->pointer_data.focus == picker->surface->wl_surface) {
        double x = seat->pointer_data.surface_x * scale;
        double y = seat->pointer_data.surface_y * scale;
        for (int i = 0; i < toplevel->entry_count; i++) {
            ToplevelPickerEntry *entry = &toplevel->entries[i];
            if (!entry->visible) {
                continue;
            }
            if (entry->full_bounds.x <= x &&
                x <= entry->full_bounds.x + entry->full_bounds.width &&
                entry->full_bounds.y <= y &&
                y <= entry->full_bounds.y + entry->full_bounds.height) {
                new_idx = i;
                break;
            }
        }
    }

    if (toplevel->highlight_index != new_idx) {
        if (toplevel->is_keyboard_focus && new_idx == -1) {
            // Keyboard focus is stronger than mouse focus.
            return;
        }
        toplevel->highlight_index = new_idx;
        toplevel->is_keyboard_focus = false;
        overlay_surface_queue_draw(picker->surface);
    }
}

static void confirm_selection(PickerSurface *picker) {
    ToplevelPicker *toplevel = &picker->toplevel;
    assert(toplevel->highlight_index != -1);
    ToplevelPickerEntry *entry = &toplevel->entries[toplevel->highlight_index];
    Image *img = capture_frame_steal_image(entry->frame);
    picker_context_finish(picker, PICKER_FINISH_REASON_SELECTED, img);
}

void toplevel_picker_handle_mouse(PickerSurface *picker, MouseEvent event) {
    ToplevelPicker *toplevel = &picker->toplevel;

    if (event.buttons_pressed & POINTER_BUTTON_LEFT) {
        if (toplevel->is_keyboard_focus) {
            toplevel->highlight_index = -1;
            toplevel->is_keyboard_focus = false;
            overlay_surface_queue_draw(picker->surface);
        } else {
            recalculate_hover(picker);
            if (toplevel->highlight_index == -1) {
                picker_context_finish(
                    picker, PICKER_FINISH_REASON_CANCELLED, NULL
                );
            } else {
                confirm_selection(picker);
            }
        }
    } else {
        recalculate_hover(picker);
    }
}

void toplevel_picker_handle_keyboard(
    PickerSurface *picker, KeyboardEvent event
) {
    ToplevelPicker *toplevel = &picker->toplevel;

    char name[64];
    xkb_keysym_get_name(event.keysym, name, 64);
    log_debug("keysym: %x (%s)\n", event.keysym, name);
    int new_idx = toplevel->highlight_index;
    if (event.type == KEYBOARD_EVENT_PRESS) {
        switch (event.keysym) {
        case XKB_KEY_Return:
            if (toplevel->highlight_index != -1) {
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
            int test_idx = toplevel->highlight_index;
            if (toplevel->highlight_index == -1) {
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
            // same X, smaller/bigger Y
            if (toplevel->entry_count == 0) {
                break;
            }
            if (toplevel->highlight_index == -1) {
                // Highlight the first visible entry.
                for (int i = 0; i < toplevel->entry_count; i++) {
                    if (toplevel->entries[i].visible) {
                        new_idx = i;
                        break;
                    }
                }
                break;
            }

            int delta = event.keysym == XKB_KEY_Up ? -1 : 1;
            BBox target_bounds =
                toplevel->entries[toplevel->highlight_index].full_bounds;
            for (int i = toplevel->highlight_index + delta;
                 i >= 0 && i < toplevel->entry_count;
                 i += delta) {
                ToplevelPickerEntry *entry = &toplevel->entries[i];
                if (entry->visible && entry->full_bounds.x == target_bounds.x) {
                    new_idx = i;
                    break;
                }
            }
            break;
        }
        case XKB_KEY_Left:
        case XKB_KEY_Right: {
            // first closest X on corresponding side, then closest Y
            // TODO: This algorithm works, but isn't the most intuitive thing in
            // the world.
            if (toplevel->entry_count == 0) {
                break;
            }
            if (toplevel->highlight_index == -1) {
                // Highlight the first visible entry.
                for (int i = 0; i < toplevel->entry_count; i++) {
                    if (toplevel->entries[i].visible) {
                        new_idx = i;
                        break;
                    }
                }
                break;
            }

            int best_idx = toplevel->highlight_index;
            double best_x = event.keysym == XKB_KEY_Left ? -INFINITY : INFINITY;
            double best_center_y = INFINITY;
            BBox start_bounds =
                toplevel->entries[toplevel->highlight_index].full_bounds;
            double start_x = start_bounds.x;
            double start_center_y = start_bounds.y + start_bounds.height / 2.0;

            for (int i = 0; i < toplevel->entry_count; i++) {
                ToplevelPickerEntry *entry = &toplevel->entries[i];
                if (!entry->visible) {
                    continue;
                }

                BBox test_bounds = entry->full_bounds;
                double test_center_y = test_bounds.y + test_bounds.height / 2.0;
                if (test_bounds.x == start_x) {
                    continue;
                } else if (test_bounds.x == best_x) {
                    // We can still improve on Y
                    if (fabs(test_center_y - start_center_y) <
                        fabs(best_center_y - start_center_y)) {
                        best_idx = i;
                        best_center_y = test_center_y;
                    }
                } else {
                    // Check if the X is better
                    if (event.keysym == XKB_KEY_Left) {
                        // largest X smaller than start_x
                        if (test_bounds.x < start_x && test_bounds.x > best_x) {
                            best_idx = i;
                            best_x = test_bounds.x;
                            best_center_y = test_center_y;
                        }
                    } else {
                        // smallest X larger than start_x
                        if (test_bounds.x > start_x && test_bounds.x < best_x) {
                            best_idx = i;
                            best_x = test_bounds.x;
                            best_center_y = test_center_y;
                        }
                    }
                }
            }
            new_idx = best_idx;
            break;
        }
        }
    }

    if (toplevel->highlight_index != new_idx) {
        toplevel->highlight_index = new_idx;
        toplevel->is_keyboard_focus = true;
        overlay_surface_queue_draw(picker->surface);
    }
}

void toplevel_picker_handle_scale(PickerSurface *picker) {
    recalculate_hover(picker);
}

void toplevel_picker_draw(PickerSurface *picker, RenderDisplayList *dl) {
    ToplevelPicker *toplevel = &picker->toplevel;
    OverlaySurface *surface = picker->surface;
    if (surface->device_width != toplevel->last_width ||
        surface->device_height != toplevel->last_height ||
        surface->scale != toplevel->last_scale) {
        recompute_grid(picker);
        toplevel->last_width = surface->device_width;
        toplevel->last_height = surface->device_height;
        toplevel->last_scale = surface->scale;
    }

    // TODO: Make configurable (also see output and region pickers)
    RENDER_RECT(
        *dl,
        .bounds = (BBox){0, 0, surface->device_width, surface->device_height},
        .color = {0.025, 0.025, 0.025, 0.4}
    );

    // TODO: scrolling
    for (int i = 0; i < toplevel->entry_count; i++) {
        ToplevelPickerEntry *entry = &toplevel->entries[i];
        if (!entry->visible) {
            continue;
        }

        // Cull entries which are fully outside the screen
        if (entry->full_bounds.y + entry->full_bounds.height < 0 ||
            entry->full_bounds.y > surface->device_height) {
            continue;
        }

        if (toplevel->highlight_index == i) {
            // highlight
            RENDER_RECT(
                *dl,
                .bounds = entry->full_bounds,
                .color = {0.5, 0.5, 0.5, 0.5},
                .border_radius =
                    RENDER_BORDER_RADIUS(16 * surface->scale / 120.0),
            );
        }

        // image preview
        RENDER_RECT(
            *dl,
            .bounds = entry->image_bounds,
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
            .bounds = entry->label_background_bounds,
            .color = {0, 0, 0, 0.75},
            .border_radius = RENDER_BORDER_RADIUS(4),
        );
        // label
        RENDER_TEXT(
            *dl,
            .x = entry->label_text_x,
            .y = entry->label_text_y,
            .content = entry->title,
            .length = -1,
            .style = toplevel->label_text_style,
            .color = RENDER_COLOR_DEFAULT,
        );
    }
}

int entry_compare(const void *a, const void *b) {
    const ToplevelPickerEntry *entry_a = a;
    const ToplevelPickerEntry *entry_b = b;
    return strcoll(entry_a->title, entry_b->title);
}

void toplevel_picker_init(PickerSurface *picker, struct wl_list *captures) {
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
    picker->toplevel.highlight_index = -1;

    int i = 0;
    wl_list_for_each(capture, captures, link) {
        if (capture->type == CAPTURE_FRAME_TYPE_TOPLEVEL) {
            picker->toplevel.entries[i] = (ToplevelPickerEntry){
                .frame = capture,
                .title = strdup(capture->toplevel->title),
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

void toplevel_picker_enter(PickerSurface *picker) { recalculate_hover(picker); }

void toplevel_picker_destroy(PickerSurface *picker) {
    ToplevelPicker *toplevel = &picker->toplevel;
    if (toplevel->entry_count > 0) {
        for (int i = 0; i < toplevel->entry_count; i++) {
            capture_frame_destroy(toplevel->entries[i].frame);
            free(toplevel->entries[i].title);
        }
        free(toplevel->entries);
    }
}
