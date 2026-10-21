void set_window_title(const char *new_title);
void bounce_dock_icon();
void display_nsalert(const char *title, const char *message);
uint8_t *decode_image_rgba(const char *path, uint32_t *width, uint32_t *height);
char *copy_clipboard_text();
void set_clipboard_text(const char *text);
void add_menu_bar();
