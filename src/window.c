#include "window.h"
#include "key.h"
#include "util.h"
#include <dbus/dbus.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <yyjson.h>

void mzd_dbus_error_guard(DBusError *error) {
    if (dbus_error_is_set(error)) {
        fprintf(stderr, "%s", error->message);
        dbus_error_free(error);
        abort();
    }
}

DBusMessage *mzd_dbus_call_create(const char *method) {
    DBusMessage *query = dbus_message_new_method_call(
        "org.gnome.Shell",
        "/org/gnome/Shell/Extensions/Windows",
        "org.gnome.Shell.Extensions.Windows",
        method
    );

    return query;
}

DBusMessage *mzd_dbus_call_create_with_window(const char *method, const struct MzdWindow *window) {
    DBusMessage *query = mzd_dbus_call_create(method);
    dbus_message_append_args(query, DBUS_TYPE_UINT32, &window->id, DBUS_TYPE_INVALID);

    return query;
}



void mzd_window_free(const struct MzdWindow *window) {
    free((void *) window->class);
    free((void *) window->class_instance);
    free((void *) window);
}

void mzd_windowv_free(const struct MzdWindow **windows) {
    for (int i = 0; windows[i]; i++)
        mzd_window_free(windows[i]);
    free(windows);
}

bool mzd_window_filter(struct MzdWindowFilter *window_filter, const struct MzdWindow *window) {
    return window_filter->call(window, window_filter->value);
}



void mzd_window_manipulator_dbus_connect(struct MzdWindowManipulator *window_manipulator) {
    DBusError dbus_error;
    dbus_error_init(&dbus_error);

    DBusConnection *connection = dbus_bus_get_private(DBUS_BUS_SESSION, &dbus_error);
    window_manipulator->dbus = connection;
    mzd_dbus_error_guard(&dbus_error);
}

void mzd_window_manipulator_setting_connect(struct MzdWindowManipulator *window_manipulator) {
    window_manipulator->gsettings = mzd_keybind_gsettings();
}

void mzd_window_manipulator_setting_keybind(struct MzdWindowManipulator *window_manipulator) {
    const mzd_key **minimize_keybindv = mzd_unsafe_keybindv_extract_minimize(window_manipulator->gsettings);
    window_manipulator->minimize_keybind = minimize_keybindv[0];
    for (int i = 1; minimize_keybindv[i]; i++)
        free((void *) minimize_keybindv[i]);
    free(minimize_keybindv);

    const mzd_key **close_keybindv = mzd_unsafe_keybindv_extract_close(window_manipulator->gsettings);
    window_manipulator->close_keybind = close_keybindv[0];
    for (int i = 1; close_keybindv[i]; i++)
        free((void *) close_keybindv[i]);
    free(close_keybindv);
}



void mzd_window_manipulator_uinput_attach(struct MzdWindowManipulator *window_manipulator) {
    window_manipulator->fd = mzd_uinput_connect();
    mzd_uinput_prepare(window_manipulator->fd, window_manipulator->minimize_keybind);
    mzd_uinput_prepare(window_manipulator->fd, window_manipulator->close_keybind);
    mzd_uinput_setup(window_manipulator->fd);
}

void mzd_window_manipulator_uinput_use_minimize(const struct MzdWindowManipulator *window_manipulator) {
    mzd_uinput_use(window_manipulator->fd, window_manipulator->minimize_keybind);
}

void mzd_window_manipulator_uinput_use_close(const struct MzdWindowManipulator *window_manipulator) {
    mzd_uinput_use(window_manipulator->fd, window_manipulator->close_keybind);
}



DBusMessage *mzd_unsafe_window_manipulator_dbus_call_send(const struct MzdWindowManipulator *window_manipulator, DBusMessage *query) {
    DBusError dbus_error;
    dbus_error_init(&dbus_error);

    DBusMessage *response = dbus_connection_send_with_reply_and_block(
        window_manipulator->dbus,
        query,
        500,
        &dbus_error
    );
    mzd_dbus_error_guard(&dbus_error);

    dbus_error_free(&dbus_error);
    return response;
}

const struct MzdWindow **mzd_unsafe_window_manipulator_dbus_call_list(const struct MzdWindowManipulator *window_manipulator, DBusMessage *query) {
    DBusError dbus_error;
    dbus_error_init(&dbus_error); 

    DBusMessage *response =  mzd_unsafe_window_manipulator_dbus_call_send(window_manipulator, query);
    const char *windows_str = 0;
    dbus_message_get_args(response, &dbus_error, DBUS_TYPE_STRING, &windows_str, DBUS_TYPE_INVALID);
    yyjson_doc *const doc = yyjson_read(windows_str, strlen(windows_str), 0);
    yyjson_val *const root = yyjson_doc_get_root(doc);

    yyjson_arr_iter arr_iter = yyjson_arr_iter_with(root);
    yyjson_val *obj;
    const size_t count = yyjson_arr_size(root);
    const struct MzdWindow **windows = malloc((count + 1) * sizeof(struct MzdWindow *));
    while ((obj = yyjson_arr_iter_next(&arr_iter))) {
        yyjson_obj_iter obj_iter = yyjson_obj_iter_with(obj);
        yyjson_val *key, *val;
        struct MzdWindow *const window = malloc(sizeof(struct MzdWindow));
        while ((key = yyjson_obj_iter_next(&obj_iter))) {
            val = yyjson_obj_iter_get_val(key);
            const char *key_str = key->uni.str;

            switch (mzd_str_djb2(key_str)) {
                case MZD_DJB2_IN_CURRENT_WORKSPACE:
                    window->in_current_workspace = unsafe_yyjson_get_bool(val);
                    break;
                case MZD_DJB2_WM_CLASS:
                    window->class = strdup(unsafe_yyjson_get_str(val));
                    break;
                case MZD_DJB2_WM_CLASS_INSTANCE:
                    window->class_instance = strdup(unsafe_yyjson_get_str(val));
                    break;
                case MZD_DJB2_PID:
                    window->pid = unsafe_yyjson_get_int(val);
                    break;
                case MZD_DJB2_ID:
                    window->id = unsafe_yyjson_get_uint(val);
                    break;
                case MZD_DJB2_FRAME_TYPE:
                    window->frame_type = unsafe_yyjson_get_uint(val);
                    break;
                case MZD_DJB2_WINDOW_TYPE:
                    window->type = unsafe_yyjson_get_uint(val);
                    break;
                case MZD_DJB2_FOCUS:
                    window->focus = unsafe_yyjson_get_bool(val);
                    break;
            }
        }
        windows[arr_iter.idx - 1] = window;
    }
    windows[count] = 0;

    yyjson_doc_free(doc);
    dbus_message_unref(response);
    dbus_error_free(&dbus_error);
    return windows;
}

