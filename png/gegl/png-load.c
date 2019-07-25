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
 *           2006 Kevin Cozens <kcozens@cvs.gnome.org>
 */

#include "config.h"
#include <glib/gi18n-lib.h>
#include <gegl-gio-private.h>

#ifdef GEGL_PROPERTIES

property_file_path (path, _("File"), "")
  description (_("Path of file to load."))
property_uri (uri, _("URI"), "")
  description (_("URI for file to load."))

#else

#define GEGL_OP_SOURCE
#define GEGL_OP_NAME png_load
#define GEGL_OP_C_SOURCE png-load.c

#include "gegl-op.h"
#include <png.h>


#define WARN_IF_ERROR(gerror) \
do { \
    if (gerror) { \
      g_warning("gegl:png-load %s", gerror->message); \
    } \
} while(0)

typedef enum {
  LOAD_PNG_TOO_SHORT,
  LOAD_PNG_WRONG_HEADER
} LoadPngErrors;

static GQuark error_quark(void)
{
  return g_quark_from_static_string ("gegl:load-png-error-quark");
}

static void
read_fn(png_structp png_ptr, png_bytep buffer, png_size_t length)
{
  GError *err = NULL;
  GInputStream *stream = G_INPUT_STREAM(png_get_io_ptr(png_ptr));
  gsize bytes_read = 0;
  g_assert(stream);

  g_input_stream_read_all(stream, buffer, length, &bytes_read, NULL, &err);
  if (err) {
    g_printerr("gegl:load-png %s: %s\n", __PRETTY_FUNCTION__, err->message);
  }
}

static void
error_fn(png_structp png_ptr, png_const_charp msg)
{
  g_printerr("LIBPNG ERROR: %s", msg);
}

static gboolean
check_valid_png_header(GInputStream *stream, GError **err)
{
  const size_t hdr_size=8;
  gssize hdr_read_size;
  unsigned char header[hdr_size];

  hdr_read_size = g_input_stream_read(G_INPUT_STREAM(stream), header, hdr_size, NULL, err);
  if (hdr_read_size == -1)
    {
      // err should be set by _read()
      return FALSE;
    }
  else if (hdr_read_size < hdr_size)
    {
      g_set_error(err, error_quark(), LOAD_PNG_TOO_SHORT,
                 "too short for a png file, only %lu bytes.",
                 (unsigned long)hdr_read_size);

      return FALSE;
    }
  else if (hdr_read_size > hdr_size)
    {
        const gboolean reached = TRUE;
        g_assert(!reached);
    }

  if (png_sig_cmp (header, 0, hdr_size))
    {
      g_set_error(err, error_quark(), LOAD_PNG_WRONG_HEADER, "wrong png header");
      return FALSE;
    }
  return TRUE;
}

static const Babl *
get_babl_format(int bit_depth, int color_type, const Babl *space)
{
   gchar format_string[32];

   if (color_type & PNG_COLOR_TYPE_RGB)
      {
        if (color_type & PNG_COLOR_MASK_ALPHA)
          strcpy (format_string, "R'G'B'A ");
        else
          strcpy (format_string, "R'G'B' ");
      }
    else if ((color_type & PNG_COLOR_TYPE_GRAY) == PNG_COLOR_TYPE_GRAY)
      {
        if (color_type & PNG_COLOR_MASK_ALPHA)
          strcpy (format_string, "Y'A ");
        else
          strcpy (format_string, "Y' ");
      }
    else if (color_type & PNG_COLOR_TYPE_PALETTE)
      {
        if (color_type & PNG_COLOR_MASK_ALPHA)
          strcpy (format_string, "R'G'B'A ");
        else
          strcpy (format_string, "R'G'B' ");
      }
    else
      {
        return NULL;
      }

    if (bit_depth <= 8)
      {
        strcat (format_string, "u8");
      }
    else if(bit_depth == 16)
      {
        strcat (format_string, "u16");
      }
    else
      {
        return NULL;
      }

    return babl_format_with_space (format_string, space);
}


