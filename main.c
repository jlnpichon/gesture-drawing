#include <ctype.h>
#include <errno.h>
#include <libgen.h>
#include <math.h>
#include <string.h>

#include <gtk/gtk.h>

#include "utils.h"

struct slideshow_application {
	GtkApplication *gtk_app;
	GtkWidget *window;
	GtkWidget *label_folder;
	GtkWidget *label_found;

	GArray *images;
	int image_index;
	char *current_image;

	/* Canvas window. */
	GList *viewed_images;
	GList *viewed_images_iterator;
	GtkWidget *label_timer;

	GtkWidget *window2;
	GtkWidget *scrolled_window;
	GtkWidget *image;
	GtkWidget *button_play;
	int pause;
	int canvas_width, canvas_height;

	GRand *grand;

	int timer;
	int timeout;
};

static int scan_folder(const char *folder, struct slideshow_application *app)
{
	GDir *dir;
	const char *filename;
	GError *error = NULL;

	dir = g_dir_open(folder, 0, &error);
	if (!dir) {
		GtkWidget *message = gtk_message_dialog_new(GTK_WINDOW(app->window),
				GTK_DIALOG_MODAL,
				GTK_MESSAGE_ERROR,
				GTK_BUTTONS_OK,
				"%s", error->message);

		gtk_dialog_run(GTK_DIALOG(message));
		gtk_widget_destroy(message);

		return -1;
	}

	while ((filename = g_dir_read_name(dir))) {
		const char *path;

		path = g_build_filename(folder, filename, NULL);
		if (g_file_test(path, G_FILE_TEST_IS_DIR)) {
			scan_folder(path, app);
			g_free((void *) path);
			continue;
		}

		if (is_image(path))
			g_array_append_val(app->images, path);
		else
			g_free((void *) path);
	}

	g_dir_close(dir);

	return 0;
}

static void resize_image(struct slideshow_application *app, int width, int height, int force)
{
	GError *error = NULL;

	if (!force && (app->canvas_width == width && app->canvas_height == height))
		return;

	GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file_at_size(
			app->current_image,
			width - 20, height - 100,
			&error);
	gtk_image_set_from_pixbuf(GTK_IMAGE(app->image), pixbuf);

	app->canvas_width = width;
	app->canvas_height = height;
}

static void __prev_image(struct slideshow_application *app)
{
	if (!app->viewed_images_iterator) {
		app->viewed_images = g_list_append(app->viewed_images, app->current_image);
		app->viewed_images_iterator = g_list_last(app->viewed_images);
	}

	if (!g_list_previous(app->viewed_images_iterator))
		return;

	app->viewed_images_iterator = g_list_previous(app->viewed_images_iterator);
	app->current_image = app->viewed_images_iterator->data;
}

static void __next_image(struct slideshow_application *app)
{
	if (app->viewed_images_iterator) {
		app->viewed_images_iterator = g_list_next(app->viewed_images_iterator);

		if (app->viewed_images_iterator) {
			app->current_image = app->viewed_images_iterator->data;
			return;
		}
	} else {
		app->viewed_images = g_list_append(app->viewed_images, app->current_image);
	}

	app->image_index = g_rand_int_range(app->grand, 0, app->images->len);
	app->current_image = g_array_index(app->images, char *, app->image_index);
	g_array_remove_index(app->images, app->image_index);
}

static void update_timer_label(GtkLabel *label, int t)
{
	char strtime[32];

	seconds_to_time(strtime, sizeof(strtime), t);
	gtk_label_set_text(label, strtime);
}

static void button_prev_clicked(GtkApplication *gtk_app, gpointer data)
{
	struct slideshow_application *app = (struct slideshow_application *) data;

	/* Reset the timer. */
	app->timeout = app->timer * 10;
	update_timer_label(GTK_LABEL(app->label_timer), app->timeout / 10);

	__prev_image(app);
	/* Trigger redraw of the image widget. */
	resize_image(app, app->canvas_width, app->canvas_height, 1);
}

static void button_next_clicked(GtkApplication *gtk_app, gpointer data)
{
	struct slideshow_application *app = (struct slideshow_application *) data;

	/* Reset the timer. */
	app->timeout = app->timer * 10;
	update_timer_label(GTK_LABEL(app->label_timer), app->timeout / 10);

	__next_image(app);
	/* Trigger redraw of the image widget. */
	resize_image(app, app->canvas_width, app->canvas_height, 1);
}