const char *mzd_unsafe_window_manipulator_dbus_call_title(const struct MzdWindowManipulator *window_manipulator, DBusMessage *query) {
    DBusError dbus_error;
    dbus_error_init(&dbus_error);

    DBusMessage *response = mzd_unsafe_window_manipulator_dbus_call_send(window_manipulator, query);
    const char *title;
    dbus_message_get_args(response, &dbus_error, DBUS_TYPE_STRING, &title, DBUS_TYPE_INVALID);

    dbus_message_unref(response);
    dbus_error_free(&dbus_error);
    return title;
}

void mzd_unsafe_window_manipulator_dbus_call(const struct MzdWindowManipulator *window_manipulator, DBusMessage *query) {
    dbus_message_unref(mzd_unsafe_window_manipulator_dbus_call_send(window_manipulator, query));
}

void mzd_unsafe_window_manipulator_dbus_call_with_window(const struct MzdWindowManipulator *window_manipulator, const char *method, const struct MzdWindow *window) {
    DBusMessage *query = mzd_dbus_call_create_with_window(method, window);
    mzd_unsafe_window_manipulator_dbus_call(window_manipulator, query);

    dbus_message_unref(query);
}



const struct MzdWindow **mzd_window_manipulator_list(const struct MzdWindowManipulator *window_manipulator) {
    DBusMessage *query = mzd_dbus_call_create("List");
    const struct MzdWindow **windows = mzd_unsafe_window_manipulator_dbus_call_list(window_manipulator, query);

    dbus_message_unref(query);
    return windows;
}

void mzd_window_manipulator_focus(const struct MzdWindowManipulator *window_manipulator, const struct MzdWindow *window) {
    mzd_unsafe_window_manipulator_dbus_call_with_window(window_manipulator, "Activate", window);
}

const char *mzd_window_manipulator_title(const struct MzdWindowManipulator *window_manipulator, const struct MzdWindow *window) {
    DBusMessage *query = mzd_dbus_call_create_with_window("GetTitle", window);
    const char *title = mzd_unsafe_window_manipulator_dbus_call_title(window_manipulator, query);

    dbus_message_unref(query);
    return title;
}

void mzd_window_manipulator_minimize(const struct MzdWindowManipulator *window_manipulator, const struct MzdWindow *window, const unsigned short flags) {
    if (mzd_flags_has(flags, MZD_KEYBIND)) {
        if (window->focus)
            mzd_flags_has(flags, MZD_CLOSE) ?
                mzd_window_manipulator_uinput_use_close(window_manipulator) :
                mzd_window_manipulator_uinput_use_minimize(window_manipulator);
        else
            mzd_window_manipulator_focus(window_manipulator, window);
    }
    else
        mzd_flags_has(flags, MZD_CLOSE) ?
            mzd_unsafe_window_manipulator_dbus_call_with_window(window_manipulator, "Close", window) :
            mzd_unsafe_window_manipulator_dbus_call_with_window(window_manipulator, "Minimize", window);
}

void mzd_window_manipulator_match(const struct MzdWindowManipulator *window_manipulator, struct MzdWindowFilter *window_filter, const unsigned short flags) {
    struct timespec time, remaining;
    time.tv_sec = 0;
    time.tv_nsec = mzd_nanoseconds_ms(500);

    for (long t = 0; t < mzd_nanoseconds_s(5); t += time.tv_nsec) {
        const struct MzdWindow **windows = mzd_window_manipulator_list(window_manipulator);
        for (int i = 0; windows[i]; i++) {
            const struct MzdWindow *window = windows[i];
            if (mzd_window_filter(window_filter, window)) {
                mzd_window_manipulator_minimize(window_manipulator, window, flags);

                if (mzd_flags_has(flags, MZD_FIRST)) {
                    mzd_windowv_free(windows);

                    // really not evil, breaking out of nested loop
                    goto end;
                }
            }
        }

        nanosleep(&time, &remaining);
        mzd_windowv_free(windows);
    }

// no-op
end:;
}



void mzd_window_manipulator_free(const struct MzdWindowManipulator *window_manipulator) {
    dbus_connection_flush(window_manipulator->dbus);
    dbus_connection_close(window_manipulator->dbus);
    dbus_connection_unref(window_manipulator->dbus);
    dbus_shutdown();
    if (window_manipulator->gsettings)
        g_object_unref(window_manipulator->gsettings);
    if (window_manipulator->minimize_keybind)
        free((void *)window_manipulator->minimize_keybind);
    if (window_manipulator->close_keybind)
        free((void *)window_manipulator->close_keybind);
    if (window_manipulator->fd)
        mzd_uinput_free(window_manipulator->fd);
}