static const Babl *
gegl_png_space (png_structp load_png_ptr,
                png_infop   load_info_ptr)
{
  char *name = NULL;
  unsigned char *profile = NULL;
  unsigned int   proflen = 0;
  int   compression_type;
  if (png_get_iCCP(load_png_ptr, load_info_ptr, &name, &compression_type, &profile, &proflen) ==
      PNG_INFO_iCCP)
    {
      const char *error = NULL;
      return babl_space_from_icc ((char*)profile, (int)proflen,
                                 BABL_ICC_INTENT_RELATIVE_COLORIMETRIC, &error);
    }

  if (png_get_valid (load_png_ptr, load_info_ptr, PNG_INFO_sRGB))
    {
      return NULL; // which in the end means the same as:
      return babl_space ("sRGB");
    }

  if (png_get_valid(load_png_ptr, load_info_ptr, PNG_INFO_gAMA))
    {
      /* sRGB as defaults */
      double wp[2]={0.3127, 0.3290};
      double red[2]={0.6400, 0.3300};
      double green[2]= {0.3000, 0.6000};
      double blue[2]={0.1500, 0.0600};
      double gamma;
      png_get_gAMA(load_png_ptr, load_info_ptr, &gamma);

      if (png_get_valid(load_png_ptr, load_info_ptr, PNG_INFO_cHRM))
      {
        png_get_cHRM(load_png_ptr, load_info_ptr,
                     &wp[0], &wp[1],
                     &red[0], &red[1],
                     &green[0], &green[1],
                     &blue[0], &blue[1]);
      }
      return babl_space_from_chromaticities (NULL, wp[0], wp[1],
                                             red[0], red[1],
                                             green[0], green[1],
                                             blue[0], blue[1],
                                             babl_trc_gamma (1.0/gamma),
                                             babl_trc_gamma (1.0/gamma),
                                             babl_trc_gamma (1.0/gamma),
                                             1);
    }

  return NULL;
}

