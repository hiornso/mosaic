#include <gtk/gtk.h>
#include <stdlib.h>
#include <stdio.h>
#include <dirent.h>
#include <sys/stat.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>
#include <time.h>

#define eprintf(...) fprintf(stderr, __VA_ARGS__)

// be more native to MacOS
#if __APPLE__
#define PRIMARY_STRING "<Meta>"
#else
#define PRIMARY_STRING "<Control>"
#endif

typedef struct keyboard_shortcut {
	char *keycombo;
	void *data;
} KeyboardShortcut;

typedef struct coord {
	int x,y;
} Coord;

typedef struct omni {
	GMainLoop *mainloop;
	GtkWidget *win, *pic;
	GdkPixbuf **imgs;
	Coord *coords;
	long n_imgs;
	Coord looking_at;
	GdkPixbuf *view;
	int viewport_width, width, viewport_height, height;
	int xbounds[2], ybounds[2];
	bool *folds;
	Coord *offsets;
	GdkPixbuf *copy;
	time_t last_coord_dump;
} Omni;

int filter_only_files(const struct dirent *a) {
	return a->d_type == DT_REG || a->d_type == DT_UNKNOWN;
}

void regenerate_coords(Omni *omni) {
	Coord c = {0,0};
	int direction = 1;
	for (int i = 0;; ++i) {
		omni->coords[i] = c;
		if (i >= omni->n_imgs - 1) break;
		bool fold = omni->folds[i];
		if (fold) {
			c.x -= omni->width;
			direction *= -1;
		} else {
			c.y += direction * omni->height;
		}
		c.x += omni->offsets[i].x;
		c.y += omni->offsets[i].y;
	}
}

void regenerate_bounding_box(Omni *omni) {
	int xbounds[2] = {INT_MAX, INT_MIN};
	int ybounds[2] = {INT_MAX, INT_MIN};
	
	for (int i = 0; i < omni->n_imgs; ++i) {
		xbounds[0] = MIN(xbounds[0], omni->coords[i].x - omni->width / 2);
		xbounds[1] = MAX(xbounds[1], omni->coords[i].x + omni->width / 2 + omni->width - omni->viewport_width);
		
		ybounds[0] = MIN(ybounds[0], omni->coords[i].y - omni->height / 2);
		ybounds[1] = MAX(ybounds[1], omni->coords[i].y + omni->height / 2 + omni->height - omni->viewport_height);
	}
	
	memcpy(omni->xbounds, xbounds, sizeof(xbounds));
	memcpy(omni->ybounds, ybounds, sizeof(ybounds));
}

void dump_coords(Omni *omni) {
	char fname[50];
	time_t t = time(NULL);
	strftime(fname, sizeof(fname), "mosaic_%F_%H-%M-%S.coords", localtime(&t));
	
	FILE *f = fopen(fname, "wb");
	if (f != NULL) {
		fprintf(f, "%li\n", omni->n_imgs);
		
		for (int i = 0; i < omni->n_imgs - 2; ++i) {
			fprintf(f, "%i,", omni->folds[i]);
		}
		fprintf(f, "%i\n", omni->folds[omni->n_imgs - 2]);
		
		for (int i = 0; i < omni->n_imgs - 2; ++i) {
			fprintf(f, "%i,%i,", omni->offsets[i].x, omni->offsets[i].y);
		}
		fprintf(f, "%i,%i\n", omni->offsets[omni->n_imgs - 2].x, omni->offsets[omni->n_imgs - 2].y);
		
		fclose(f);
		
		printf("Successfully dumped coords to file '%s'.\n", fname);
		
		omni->last_coord_dump = t;
	} else {
		eprintf("error opening file '%s' to dump coords to.\n", fname);
	}
}