static void button_play_clicked(GtkApplication *gtk_app, gpointer data)
{
	GtkWidget *image;
	struct slideshow_application *app = (struct slideshow_application *) data;

	gtk_button_set_image(GTK_BUTTON(app->button_play), NULL);
	if (app->pause) {
		image = gtk_image_new_from_icon_name("media-playback-pause", GTK_ICON_SIZE_BUTTON);
		app->pause = 0;
	} else {
		image = gtk_image_new_from_icon_name("media-playback-start", GTK_ICON_SIZE_BUTTON);
		app->pause = 1;
	}
	gtk_button_set_image(GTK_BUTTON(app->button_play), image);
}

static void check_resized_callback(GtkApplication *gtk_app, gpointer data)
{
	struct slideshow_application *app = (struct slideshow_application *) data;

	int width, height;
	gtk_window_get_size(GTK_WINDOW(app->window2), &width, &height);
	resize_image(app, width, height, 0);
}

static int timeout_callback(void *data)
{
	struct slideshow_application *app = (struct slideshow_application *) data;

	if (!app->window2)
		return 0;

	if (!app->pause)
		app->timeout--;

	if (app->timeout < 0) {
		/* Reset the timer. */
		app->timeout = app->timer * 10;

		__next_image(app);
		/* Trigger redraw of the image widget. */
		resize_image(app, app->canvas_width, app->canvas_height, 1);
	} else if ((app->timeout % 10) == 0) {
		update_timer_label(GTK_LABEL(app->label_timer), app->timeout / 10);
	}

	g_timeout_add(100, timeout_callback, app);

	return 0;
}

static void destroy_window_callback(GtkApplication *gtk_app, gpointer data)
{
	struct slideshow_application *app = (struct slideshow_application *) data;

	gtk_widget_destroy(app->window2);
	app->window2 = NULL;

	/* Put all the viewed images back. */
	GList *it;
	for (it = g_list_first(app->viewed_images); it; it = it->next) {
		g_array_append_val(app->images, it->data);
	}
	if (!app->viewed_images_iterator)
		g_array_append_val(app->images, app->current_image);
	g_list_free(app->viewed_images);

	app->current_image = NULL;
	app->viewed_images = NULL;
	app->viewed_images_iterator = NULL;
	app->pause = 0;

	gtk_widget_show_all(app->window);
}

static void canvas_window_new(struct slideshow_application *app)
{
	app->window2 = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_default_size(GTK_WINDOW(app->window2), 800, 600);

	GtkWidget *vbox;
	vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
	gtk_box_set_homogeneous(GTK_BOX(vbox), FALSE);
	gtk_container_set_border_width(GTK_CONTAINER(vbox), 10);
	gtk_container_add(GTK_CONTAINER(app->window2), vbox);

	GtkWidget *align = gtk_alignment_new(1, 0, 0, 0);

	char strtime[32];
	seconds_to_time(strtime, sizeof(strtime), app->timer);
	app->label_timer = gtk_label_new(strtime);

	app->timeout = app->timer * 10;
	g_timeout_add(100, timeout_callback, app);

	gtk_container_add(GTK_CONTAINER(align), app->label_timer);
	gtk_box_pack_start(GTK_BOX(vbox), align, FALSE, FALSE, 0);

	GtkWidget *align_scrolled_window = gtk_alignment_new(1, 1, 1, 1);
	app->scrolled_window = gtk_scrolled_window_new(NULL, NULL);
	gtk_container_set_border_width(GTK_CONTAINER(app->scrolled_window), 0);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(app->scrolled_window),
			GTK_POLICY_AUTOMATIC,
			GTK_POLICY_AUTOMATIC);

	app->image = gtk_image_new();
	gtk_container_add(GTK_CONTAINER(app->scrolled_window), app->image);
	gtk_container_add(GTK_CONTAINER(align_scrolled_window), app->scrolled_window);
	gtk_box_pack_start(GTK_BOX(vbox), align_scrolled_window, TRUE, TRUE, 0);

	if (!app->grand)
		app->grand = g_rand_new_with_seed(time(NULL));

	/* Select the first image. */
	app->image_index = g_rand_int_range(app->grand, 0, app->images->len);
	app->current_image = g_array_index(app->images, char *, app->image_index);
	g_array_remove_index(app->images, app->image_index);
	/* Trigger redraw of the image widget. */
	resize_image(app, 800, 600, 1);

	g_signal_connect(app->window2, "check-resize", G_CALLBACK(check_resized_callback), app);
	g_signal_connect(app->window2, "destroy", G_CALLBACK(destroy_window_callback), app);

	GtkWidget *hbox;
	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);

	app->pause = 0;
	GtkWidget *image = gtk_image_new_from_icon_name("media-playback-pause",
			GTK_ICON_SIZE_BUTTON);
	app->button_play = gtk_button_new();
	gtk_button_set_image(GTK_BUTTON(app->button_play), image);

	GtkWidget *prev = gtk_button_new_from_icon_name("go-previous",
			GTK_ICON_SIZE_BUTTON);
	GtkWidget *next = gtk_button_new_from_icon_name("go-next",
			GTK_ICON_SIZE_BUTTON);
	gtk_box_pack_start(GTK_BOX(hbox), prev, 1, 1, 0);
	gtk_box_pack_start(GTK_BOX(hbox), app->button_play, 1, 1, 0);
	gtk_box_pack_start(GTK_BOX(hbox), next, 1, 1, 0);

	g_signal_connect(prev, "clicked", G_CALLBACK(button_prev_clicked), app);
	g_signal_connect(next, "clicked", G_CALLBACK(button_next_clicked), app);
	g_signal_connect(app->button_play, "clicked", G_CALLBACK(button_play_clicked), app);

	gtk_widget_hide(app->window);
	gtk_widget_show_all(app->window2);
}

