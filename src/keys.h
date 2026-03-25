#pragma once

typedef struct Keys Keys;

/*
 * opens /dev/uinput and registers a virtual keyboard device
 * returns NULL on failure (check stderr for hint)
 * requires membership in the 'input' group or root, or a udev rule, see README
 */
Keys *keys_init(void);
void  keys_free(Keys *k);

// send a key or a key combo
int keys_send_combo(Keys *k, const char *combo);

// send a media key
int keys_send_media(Keys *k, const char *name);

// release every key currently tracked as held (failsafe on shutdown)
void keys_release_all(Keys *k);

// type a string character by character (US QWERTY layout, 5 ms between keys)
int keys_send_string(Keys *k, const char *str);
