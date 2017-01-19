/*
 * Copyright (c) 2014, Qualcomm Technologies, Inc. All Rights Reserved.
 * Qualcomm Technologies Proprietary and Confidential.
 */
#include <ft2build.h>
#include <freetype.h>
#include <utils/Unicode.h>
#include "mmi_utils.h"
#include "mmi_graphics.h"

static struct {
    unsigned width;
    unsigned height;
    unsigned cwidth;
    unsigned cheight;
} font_ft = {
.width = 25,.height = 25,.cwidth = 25,.cheight = 25};

static FT_Library pft_lib = NULL;
static FT_Face pft_face = NULL;
static GRFont *gr_ft_font = NULL;

/*
*   freetype initialize
*/
static int ft_init(char *ft_ttf_path) {
    FT_Error error = NULL;

    if(ft_ttf_path == NULL) {
        return -1;
    }

    error = FT_Init_FreeType(&pft_lib);
    if(error) {
        ALOGE("freetype init lib faill");
        pft_lib = NULL;
        return -1;
    }
    /* create font face from font file */
    error = FT_New_Face(pft_lib, ft_ttf_path, 0, &pft_face);
    if(error) {
        error = FT_New_Face(pft_lib, DEFAULT_FONT_PATH, 0, &pft_face);
        if(error) {
            ALOGE("load font fail");
            FT_Done_FreeType(pft_lib);
            pft_lib = NULL;
            return -1;
        }
    }
    return 0;
}

/*
*   font initialize
*/
static int ft_font_init(void) {
    gr_ft_font = (GRFont *) calloc(sizeof(*gr_ft_font), 1);
    if(gr_ft_font == NULL) {
        ALOGE("memory low, alloc fail");
        return -1;
    }
    // fall back to the compiled-in font.
    gr_ft_font->texture = (GRSurface *) malloc(sizeof(*gr_ft_font->texture));
    if(gr_ft_font->texture == NULL) {
        ALOGE("memory low, alloc fail");
        free(gr_ft_font);
        return -1;
    }

    gr_ft_font->texture->width = font_ft.width;
    gr_ft_font->texture->height = font_ft.height;
    gr_ft_font->texture->row_bytes = font_ft.width;
    gr_ft_font->texture->pixel_bytes = 1;

    unsigned char *bits = (unsigned char *) malloc(font_ft.width * font_ft.height * 3);

    if(bits == NULL) {
        ALOGE("memory low, alloc fail");
        free(gr_ft_font->texture);
        free(gr_ft_font);
        return -1;
    }

    gr_ft_font->texture->data = (GGLubyte *) bits;
    gr_ft_font->cwidth = font_ft.cwidth;
    gr_ft_font->cheight = font_ft.cheight;

    return 0;
}

static FT_Error set_ft_font(unsigned short disp_char, GRFont * font) {
    FT_Error error = NULL;

    FT_Set_Pixel_Sizes(pft_face, font_ft.cwidth, font_ft.cheight);
    FT_UInt glyph_index = FT_Get_Char_Index(pft_face, disp_char);

    FT_Load_Glyph(pft_face, glyph_index, FT_LOAD_DEFAULT);
    error = FT_Render_Glyph(pft_face->glyph, FT_RENDER_MODE_MONO);
    if(!error) {
        FT_Bitmap bitmap = pft_face->glyph->bitmap;
        unsigned char *bits2 = font->texture->data;
        int i, j;

        for(i = 0; i < bitmap.rows; ++i) {
            for(j = 0; j < bitmap.width; ++j) {
                if(bitmap.buffer[i * bitmap.pitch + j / 8] & (0xC0 >> (j % 8))) {
                    *bits2 = 255;
                } else {
                    *bits2 = 0;
                }
                bits2++;
            }
        }

        font->texture->width = bitmap.width;
        font->texture->height = bitmap.rows;
        font->cwidth = bitmap.width;
        font->cheight = bitmap.rows;
        font->texture->row_bytes = bitmap.pitch;
    }
    return error;
}

int mmi_gr_init(void) {
    int ret = 0;
    ret = ft_init(DEFAULT_FONT_PATH);
    if(ret) {
        ALOGI("%s: fail to load font", __FUNCTION__);
        return -1;
    }

    ret = ft_font_init();
    ALOGI("%s: sucess", __FUNCTION__);
    return ret;
}

void mmi_gr_text(int x, int y, const char *s, int bold) {
    FT_Error error = NULL;

    unsigned short unicode[DEFAULT_UNICODE_STR_LEN] = { 0 };
    int chnum = 0;
    int unicode_len = 0;
    int srclen = strlen(s);

    GRFont *font = gr_ft_font;

    if(!font->texture)
        return;

    bold = bold && (font->texture->height != font->cheight);

    /*convert utf8 to unicode */
    if(srclen > DEFAULT_UNICODE_STR_LEN)
        srclen = DEFAULT_UNICODE_STR_LEN;


    unicode_len = utf8_to_utf16_length((unsigned char *) s, srclen);
    utf8_to_utf16((unsigned char *) s, srclen, unicode);

    while(chnum < unicode_len) {
        error = set_ft_font(unicode[chnum], font);
        if(!error) {

            gr_text_blend(x, y, font);
            x += pft_face->glyph->advance.x >> 6;
        }
        chnum++;
    }

}

void mmi_set_font_size(int w, int h) {
    font_ft.cwidth = w;
    font_ft.cheight = h;
}

void mmi_gr_deinit(void) {
    if(gr_ft_font != NULL && gr_ft_font->texture != NULL && gr_ft_font->texture->data != NULL) {
        free(gr_ft_font->texture->data);
        gr_ft_font->texture->data = NULL;
    }

    if(gr_ft_font != NULL && gr_ft_font->texture != NULL) {
        free(gr_ft_font->texture);
        gr_ft_font->texture = NULL;
    }

    if(gr_ft_font != NULL) {
        free(gr_ft_font);
        gr_ft_font = NULL;
    }

    if(pft_face != NULL) {
        FT_Done_Face(pft_face);
        pft_face = NULL;
    }

    if(pft_lib != NULL) {
        FT_Done_FreeType(pft_lib);
        pft_lib = NULL;
    }
}