static void button_folder_clicked(GtkApplication *gtk_app, gpointer data)
{
	int ret;
	GtkWidget *dialog;
	struct slideshow_application *app = (struct slideshow_application *) data;

	dialog = gtk_file_chooser_dialog_new("Choose a folder to scan",
			GTK_WINDOW(app->window),
			GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
			"gtk-cancel", GTK_RESPONSE_CANCEL,
			"gtk-ok", GTK_RESPONSE_OK,
			NULL);

	ret = gtk_dialog_run(GTK_DIALOG(dialog));
	if (ret == GTK_RESPONSE_OK) {
		char *folder;
		char label[512];
		GtkFileChooser *chooser  = GTK_FILE_CHOOSER(dialog);

		folder = gtk_file_chooser_get_filename(chooser);
		char *bname = g_path_get_basename(folder);
		snprintf(label, sizeof(label), "Folder:  %s", bname);
		gtk_label_set_text(GTK_LABEL(app->label_folder), label);
		g_free(bname);

		scan_folder(folder, app);

		snprintf(label, sizeof(label), "Found: %d images", app->images->len);
		gtk_label_set_text(GTK_LABEL(app->label_found), label);

		g_free(folder);
	}

	gtk_widget_destroy(dialog);
}

static void button_draw_clicked(GtkApplication *gtk_app, gpointer data)
{
	struct slideshow_application *app = (struct slideshow_application *) data;

	if (app->images->len == 0) {
		GtkWidget *message = gtk_message_dialog_new(GTK_WINDOW(app->window),
				GTK_DIALOG_MODAL,
				GTK_MESSAGE_ERROR,
				GTK_BUTTONS_OK,
				"No image found, please select a folder containing images.");

		gtk_dialog_run(GTK_DIALOG(message));
		gtk_widget_destroy(message);

		return;
	}

	canvas_window_new(app);
}

static void radio_toggled(GtkWidget *button, gpointer data)
{
	int state;
	const char *button_label;
	struct slideshow_application *app = (struct slideshow_application *) data;

	button_label = gtk_button_get_label(GTK_BUTTON(button));
	state = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(button));
	if (!state)
		return;

	int multiply;
	const char *suffix = button_label;
	const char *end = button_label + strlen(button_label);

	while (isdigit(*suffix) && suffix < end)
		suffix++;

	g_assert(suffix != end);
	if (*suffix == 's') {
		multiply = 1;
	} else {
		g_assert(strlen(suffix) == 2);
		g_assert(!strcmp(suffix, "mn"));
		multiply = 60;
	}

	char *endptr = NULL;
	errno = 0;
	app->timer = g_ascii_strtoull(button_label, &endptr, 10);
	g_assert(endptr != button_label);
	g_assert(errno == 0);
	app->timer *= multiply;
}