static gint
gegl_buffer_import_png (GeglBuffer  *gegl_buffer,
                        GInputStream *stream,
                        gint         dest_x,
                        gint         dest_y,
                        gint        *ret_width,
                        gint        *ret_height,
                        const Babl  *format, // can be NULL
                        GError **err)
{
  gint           width;
  gint           bit_depth;
  gint           bpp;
  gint           number_of_passes=1;
  const Babl    *space = NULL;
  png_uint_32    w;
  png_uint_32    h;
  png_structp    load_png_ptr;
  png_infop      load_info_ptr;
  guchar        *pixels;
  /*png_bytep     *rows;*/


  unsigned   int i;
  png_bytep  *row_p = NULL;

  g_return_val_if_fail(stream, -1);

  if (!check_valid_png_header(stream, err))
    {
      return -1;
    }

  load_png_ptr = png_create_read_struct (PNG_LIBPNG_VER_STRING, NULL, error_fn, NULL);

  if (!load_png_ptr)
    {
      return -1;
    }

  load_info_ptr = png_create_info_struct (load_png_ptr);
  if (!load_info_ptr)
    {
      png_destroy_read_struct (&load_png_ptr, &load_info_ptr, NULL);
      return -1;
    }
  png_set_benign_errors (load_png_ptr, TRUE);
  png_set_option (load_png_ptr, PNG_SKIP_sRGB_CHECK_PROFILE, PNG_OPTION_ON);

  if (setjmp (png_jmpbuf (load_png_ptr)))
    {
      png_destroy_read_struct (&load_png_ptr, &load_info_ptr, NULL);
      g_free (row_p);
      return -1;
    }

  png_set_read_fn(load_png_ptr, stream, read_fn);

  png_set_sig_bytes (load_png_ptr, 8); // we already read header
  png_read_info (load_png_ptr, load_info_ptr);
  {
    int color_type;
    int interlace_type;

    png_get_IHDR (load_png_ptr,
                  load_info_ptr,
                  &w, &h,
                  &bit_depth,
                  &color_type,
                  &interlace_type,
                  NULL, NULL);
    width = w;
    if (ret_width)
      *ret_width = w;
    if (ret_height)
      *ret_height = h;

    if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8)
      {
        png_set_expand (load_png_ptr);
        bit_depth = 8;
      }

    if (png_get_valid (load_png_ptr, load_info_ptr, PNG_INFO_tRNS))
      {
        png_set_tRNS_to_alpha (load_png_ptr);
        color_type |= PNG_COLOR_MASK_ALPHA;
      }

    switch (color_type)
      {
        case PNG_COLOR_TYPE_GRAY:
          bpp = 1;
          break;
        case PNG_COLOR_TYPE_GRAY_ALPHA:
          bpp = 2;
          break;
        case PNG_COLOR_TYPE_RGB:
          bpp = 3;
          break;
        case PNG_COLOR_TYPE_RGB_ALPHA:
          bpp = 4;
          break;
        case (PNG_COLOR_TYPE_PALETTE | PNG_COLOR_MASK_ALPHA):
          bpp = 4;
          break;
        case PNG_COLOR_TYPE_PALETTE:
          bpp = 3;
          break;
        default:
          g_warning ("color type mismatch");
          png_destroy_read_struct (&load_png_ptr, &load_info_ptr, NULL);
          return -1;
      }

    space = gegl_png_space (load_png_ptr, load_info_ptr);

    if (color_type == PNG_COLOR_TYPE_PALETTE)
      png_set_palette_to_rgb (load_png_ptr);

    if (bit_depth == 16)
      bpp = bpp << 1;

    if (!format)
      format = get_babl_format(bit_depth, color_type, space);

#if BYTE_ORDER == LITTLE_ENDIAN
    if (bit_depth == 16)
      png_set_swap (load_png_ptr);
#endif

    if (interlace_type == PNG_INTERLACE_ADAM7)
      number_of_passes = png_set_interlace_handling (load_png_ptr);

    if (!space)
    {
    if (png_get_valid (load_png_ptr, load_info_ptr, PNG_INFO_gAMA))
      {
        gdouble gamma;
        png_get_gAMA (load_png_ptr, load_info_ptr, &gamma);
        png_set_gamma (load_png_ptr, 2.2, gamma);
      }
    else
      {
        png_set_gamma (load_png_ptr, 2.2, 0.45455);
      }
    }

    png_read_update_info (load_png_ptr, load_info_ptr);
  }

  pixels = g_malloc0 (width*bpp);

  {
    gint           pass;
    GeglRectangle  rect;

    for (pass=0; pass<number_of_passes; pass++)
      {
        for(i=0; i<h; i++)
          {
            gegl_rectangle_set (&rect, 0, i, width, 1);

            if (pass != 0)
              gegl_buffer_get (gegl_buffer, &rect, 1.0, format, pixels, GEGL_AUTO_ROWSTRIDE, GEGL_ABYSS_NONE);

            png_read_rows (load_png_ptr, &pixels, NULL, 1);
            gegl_buffer_set (gegl_buffer, &rect, 0, format, pixels,
                             GEGL_AUTO_ROWSTRIDE);
          }
      }
  }


  png_read_end (load_png_ptr, NULL);
  png_destroy_read_struct (&load_png_ptr, &load_info_ptr, NULL);

  g_free (pixels);

  return 0;
}



static gint query_png (GInputStream *stream,
                       gint        *width,
                       gint        *height,
                       const Babl  **format,
                       GError **err)
{
  png_uint_32   w;
  png_uint_32   h;
  png_structp   load_png_ptr;
  png_infop     load_info_ptr;
  const Babl *  space = NULL; // null means sRGB

  png_bytep  *row_p = NULL;
  g_return_val_if_fail(stream, -1);

  if (!check_valid_png_header(stream, err))
    {
      return -1;
    }

  load_png_ptr = png_create_read_struct (PNG_LIBPNG_VER_STRING, NULL, error_fn, NULL);
  if (!load_png_ptr)
    {
      return -1;
    }

  load_info_ptr = png_create_info_struct (load_png_ptr);
  if (!load_info_ptr)
    {
      png_destroy_read_struct (&load_png_ptr, &load_info_ptr, NULL);
      return -1;
    }
  png_set_benign_errors (load_png_ptr, TRUE);
  png_set_option (load_png_ptr, PNG_SKIP_sRGB_CHECK_PROFILE, PNG_OPTION_ON);


  if (setjmp (png_jmpbuf (load_png_ptr)))
    {
     png_destroy_read_struct (&load_png_ptr, &load_info_ptr, NULL);
     g_free (row_p);
      return -1;
    }

  png_set_read_fn(load_png_ptr, stream, read_fn);
  png_set_sig_bytes (load_png_ptr, 8); // we already read header
  png_read_info (load_png_ptr, load_info_ptr);
  {
    int bit_depth;
    int color_type;
    const Babl *f;

    png_get_IHDR (load_png_ptr,
                  load_info_ptr,
                  &w, &h,
                  &bit_depth,
                  &color_type,
                  NULL, NULL, NULL);
    *width = w;
    *height = h;

    if (png_get_valid (load_png_ptr, load_info_ptr, PNG_INFO_tRNS))
      color_type |= PNG_COLOR_MASK_ALPHA;

    space = gegl_png_space (load_png_ptr, load_info_ptr);

    f = get_babl_format(bit_depth, color_type, space);
    if (!f)
      {
        png_destroy_read_struct (&load_png_ptr, &load_info_ptr, NULL);
        return -1;
      }
    *format = f;

  }
  png_destroy_read_struct (&load_png_ptr, &load_info_ptr, NULL);
  return 0;
}

