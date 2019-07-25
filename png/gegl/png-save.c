/* This file is an image processing operation for GEGL
 *
 * GEGL is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * GEGL is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with GEGL; if not, see <https://www.gnu.org/licenses/>.
 *
 * Copyright 2006 Øyvind Kolås <pippin@gimp.org>
 *           2006 Dominik Ernst <dernst@gmx.de>
 */

#include "config.h"
#include <glib/gi18n-lib.h>


#ifdef GEGL_PROPERTIES

property_file_path (path, _("File"), "")
  description (_("Target path and filename, use '-' for stdout."))
property_int (compression, _("Compression"), 3)
  description (_("PNG compression level from 1 to 9"))
  value_range (1, 9)
property_int (bitdepth, _("Bitdepth"), 16)
  description (_("8 and 16 are the currently accepted values."))
  value_range (8, 16)

#else

#define GEGL_OP_SINK
#define GEGL_OP_NAME png_save
#define GEGL_OP_C_SOURCE png-save.c

#include <gegl-op.h>
#include <gegl-gio-private.h>
#include <png.h>

static void
write_fn(png_structp png_ptr, png_bytep buffer, png_size_t length)
{
  GError *err = NULL;
  GOutputStream *stream = G_OUTPUT_STREAM(png_get_io_ptr(png_ptr));
  gsize bytes_written = 0;
  g_assert(stream);

  g_output_stream_write_all(stream, buffer, length, &bytes_written, NULL, &err);
  if (err) {
    g_printerr("gegl:save-png %s: %s\n", __PRETTY_FUNCTION__, err->message);
  }
}

static void
flush_fn(png_structp png_ptr)
{
  GError *err = NULL;
  GOutputStream *stream = G_OUTPUT_STREAM(png_get_io_ptr(png_ptr));
  g_assert(stream);

  g_output_stream_flush(stream, NULL, &err);
  if (err) {
    g_printerr("gegl:save-png %s: %s\n", __PRETTY_FUNCTION__, err->message);
  }
}

static void
error_fn(png_structp png_ptr, png_const_charp msg)
{
  g_printerr("LIBPNG ERROR: %s", msg);
}

static gint
export_png (GeglOperation       *operation,
            GeglBuffer          *input,
            const GeglRectangle *result,
            png_structp          png,
            png_infop            info,
            gint                 compression,
            gint                 bit_depth)
{
  gint           i, src_x, src_y;
  png_uint_32    width, height;
  guchar        *pixels;
  png_color_16   white;
  int            png_color_type;
  gchar          format_string[16];
  const Babl    *babl = gegl_buffer_get_format (input);
  const Babl    *space = babl_format_get_space (babl);
  const Babl    *format;

  src_x = result->x;
  src_y = result->y;
  width = result->width;
  height = result->height;

  {

    if (bit_depth != 16)
      bit_depth = 8;

    if (babl_format_has_alpha (babl))
      if (babl_format_get_n_components (babl) != 2)
        {
          png_color_type = PNG_COLOR_TYPE_RGB_ALPHA;
          strcpy (format_string, "R'G'B'A ");
        }
      else
        {
          png_color_type = PNG_COLOR_TYPE_GRAY_ALPHA;
          strcpy (format_string, "Y'A ");
        }
    else
      if (babl_format_get_n_components (babl) != 1)
        {
          png_color_type = PNG_COLOR_TYPE_RGB;
          strcpy (format_string, "R'G'B' ");
        }
      else
        {
          png_color_type = PNG_COLOR_TYPE_GRAY;
          strcpy (format_string, "Y' ");
        }
  }

  if (bit_depth == 16)
    strcat (format_string, "u16");
  else
    strcat (format_string, "u8");

  if (setjmp (png_jmpbuf (png)))
    return -1;

  png_set_compression_level (png, compression);

  png_set_IHDR (png, info,
     width, height, bit_depth, png_color_type,
     PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_DEFAULT);

  if (
      (png_color_type == PNG_COLOR_TYPE_RGB || png_color_type == PNG_COLOR_TYPE_RGB_ALPHA))
    {
      if (space == NULL || space == babl_space ("sRGB"))
      {
        white.red = 0xff;
        white.blue = 0xff;
        white.green = 0xff;
        png_set_sRGB_gAMA_and_cHRM (png, info, PNG_sRGB_INTENT_RELATIVE);
      }
      else
      {

        double wp[2];
        double red[2];
        double green[2];
        double blue[2];
        const Babl *trc[3];
        int is_cmyk = babl_space_is_cmyk (space);

        babl_space_get (space, &wp[0], &wp[1],
                        &red[0], &red[1],
                        &green[0], &green[1],
                        &blue[0], &blue[1],
                        &trc[0], &trc[1], &trc[2]);
        png_set_cHRM (png, info, wp[0], wp[1], red[0], red[1], green[0], green[1], blue[0], blue[1]);
      /* XXX: should also set gamma based on trc! */
        if (trc[0] == babl_trc("sRGB") ||
            trc[0] == babl_trc("2.2") || is_cmyk)
        {
          png_set_gAMA (png, info, 2.2);
        }
        else if (trc[0] == babl_trc("linear"))
        {
          png_set_gAMA (png, info, 1.0);
        }
        else
        {
          png_set_gAMA (png, info, 2.2);
        }

        if (!is_cmyk) {
          int icc_len;
          const char *name = babl_get_name (space);
          const char *icc_profile;
          if (strlen (name) > 10) name = "GEGL";
          icc_profile = babl_space_get_icc (space, &icc_len);
          if (icc_profile)
          {
             png_set_iCCP (png, info,
                           name, 0, (void*)icc_profile, icc_len);
          }
        }
      }
    }
  else
    {
      white.gray = 0xff;
    }
  png_set_bKGD (png, info, &white);

  format = babl_format_with_space (format_string, space);


  if (space && space != babl_space("sRGB"))
  {
  }

  png_write_info (png, info);

#if BYTE_ORDER == LITTLE_ENDIAN
  if (bit_depth > 8)
    png_set_swap (png);
#endif
  pixels = g_malloc0 (width * babl_format_get_bytes_per_pixel (format));

  for (i=0; i< height; i++)
    {
      GeglRectangle rect;

      rect.x = src_x;
      rect.y = src_y+i;
      rect.width = width;
      rect.height = 1;

      gegl_buffer_get (input, &rect, 1.0, format, pixels, GEGL_AUTO_ROWSTRIDE, GEGL_ABYSS_NONE);

      png_write_rows (png, &pixels, 1);
    }

  png_write_end (png, info);

  g_free (pixels);

  return 0;
}