void populate_view(Omni *omni, bool zoomed_out) {
	printf("populate looking_at=(%i,%i)\n", omni->looking_at.x, omni->looking_at.y);
	
	const int ZOOMFACTOR = zoomed_out ? 10 : 0; // add ZOOMFACTOR * img_dims onto the default viewport size
	
	const Coord looking_at = (Coord){
		omni->looking_at.x - omni->width * ZOOMFACTOR,
		omni->looking_at.y - omni->height * ZOOMFACTOR
	};
	const int viewport_width = omni->viewport_width + 2 * omni->width * ZOOMFACTOR;
	const int viewport_height = omni->viewport_height + 2 * omni->height * ZOOMFACTOR;
	
	gdk_pixbuf_fill(omni->view, 0x000000ff);
	
	for (int i = 0; i < omni->n_imgs; ++i) {
		int xdiff_tl = omni->coords[i].x - looking_at.x;
		int ydiff_tl = omni->coords[i].y - looking_at.y;
		
		int xdiff_br = xdiff_tl + omni->width - viewport_width;
		int ydiff_br = ydiff_tl + omni->height - viewport_height;
		
		int  xsrc_tl = -MIN(0, xdiff_tl);
		int xdest_tl =  MAX(0, xdiff_tl);
		int  xsrc_br = omni->width  - MAX(0, xdiff_br);
		
		int  ysrc_tl = -MIN(0, ydiff_tl);
		int ydest_tl =  MAX(0, ydiff_tl);
		int  ysrc_br = omni->height - MAX(0, ydiff_br);
		
		int w = xsrc_br - xsrc_tl;
		int h = ysrc_br - ysrc_tl;
		
		if (w > 0 && h > 0) {
			if (zoomed_out) {
				const double scale = (double)viewport_width / omni->viewport_width;
				gdk_pixbuf_composite(omni->imgs[i], omni->view, xdest_tl / scale, ydest_tl / scale, w / scale, h / scale, -(looking_at.x - omni->coords[i].x) / scale, -(looking_at.y - omni->coords[i].y) / scale, 1 / scale, 1 / scale, GDK_INTERP_BILINEAR, 255);
			} else {
				gdk_pixbuf_copy_area(omni->imgs[i], xsrc_tl, ysrc_tl, w, h, omni->view, xdest_tl, ydest_tl);
			}
		}
	}
	
	GdkPixbuf *copy = gdk_pixbuf_copy(omni->view);
	gtk_picture_set_pixbuf(GTK_PICTURE(omni->pic), copy);
	
	if (omni->copy != NULL) g_object_unref(omni->copy);
	omni->copy = copy;
}

int viewport_index(Omni *omni) {
	int i; // index of image we are looking at
	for (i = 0; i < omni->n_imgs; ++i) {
		if (omni->looking_at.x < omni->coords[i].x && omni->coords[i].x + omni->width  < omni->looking_at.x + omni->viewport_width &&
			omni->looking_at.y < omni->coords[i].y && omni->coords[i].y + omni->height < omni->looking_at.y + omni->viewport_height) return i;
	}
	return -1;
}

