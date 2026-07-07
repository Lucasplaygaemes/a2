#include "base64.h"
#include <stdlib.h>
#include <stdint.h>

static const char base64_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

char *base64_encode(const unsigned char *input, size_t input_len, size_t *out_len) {
    if (input == NULL || input_len == 0) {
        if (out_len) *out_len = 0;
        return NULL;
    }

    size_t encoded_len = 4 * ((input_len + 2) / 3);
    char *encoded = (char *)malloc(encoded_len + 1);
    if (!encoded) return NULL;

    size_t i, j;
    for (i = 0, j = 0; i < input_len;) {
        uint32_t octet_a = i < input_len ? (unsigned char)input[i++] : 0;
        uint32_t octet_b = i < input_len ? (unsigned char)input[i++] : 0;
        uint32_t octet_c = i < input_len ? (unsigned char)input[i++] : 0;

        uint32_t triple = (octet_a << 0x10) + (octet_b << 0x08) + octet_c;

        encoded[j++] = base64_chars[(triple >> 3 * 6) & 0x3F];
        encoded[j++] = base64_chars[(triple >> 2 * 6) & 0x3F];
        encoded[j++] = base64_chars[(triple >> 1 * 6) & 0x3F];
        encoded[j++] = base64_chars[(triple >> 0 * 6) & 0x3F];
    }

    for (i = 0; i < (3 - input_len % 3) % 3; i++) {
        encoded[encoded_len - 1 - i] = '=';
    }
    encoded[encoded_len] = '\0';

    if (out_len) *out_len = encoded_len;
    return encoded;
}

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include <stdio.h>
#include <string.h>

void render_kitty_image(EditorBuffer *buf, int win_y, int win_x, int max_cols, int max_rows) {
    if (!buf || buf->filename[0] == '\0') return;

    int c = max_cols > 0 ? max_cols : 1;
    int r = max_rows > 0 ? max_rows : 1;

    // Se precisarmos mover/redimensionar a imagem, a maneira mais segura em terminais antigos
    // é deletar a imagem inteira da memória (d=i) e re-transmitir do zero.
    if (buf->image_is_visible && (buf->image_last_cols != c || buf->image_last_rows != r || buf->image_last_y != win_y || buf->image_last_x != win_x)) {
        printf("\033_Ga=d,d=i,i=%u;\033\\", buf->kitty_image_id);
        fflush(stdout);
        buf->image_is_visible = false;
        buf->image_transmitted = false;
        buf->kitty_image_id = (uint32_t)(rand() % 1000000 + 1); // get a new id just in case
    }

    if (!buf->image_transmitted) {
        int width, height, channels;
        unsigned char *img = stbi_load(buf->filename, &width, &height, &channels, 4);
        if (!img) return;

        size_t img_size = width * height * 4;
        size_t b64_len;
        char *b64_data = base64_encode(img, img_size, &b64_len);
        stbi_image_free(img);

        if (!b64_data) return;

        size_t chunk_size = 4096;
        size_t offset = 0;
        
        while (offset < b64_len) {
            size_t size = chunk_size;
            if (offset + size > b64_len) size = b64_len - offset;
            int m = (offset + size < b64_len) ? 1 : 0;
            
            if (offset == 0) {
                // a=t: transmit only
                printf("\033_Ga=t,f=32,s=%d,v=%d,i=%u,m=%d;", width, height, buf->kitty_image_id, m);
            } else {
                printf("\033_Gm=%d;", m);
            }
            fwrite(b64_data + offset, 1, size, stdout);
            printf("\033\\");
            offset += size;
        }
        free(b64_data);
        buf->image_transmitted = true;
    }

    if (!buf->image_is_visible) {
        // Place image
        printf("\033[%d;%dH", win_y + 1, win_x + 1);
        printf("\033_Ga=p,i=%u,p=1,c=%d,r=%d,z=1;\033\\", buf->kitty_image_id, c, r);
        fflush(stdout);
        buf->image_is_visible = true;
        buf->image_last_cols = c;
        buf->image_last_rows = r;
        buf->image_last_y = win_y;
        buf->image_last_x = win_x;
    }
}

void hide_kitty_image(EditorBuffer *buf) {
    if (!buf || buf->kitty_image_id == 0) return;
    if (buf->image_is_visible) {
        printf("\033_Ga=d,d=i,i=%u;\033\\", buf->kitty_image_id);
        fflush(stdout);
        buf->image_is_visible = false;
        buf->image_transmitted = false;
    }
}

void delete_kitty_image(uint32_t id) {
    if (id == 0) return;
    printf("\033_Ga=d,d=i,i=%u;\033\\", id);
    fflush(stdout);
}

void render_kitty_hover(EditorImageHover *hover, int win_y, int win_x, int max_cols, int max_rows) {
    if (!hover || hover->image_path[0] == '\0') return;

    int c = max_cols > 0 ? max_cols : 1;
    int r = max_rows > 0 ? max_rows : 1;

    if (hover->image_is_visible && (hover->image_last_cols != c || hover->image_last_rows != r || hover->image_last_y != win_y || hover->image_last_x != win_x)) {
        printf("\033_Ga=d,d=i,i=%u;\033\\", hover->kitty_image_id);
        fflush(stdout);
        hover->image_is_visible = false;
        hover->kitty_image_id = 0; // force retransmission
    }

    if (hover->kitty_image_id == 0) {
        hover->kitty_image_id = (uint32_t)(rand() % 1000000 + 1000000); // offset to avoid collision
        int width, height, channels;
        unsigned char *img = stbi_load(hover->image_path, &width, &height, &channels, 4);
        if (!img) { hover->image_path[0] = '\0'; return; } // invalid image

        size_t img_size = width * height * 4;
        size_t b64_len;
        char *b64_data = base64_encode(img, img_size, &b64_len);
        stbi_image_free(img);
        if (!b64_data) return;

        size_t chunk_size = 4096;
        size_t offset = 0;
        
        while (offset < b64_len) {
            size_t size = chunk_size;
            if (offset + size > b64_len) size = b64_len - offset;
            int m = (offset + size < b64_len) ? 1 : 0;
            if (offset == 0) {
                printf("\033_Ga=t,f=32,s=%d,v=%d,i=%u,m=%d;", width, height, hover->kitty_image_id, m);
            } else {
                printf("\033_Gm=%d;", m);
            }
            fwrite(b64_data + offset, 1, size, stdout);
            printf("\033\\");
            offset += size;
        }
        free(b64_data);
    }

    if (!hover->image_is_visible) {
        printf("\033[%d;%dH", win_y + 1, win_x + 1);
        printf("\033_Ga=p,i=%u,p=1,c=%d,r=%d,z=3;\033\\", hover->kitty_image_id, c, r);
        fflush(stdout);
        hover->image_is_visible = true;
        hover->image_last_cols = c;
        hover->image_last_rows = r;
        hover->image_last_y = win_y;
        hover->image_last_x = win_x;
    }
}

void hide_kitty_hover(EditorImageHover *hover) {
    if (!hover || hover->kitty_image_id == 0) return;
    if (hover->image_is_visible) {
        printf("\033_Ga=d,d=i,i=%u;\033\\", hover->kitty_image_id);
        fflush(stdout);
        hover->image_is_visible = false;
        hover->kitty_image_id = 0;
    }
}
