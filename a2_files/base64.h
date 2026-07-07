#ifndef BASE64_H
#define BASE64_H

#include <stddef.h>

/**
 * Encodes binary data to a base64 string.
 * @param input The binary data to encode
 * @param input_len The length of the binary data
 * @param out_len Pointer to store the length of the resulting base64 string (optional)
 * @return Null-terminated base64 string (must be freed by caller), or NULL on failure
 */
char *base64_encode(const unsigned char *input, size_t input_len, size_t *out_len);

#include "defs.h"

/**
 * Renders an image using the Kitty Graphics Protocol.
 * @param buf The editor buffer containing the image path and state
 * @param max_cols The maximum number of columns the image should occupy
 * @param max_rows The maximum number of rows the image should occupy
 */
void render_kitty_image(EditorBuffer *buf, int win_y, int win_x, int max_cols, int max_rows);

/**
 * Hides a placed Kitty image
 */
void hide_kitty_image(EditorBuffer *buf);

/**
 * Deletes a Kitty image from terminal memory
 */
void delete_kitty_image(uint32_t id);

/**
 * Renders a kitty image hover popup
 */
void render_kitty_hover(EditorImageHover *hover, int win_y, int win_x, int max_cols, int max_rows);

/**
 * Hides a kitty image hover popup
 */
void hide_kitty_hover(EditorImageHover *hover);

#endif // BASE64_H