gboolean key_pressed(GtkWidget *widget, GVariant *variant, gpointer user_data) {
	KeyboardShortcut *sc = (KeyboardShortcut*)user_data;
	printf("key pressed: %s [data=%p]\n", sc->keycombo, sc->data);
	
	Omni *omni = (Omni*)sc->data;
	char *keycombo = sc->keycombo;
	
	if (strcmp(keycombo, PRIMARY_STRING "q") == 0) {
		g_main_loop_quit(omni->mainloop);
		return TRUE;
	}
	
	char key = '\0';
	
	int stepsize = 1;
	
	if (keycombo[0] == '<') {
		if      (keycombo[1] == 'S') stepsize = 10;  // Shift
		else if (keycombo[1] == 'C' || keycombo[1] == 'M') stepsize = 100; // Control or Meta
		
		while (keycombo[0] != '>') keycombo++; // move to end of control sequence
		keycombo++;
	}
	
	if (strcmp(keycombo, "w") == 0) key = 'w';
	if (strcmp(keycombo, "a") == 0) key = 'a';
	if (strcmp(keycombo, "s") == 0) key = 's';
	if (strcmp(keycombo, "d") == 0) key = 'd';
	
	if (strcmp(keycombo, "i") == 0) key = 'i';
	if (strcmp(keycombo, "j") == 0) key = 'j';
	if (strcmp(keycombo, "k") == 0) key = 'k';
	if (strcmp(keycombo, "l") == 0) key = 'l';
	
	if (strcmp(keycombo, "f") == 0) key = 'f';
	
	if (strcmp(keycombo, "z") == 0) key = 'z';
	
	if (strcmp(keycombo, "o") == 0) key = 'o';
	if (strcmp(keycombo, "p") == 0) key = 'p';
	
	if (key == '\0') return TRUE;
	
	const int VIEWSTEP = 100;
	
	bool zoomed_out = false;
	
	const long AUTOSAVE_INTERVAL = 300; // 5 mins
	if (key != 'o' && key != 'p' && time(NULL) - omni->last_coord_dump > AUTOSAVE_INTERVAL) dump_coords(omni);
	
	switch (key) {
		case 'w':
			omni->looking_at.y -= VIEWSTEP / stepsize;
			break;
		case 'a':
			omni->looking_at.x -= VIEWSTEP / stepsize;
			break;
		case 's':
			omni->looking_at.y += VIEWSTEP / stepsize;
			break;
		case 'd':
			omni->looking_at.x += VIEWSTEP / stepsize;
			break;
		case 'i':
		case 'j':
		case 'k':
		case 'l':
		case 'f': {
			int i = viewport_index(omni);
			if (i < 0) break; // not looking at an actual image
			
			switch (key) {
				case 'f': {
					if (i >= omni->n_imgs - 1) return TRUE;
					omni->folds[i] = !omni->folds[i];
					break;
				}
				case 'i':
					omni->offsets[MAX(0, i - 1)].y -= stepsize;
					break;
				case 'j':
					omni->offsets[MAX(0, i - 1)].x -= stepsize;
					break;
				case 'k':
					omni->offsets[MAX(0, i - 1)].y += stepsize;
					break;
				case 'l':
					omni->offsets[MAX(0, i - 1)].x += stepsize;
					break;
			}
			
			regenerate_coords(omni);
			regenerate_bounding_box(omni);
			
			break;
		}
		case 'z':
			zoomed_out = true;
			break;
		case 'o':
			dump_coords(omni);
			break;
		case 'p': {
			dump_coords(omni);
			
			int xbounds[2] = {INT_MAX, INT_MIN};
			int ybounds[2] = {INT_MAX, INT_MIN};
			
			for (int i = 0; i < omni->n_imgs; ++i) {
				xbounds[0] = MIN(xbounds[0], omni->coords[i].x);
				xbounds[1] = MAX(xbounds[1], omni->coords[i].x + omni->width);
				
				ybounds[0] = MIN(ybounds[0], omni->coords[i].y);
				ybounds[1] = MAX(ybounds[1], omni->coords[i].y + omni->height);
			}
			
			Omni fakeomni = *omni;
			fakeomni.looking_at = (Coord){xbounds[0], ybounds[0]};
			fakeomni.viewport_width  = xbounds[1] - xbounds[0];
			fakeomni.viewport_height = ybounds[1] - ybounds[0];
			
			const int bps = gdk_pixbuf_get_bits_per_sample(fakeomni.imgs[0]);
			const int bpp = bps * gdk_pixbuf_get_n_channels(fakeomni.imgs[0]);
			
			const unsigned char *view_data = malloc((long)fakeomni.viewport_width * (long)fakeomni.viewport_height * bpp / 8);
			fakeomni.view = gdk_pixbuf_new_from_data(view_data, gdk_pixbuf_get_colorspace(fakeomni.imgs[0]), FALSE, bps, fakeomni.viewport_width, fakeomni.viewport_height, fakeomni.viewport_width * bpp / 8, NULL, NULL);
			
			populate_view(&fakeomni, false);
			omni->copy = fakeomni.copy;
			
			char fname[50];
			time_t t = time(NULL);
			strftime(fname, sizeof(fname), "mosaic_%F_%H-%M-%S.jpg", localtime(&t));
			
			GError *err = NULL;
			gdk_pixbuf_save(fakeomni.view, fname, "jpeg", &err, "quality", "100", NULL);
			if (err == NULL) {
				printf("Successfully saved image to file '%s'.\n", fname);
			} else {
				eprintf("Couldn't save file: %s\n", err->message);
			}
			
			g_object_unref(fakeomni.view);
			free((void*)view_data);
			
			return TRUE;
		}
	}
	
	omni->looking_at.x = CLAMP(omni->looking_at.x, omni->xbounds[0], omni->xbounds[1]);
	omni->looking_at.y = CLAMP(omni->looking_at.y, omni->ybounds[0], omni->ybounds[1]);
	
	populate_view(omni, zoomed_out);
		
	return TRUE;
}