static void activate(GtkApplication *gtk_app, gpointer data)
{
	GtkWidget *button_folder;
	GtkWidget *button_draw;
	GtkWidget *box_main;
	GtkWidget *box;
	GtkWidget *radio_30, *radio_45, *radio_60, *radio_1mn, *radio_2mn;
	struct slideshow_application *app = (struct slideshow_application *) data;

	app->window = gtk_application_window_new(gtk_app);
	gtk_window_set_title(GTK_WINDOW(app->window), "Slideshow");
	gtk_container_set_border_width(GTK_CONTAINER(app->window), 10);

	box_main = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
	gtk_box_set_homogeneous(GTK_BOX(box_main), FALSE);
	gtk_container_add(GTK_CONTAINER(app->window), box_main);

	button_folder = gtk_button_new_with_label("Select a folder");
	g_signal_connect(button_folder, "clicked", G_CALLBACK(button_folder_clicked), app);
	gtk_box_pack_start(GTK_BOX(box_main), button_folder, 1, 1, 2);

	app->label_folder = gtk_label_new("Folder:");
	gtk_box_pack_start(GTK_BOX(box_main), app->label_folder, 0, 1, 2);

	app->label_found = gtk_label_new("Found:");
	gtk_box_pack_start(GTK_BOX(box_main), app->label_found, 0, 1, 2);

	box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
	gtk_box_set_homogeneous(GTK_BOX(box), TRUE);

	radio_30 = gtk_radio_button_new_with_label(NULL, "30s");
	radio_45 = gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(radio_30), "45s");
	radio_60 = gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(radio_30), "60s");
	radio_1mn = gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(radio_30), "2mn");
	radio_2mn = gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(radio_30), "5mn");

	g_signal_connect(radio_30, "toggled", G_CALLBACK(radio_toggled), app);
	g_signal_connect(radio_45, "toggled", G_CALLBACK(radio_toggled), app);
	g_signal_connect(radio_60, "toggled", G_CALLBACK(radio_toggled), app);
	g_signal_connect(radio_1mn, "toggled", G_CALLBACK(radio_toggled), app);
	g_signal_connect(radio_2mn, "toggled", G_CALLBACK(radio_toggled), app);

	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(radio_60), TRUE);

	gtk_box_pack_start(GTK_BOX(box), radio_30, 1, 1, 2);
	gtk_box_pack_start(GTK_BOX(box), radio_45, 1, 1, 2);
	gtk_box_pack_start(GTK_BOX(box), radio_60, 1, 1, 2);
	gtk_box_pack_start(GTK_BOX(box), radio_1mn, 1, 1, 2);
	gtk_box_pack_start(GTK_BOX(box), radio_2mn, 1, 1, 2);

	gtk_box_pack_start(GTK_BOX(box_main), box, 1, 1, 2);

	button_draw = gtk_button_new_with_label("Draw!");
	g_signal_connect(button_draw, "clicked", G_CALLBACK(button_draw_clicked), app);
	gtk_box_pack_start(GTK_BOX(box_main), button_draw, 1, 1, 2);

	gtk_widget_show_all(app->window);
}

static void application_destroy(struct slideshow_application *app)
{
	int i;

	if (app->grand)
		g_rand_free(app->grand);

	for (i = 0; i < app->images->len; i++) {
		char *path = g_array_index(app->images, char *, i);
		g_free(path);
	}
	g_array_free(app->images, TRUE);

	if (app->window2)
		gtk_widget_destroy(app->window2);

	g_object_unref(app->gtk_app);
}

int main(int argc, char *argv[])
{
	struct slideshow_application app;
	int ret;

	memset(&app, 0, sizeof(app));
	app.images = g_array_new(FALSE, TRUE, sizeof(char *));

	app.gtk_app = gtk_application_new("org.gtk.slideshow", G_APPLICATION_FLAGS_NONE);
	g_signal_connect(app.gtk_app, "activate", G_CALLBACK(activate), &app);
	ret = g_application_run(G_APPLICATION(app.gtk_app), argc, argv);

	application_destroy(&app);

	return ret;
}
