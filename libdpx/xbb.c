/* This is extractbb, a bounding box extraction program. 
    Copyright (C) 2008-2014 by Jin-Hwan Cho and Matthias Franz
    and the dvipdfmx project team.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <time.h>
#include <string.h>

#include "numbers.h"
#include "system.h"
#include "mem.h"
#include "error.h"
#include "mfileio.h"
#include "pdfobj.h"
#include "pdfdoc.h"
#include "pdfparse.h"

#include "jpegimage.h"
#include "pngimage.h"

#include "dvipdfmx.h"
#include "pdflimits.h"

#define XBB_PROGRAM "extractbb"

static void show_version(void)
{
  fprintf (stdout, "\nThis is " XBB_PROGRAM " Version " VERSION "\n");
  fprintf (stdout, "A bounding box extraction utility from PDF, PNG, and JPEG.\n");
  fprintf (stdout, "\nCopyright (C) 2008-2014 by Jin-Hwan Cho and Matthias Franz\n");
  fprintf (stdout, "\nThis is free software; you can redistribute it and/or modify\n");
  fprintf (stdout, "it under the terms of the GNU General Public License as published by\n");
  fprintf (stdout, "the Free Software Foundation; either version 2 of the License, or\n");
  fprintf (stdout, "(at your option) any later version.\n");
}

static void show_usage(void)
{
  fprintf (stdout, "\nUsage: " XBB_PROGRAM " [-q|-v] [-O] [-m|-x] file...\n");
  fprintf (stdout, "       " XBB_PROGRAM " --help|--version\n");
  fprintf (stdout, "\nOptions:\n");
  fprintf (stdout, "  -h | --help\tShow this help message and exit\n");
  fprintf (stdout, "  --version\tOutput version information and exit\n");
  fprintf (stdout, "  -q\t\tBe quiet\n");
  fprintf (stdout, "  -v\t\tBe verbose\n");
  fprintf (stdout, "  -O\t\tWrite output to stdout\n");
  fprintf (stdout, "  -m\t\tOutput .bb  file used in DVIPDFM%s\n", compat_mode ? " (default)" : "");
  fprintf (stdout, "  -x\t\tOutput .xbb file used in DVIPDFMx%s\n", compat_mode ? "" : " (default)");
}

static void usage(void)
{
  fprintf(stdout, "\nTry \"" XBB_PROGRAM " --help\" for more information.\n");
  exit(1);
}

static char verbose = 0;

static void do_time(FILE *file)
{
  time_t current_time;
  struct tm *bd_time;

  time(&current_time);
  bd_time = localtime(&current_time);
  fprintf(file, "%%%%CreationDate: %s\n", asctime(bd_time));
}

const char *extensions[] = {
  ".ai", ".AI", ".jpeg", ".JPEG", ".jpg", ".JPG", ".pdf", ".PDF", ".png", ".PNG"
};

static int xbb_to_file = 1;

static char *make_xbb_filename(const char *name)
{
  int i;
  char *result;

  for (i = 0; i < sizeof(extensions) / sizeof(extensions[0]); i++) {
    if (strlen(extensions[i]) < strlen(name) &&
	strncmp(name+strlen(name)-strlen(extensions[i]), extensions[i], strlen(extensions[i])) == 0)
      break;
  }
  if (i == sizeof(extensions) / sizeof(extensions[0])) {
    WARN("%s: Filename does not end in a recognizable extension.\n", name);
    result = NEW(strlen(name)+5, char);  /* 5 = ".xbb" + trailing 0 */
    strcpy(result, name);
  } else { /* Remove extension */
    result = NEW(strlen(name)-strlen(extensions[i])+5, char);  /* 5 = ".xbb" + trailing 0 */
    strncpy(result, name, strlen(name)-strlen(extensions[i]));
    result[strlen(name)-strlen(extensions[i])] = 0;
  }
  strcat(result, (compat_mode ? ".bb" : ".xbb"));
  return result;
}

static void write_xbb(char *fname,
		      double bbllx_f, double bblly_f,
		      double bburx_f, double bbury_f,
		      int pdf_version, long pagecount) 
{
  char *outname = NULL;
  FILE *fp = NULL;

  long bbllx = ROUND(bbllx_f, 1.0), bblly = ROUND(bblly_f, 1.0);
  long bburx = ROUND(bburx_f, 1.0), bbury = ROUND(bbury_f, 1.0);

  if (xbb_to_file) {
    outname = make_xbb_filename(fname);
    if (!kpse_out_name_ok(outname) || !(fp = MFOPEN(outname, FOPEN_W_MODE))) {
      ERROR("Unable to open output file: %s\n", outname);
    }
  } else {
    fp = stdout;
#ifdef WIN32
    setmode(fileno(fp), _O_BINARY);
#endif
  }

  if (verbose) {
    MESG("Writing to %s: ", xbb_to_file ? outname : "stdout");
    MESG("Bounding box: %d %d %d %d\n", bbllx, bblly, bburx, bbury);
  }

  fprintf(fp, "%%%%Title: %s\n", fname);
  fprintf(fp, "%%%%Creator: %s %s\n", XBB_PROGRAM, VERSION);
  fprintf(fp, "%%%%BoundingBox: %ld %ld %ld %ld\n", bbllx, bblly, bburx, bbury);

  if (!compat_mode) {
    /* Note:
     * According to Adobe Technical Note #5644, the arguments to
     * "%%HiResBoundingBox:" must be of type real. And according
     * to the PostScript Language Reference, a real number must
     * be written with a decimal point (or an exponent). Hence
     * it seems illegal to replace "0.0" by "0".
     */
    fprintf(fp, "%%%%HiResBoundingBox: %f %f %f %f\n",
	    bbllx_f, bblly_f, bburx_f, bbury_f);
    if (pdf_version >= 0) {
      fprintf(fp, "%%%%PDFVersion: 1.%d\n", pdf_version);
      fprintf(fp, "%%%%Pages: %ld\n", pagecount);
    }
  }

  do_time(fp);

  if (xbb_to_file) {
    RELEASE(outname);
    MFCLOSE(fp);
  }
}