void free_keyboard_shortcut(KeyboardShortcut *s) {
	free(s->keycombo);
	free(s);
}

void register_shortcut(GtkEventController *controller, char *s, void *ctx) {
	GtkShortcutTrigger *trigger = gtk_shortcut_trigger_parse_string(s);
	KeyboardShortcut *kbsc = malloc(sizeof(KeyboardShortcut));
	*kbsc = (KeyboardShortcut){strdup(s), ctx};
	GtkShortcutAction *action = gtk_callback_action_new(key_pressed, kbsc, (GDestroyNotify)free_keyboard_shortcut);
	GtkShortcut *shortcut = gtk_shortcut_new(trigger, action);
	gtk_shortcut_controller_add_shortcut(GTK_SHORTCUT_CONTROLLER(controller), shortcut);
}

int main(int argc, char *argv[]) {
	int status;
	
	gtk_init();
	
	if (argc != 2 && argc != 3) {
		eprintf("Expected 1 argument (directory of files to load) or 2 arguments (directory of files to load, save file to restore layout from), got %i.\n", argc - 1);
		return -1;
	}
	
	const char * const dir = argv[1];
	const char * const layout_file_path = argc > 2 ? argv[2] : NULL;
	
	printf("Loading files from directory '%s'\n", dir);
	
	struct dirent **dirlist = NULL;
	const size_t nfiles = scandir(dir, &dirlist, filter_only_files, alphasort);
	
	if (nfiles == -1) {
		switch (errno) {
			case ENOENT:
				eprintf("Path given does not exist\n");
				return -2;
			case ENOMEM:
				eprintf("Not enough memory to scan directory\n");
				return -3;
			case ENOTDIR:
				eprintf("Path given is not to a directory\n");
				return -4;
		}
	}
	
	GdkPixbuf **imgs = malloc(sizeof(GdkPixbuf*) * nfiles);
	
	for (int i = 0; i < nfiles; ++i) {
		const struct dirent * const d = dirlist[i];
		
		const size_t s = strlen(dir) + 1 + strlen(d->d_name); // len of path
		char *buf = malloc(s + 1); // +1 for null char
		strcpy(buf, dir);
		strcat(buf, "/");
		strcat(buf, d->d_name);
		
		if (d->d_type == DT_UNKNOWN) {
			struct stat info;
			status = stat(buf, &info);
			if (status != 0) {
				eprintf("An error occurred stat'ing the file '%s': %s [this file will be skipped]\n", buf, strerror(status));
				free((void*)d);
				free(buf);
				continue;
			}
			if (!S_ISREG(info.st_mode)) {
				free(buf);
				free((void*)d);
				continue; // if it's not a regular file, skip it
			}
		}
		
		printf("File: %s [type=%i]\n", d->d_name, d->d_type);
		
		GError *error = NULL;
		imgs[i] = gdk_pixbuf_new_from_file(buf, &error);
		if (error != NULL) {
			eprintf("%s\n", error->message);
			return -5;
		}
		
		free(buf);
		free((void*)d);
	}
	
	free(dirlist);
	
	GtkWidget *win = gtk_window_new();
	GtkWidget *pic = gtk_picture_new();
	
	const int width  = gdk_pixbuf_get_width(imgs[0]);
	const int height = gdk_pixbuf_get_height(imgs[0]);
	const int bps    = gdk_pixbuf_get_bits_per_sample(imgs[0]);
	const int bpp    = bps * gdk_pixbuf_get_n_channels(imgs[0]);
	
	int xpad = width/3, ypad = height/5;
	const int viewport_width = xpad+width+xpad;
	const int viewport_height = ypad+height+ypad;
	
	const unsigned char *view_data = malloc(viewport_width * viewport_height * bpp / 8);
	assert(view_data != NULL);
	GdkPixbuf *view = gdk_pixbuf_new_from_data(view_data, gdk_pixbuf_get_colorspace(imgs[0]), FALSE, bps, viewport_width, viewport_height, viewport_width * bpp / 8, NULL, NULL);
	
	GMainLoop *mainloop = g_main_loop_new(g_main_context_default(), FALSE);
	
	Coord *coords = malloc(sizeof(Coord) * nfiles);
	assert(coords != NULL);
	
	bool *folds = calloc(nfiles - 1, sizeof(bool));
	assert(folds != NULL);
	
	Coord *offsets = calloc(nfiles - 1, sizeof(Coord));
	assert(offsets != NULL);
	
	if (layout_file_path != NULL) {
		FILE *f = fopen(layout_file_path, "rb");
		
		if (f != NULL) {
			printf("Using the layout file '%s'...\n", layout_file_path);
			
			int num_imgs_in_layout = -1;
			fscanf(f, "%i\n", &num_imgs_in_layout);
			
			if (num_imgs_in_layout == nfiles) {
				int j = 0;
				for (int i = 0; i < num_imgs_in_layout - 2; ++i) {
					fscanf(f, "%i,", &j);
					folds[i] = j;
				}
				fscanf(f, "%i\n", &j);
				folds[num_imgs_in_layout - 2] = j;
				
				for (int i = 0; i < num_imgs_in_layout - 2; ++i) {
					fscanf(f, "%i,%i,", &offsets[i].x, &offsets[i].y);
				}
				fscanf(f, "%i,%i\n", &offsets[num_imgs_in_layout - 2].x, &offsets[num_imgs_in_layout - 2].y);
			} else {
				eprintf("This layout file does not contain the same number of files as were found in the directory given, so cannot be used. Are you sure you specified the right directory and file?\n");
			}
			
			fclose(f);
		} else {
			eprintf("An error occurred opening the layout file so it is not being used.\n");
		}
	}
	
	Omni omni = (Omni){mainloop, win, pic, imgs, coords, nfiles, (Coord){-xpad,-ypad}, view, viewport_width, width, viewport_height, height, {0,0}, {0,0}, folds, offsets, NULL, time(NULL)};
	regenerate_coords(&omni);
	regenerate_bounding_box(&omni);
	
	populate_view(&omni, false);
	
	gtk_window_set_child(GTK_WINDOW(win), pic);
	
	GtkEventController *controller = gtk_shortcut_controller_new();
	
	register_shortcut(controller, "w", &omni); // move around view in chunks
	register_shortcut(controller, "a", &omni);
	register_shortcut(controller, "s", &omni);
	register_shortcut(controller, "d", &omni);
	
	register_shortcut(controller, "<Shift>w", &omni);
	register_shortcut(controller, "<Shift>a", &omni);
	register_shortcut(controller, "<Shift>s", &omni);
	register_shortcut(controller, "<Shift>d", &omni);
	
	register_shortcut(controller, PRIMARY_STRING "w", &omni);
	register_shortcut(controller, PRIMARY_STRING "a", &omni);
	register_shortcut(controller, PRIMARY_STRING "s", &omni);
	register_shortcut(controller, PRIMARY_STRING "d", &omni);
	
	register_shortcut(controller, "i", &omni); // make pixel adjustments to img offsets
	register_shortcut(controller, "j", &omni);
	register_shortcut(controller, "k", &omni);
	register_shortcut(controller, "l", &omni);
	
	register_shortcut(controller, "<Shift>i", &omni);
	register_shortcut(controller, "<Shift>j", &omni);
	register_shortcut(controller, "<Shift>k", &omni);
	register_shortcut(controller, "<Shift>l", &omni);
	
	register_shortcut(controller, PRIMARY_STRING "i", &omni);
	register_shortcut(controller, PRIMARY_STRING "j", &omni);
	register_shortcut(controller, PRIMARY_STRING "k", &omni);
	register_shortcut(controller, PRIMARY_STRING "l", &omni);
	
	register_shortcut(controller, "f", &omni); // add a fold after
	
	register_shortcut(controller, "z", &omni); // zoom
	
	register_shortcut(controller, "o", &omni); // dump coords
	register_shortcut(controller, "p", &omni); // print
	
	register_shortcut(controller, PRIMARY_STRING "q", &omni); // quit
	gtk_widget_add_controller(win, controller);
	
	g_signal_connect_swapped(win, "close-request", (GCallback)g_main_loop_quit, mainloop);
	
	gtk_widget_show(win);
	
	g_main_loop_run(mainloop);
	
	return 0;	
}