static GeglRectangle
get_bounding_box (GeglOperation *operation)
{
  GeglProperties   *o = GEGL_PROPERTIES (operation);
  GeglRectangle result = {0,0,0,0};
  gint          width, height;
  gint          status;
  const Babl *  format;
  GError *err = NULL;
  GFile *infile = NULL;

  GInputStream *stream = gegl_gio_open_input_stream(o->uri, o->path, &infile, &err);
  WARN_IF_ERROR(err);
  if (!stream) return result;
  status = query_png(stream, &width, &height, &format, &err);
  WARN_IF_ERROR(err);
  g_input_stream_close(stream, NULL, NULL);

  if (status)
    {
      width = 0;
      height = 0;
    }

  gegl_operation_set_format (operation, "output", format);
  result.width  = width;
  result.height  = height;

  g_clear_object(&infile);
  g_object_unref(stream);
  return result;
}

static gboolean
process (GeglOperation       *operation,
         GeglBuffer          *output,
         const GeglRectangle *result,
         gint                 level)
{
  GeglProperties *o = GEGL_PROPERTIES (operation);
  gint        problem;
  gint        width, height;
  Babl        *format = NULL;
  GError *err = NULL;
  GFile *infile = NULL;
  GInputStream *stream = gegl_gio_open_input_stream(o->uri, o->path, &infile, &err);
  WARN_IF_ERROR(err);
  problem = gegl_buffer_import_png (output, stream, 0, 0,
                                    &width, &height, format, &err);
  WARN_IF_ERROR(err);
  g_input_stream_close(stream, NULL, NULL);

  if (problem)
    {
      g_object_unref(infile);
      g_object_unref(stream);
      g_warning ("%s failed to open file %s for reading.",
                 G_OBJECT_TYPE_NAME (operation), o->path);
      return FALSE;
    }
  g_clear_object(&infile);
  g_object_unref(stream);
  return TRUE;
}

static GeglRectangle
get_cached_region (GeglOperation       *operation,
                   const GeglRectangle *roi)
{
  return get_bounding_box (operation);
}

static void
gegl_op_class_init (GeglOpClass *klass)
{
  GeglOperationClass       *operation_class;
  GeglOperationSourceClass *source_class;

  operation_class = GEGL_OPERATION_CLASS (klass);
  source_class    = GEGL_OPERATION_SOURCE_CLASS (klass);

  source_class->process = process;
  operation_class->get_bounding_box = get_bounding_box;
  operation_class->get_cached_region = get_cached_region;

  gegl_operation_class_set_keys (operation_class,
    "name",         "gegl:png-load",
    "title",        _("PNG File Loader"),
    "categories",   "hidden",
    "description",  _("PNG image loader."),
    NULL);

/*  static gboolean done=FALSE;
    if (done)
      return; */
  gegl_operation_handlers_register_loader (
    "image/png", "gegl:png-load");
  gegl_operation_handlers_register_loader (
    ".png", "gegl:png-load");
/*  done = TRUE; */
}

#endif