static gboolean
process (GeglOperation       *operation,
         GeglBuffer          *input,
         const GeglRectangle *result,
         gint                 level)
{
  GeglProperties *o = GEGL_PROPERTIES (operation);
  png_structp png = NULL;
  png_infop info = NULL;
  GOutputStream *stream = NULL;
  GFile *file = NULL;
  gboolean status = TRUE;
  GError *error = NULL;

  png = png_create_write_struct (PNG_LIBPNG_VER_STRING, NULL, error_fn, NULL);
  if (png != NULL)
    info = png_create_info_struct (png);
  if (png == NULL || info == NULL)
    {
      status = FALSE;
      g_warning ("failed to initialize PNG writer");
      goto cleanup;
    }

  stream = gegl_gio_open_output_stream (NULL, o->path, &file, &error);
  if (stream == NULL)
    {
      status = FALSE;
      g_warning ("%s", error->message);
      goto cleanup;
    }

  png_set_write_fn (png, stream, write_fn, flush_fn);

  if (export_png (operation, input, result, png, info, o->compression, o->bitdepth))
    {
      status = FALSE;
      g_warning("could not export PNG file");
      goto cleanup;
    }

cleanup:
  if (info != NULL)
    png_destroy_write_struct (&png, &info);
  else if (png != NULL)
    png_destroy_write_struct (&png, NULL);

  if (stream != NULL)
    g_clear_object(&stream);

  if (file != NULL)
    g_clear_object(&file);

  return status;
}

static void
gegl_op_class_init (GeglOpClass *klass)
{
  GeglOperationClass     *operation_class;
  GeglOperationSinkClass *sink_class;

  operation_class = GEGL_OPERATION_CLASS (klass);
  sink_class      = GEGL_OPERATION_SINK_CLASS (klass);

  sink_class->process    = process;
  sink_class->needs_full = TRUE;

  gegl_operation_class_set_keys (operation_class,
    "name",          "gegl:png-save",
    "title",       _("PNG File Saver"),
    "categories" ,   "output",
    "description", _("PNG image saver, using libpng"),
    NULL);

  gegl_operation_handlers_register_saver (
    ".png", "gegl:png-save");
}

#endif