static void do_jpeg (FILE *fp, char *filename)
{
  long   width, height;
  double xdensity, ydensity;

  if (jpeg_get_bbox(fp, &width, &height, &xdensity, &ydensity) < 0) {
    WARN("%s does not look like a JPEG file...\n", filename);
    return;
  }

  write_xbb(filename, 0, 0, xdensity*width, ydensity*height, -1, -1);
  return;
}

#ifdef HAVE_LIBPNG
static void do_png (FILE *fp, char *filename)
{
  long   width, height;
  double xdensity, ydensity;

  if (png_get_bbox(fp, &width, &height, &xdensity, &ydensity) < 0) {
    WARN("%s does not look like a PNG file...\n", filename);
    return;
  }

  write_xbb(filename, 0, 0, xdensity*width, ydensity*height, -1, -1);
  return;
}
#endif /* HAVE_LIBPNG */

static void do_pdf (FILE *fp, char *filename)
{
  pdf_obj *page;
  pdf_file *pf;
  long page_no = 1;
  long count;
  pdf_rect bbox;

  pf = pdf_open(filename, fp);
  if (!pf) {
    WARN("%s does not look like a PDF file...\n", filename);
    return;
  }

  page = pdf_doc_get_page(pf, page_no, &count, &bbox, NULL);

  pdf_close(pf);

  if (!page)
    return;

  pdf_release_obj(page);
  write_xbb(filename, bbox.llx, bbox.lly, bbox.urx, bbox.ury,
	    pdf_file_get_version(pf), count);
}

int extractbb (int argc, char *argv[]) 
{
  pdf_files_init();

  pdf_set_version(PDF_VERSION_MAX);

  argc -= 1; argv += 1;

  while (argc > 0 && *argv[0] == '-') {
    char *flag;

    for (flag = argv[0] + 1; *flag != 0; flag++) {
      switch (*flag) {
      case '-':
        if (flag == argv[0] + 1) {
          ++flag;
          if (!strcmp(flag, "help")) {
            show_usage();
            exit(0);
          } else if (!strcmp(flag, "version")) {
            show_version();
            exit(0);
          }
        }
        fprintf(stderr, "Unknown option \"%s\"", argv[0]);
        usage();
      case 'O':
        xbb_to_file = 0;
        break;
      case 'b':  /* Ignored for backward compatibility */
        break;
      case 'm':
        compat_mode = 1;
        break;
      case 'x':
        compat_mode = 0;
        break;
      case 'v':
        verbose = 1;
        break;
      case 'h':  
        show_usage();
        exit (0);
      default:
        fprintf (stderr, "Unknown option in \"%s\"", argv[0]);
        usage();
      }
    }
    argc -= 1; argv += 1;
  }

  if (argc == 0) {
    fprintf (stderr, "Missing filename argument\n");
    usage();
  }

  for (; argc > 0; argc--, argv++) {
    FILE *infile = NULL;
    char *kpse_file_name = NULL;

    if (kpse_in_name_ok(argv[0])) {
      infile = MFOPEN(argv[0], FOPEN_RBIN_MODE);
      if (infile) {
        kpse_file_name = xstrdup(argv[0]);
      } else {
        kpse_file_name = kpse_find_pict(argv[0]);
        if (kpse_file_name && kpse_in_name_ok(kpse_file_name))
          infile = MFOPEN(kpse_file_name, FOPEN_RBIN_MODE);
      }
    }
    if (infile == NULL) {
      WARN("Can't find file (%s), or it is forbidden to read ...skipping\n", argv[0]);
      goto cont;
    }
    if (check_for_jpeg(infile)) {
      do_jpeg(infile, kpse_file_name);
      goto cont;
    }
    if (check_for_pdf(infile)) {
      do_pdf(infile, kpse_file_name);
      goto cont;
    }
#ifdef HAVE_LIBPNG
    if (check_for_png(infile)) {
      do_png(infile, kpse_file_name);
      goto cont;
    }
#endif /* HAVE_LIBPNG */
    WARN("Can't handle file type for file named %s\n", argv[0]);
  cont:
    if (kpse_file_name)
      RELEASE(kpse_file_name);
    if (infile)
      MFCLOSE(infile);
  }

  pdf_files_close();

  return 0;
}
