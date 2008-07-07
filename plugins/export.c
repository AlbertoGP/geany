/*
 *      export.c - this file is part of Geany, a fast and lightweight IDE
 *
 *      Copyright 2007-2008 Enrico Tröger <enrico(dot)troeger(at)uvena(dot)de>
 *      Copyright 2007-2008 Nick Treleaven <nick(dot)treleaven(at)btinternet(dot)com>
 *
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2 of the License, or
 *      (at your option) any later version.
 *
 *      This program is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License
 *      along with this program; if not, write to the Free Software
 *      Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 *      MA 02110-1301, USA.
 *
 * $Id$
 */

/* Export plugin. */

#include <ctype.h>
#include <math.h>

#include "geany.h"
#include "support.h"
#include "plugindata.h"
#include "editor.h"
#include "document.h"
#include "prefs.h"
#include "utils.h"
#include "ui_utils.h"
#include "pluginmacros.h"


PluginFields	*plugin_fields;
GeanyData		*geany_data;
GeanyFunctions	*geany_functions;

PLUGIN_VERSION_CHECK(69)
PLUGIN_SET_INFO(_("Export"), _("Exports the current file into different formats."), VERSION,
	_("The Geany developer team"))

#define ROTATE_RGB(color) \
	(((color) & 0xFF0000) >> 16) + ((color) & 0x00FF00) + (((color) & 0x0000FF) << 16)
#define TEMPLATE_HTML "\
<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML 1.0 Strict//EN\"\n\
  \"http://www.w3.org/TR/xhtml1/DTD/xhtml1-strict.dtd\">\n\
<html xmlns=\"http://www.w3.org/1999/xhtml\" xml:lang=\"en\" lang=\"en\">\n\
\n\
<head>\n\
	<title>{export_filename}</title>\n\
	<meta http-equiv=\"content-type\" content=\"text/html;charset=utf-8\" />\n\
	<meta name=\"generator\" content=\"Geany " VERSION "\" />\n\
	<meta name=\"date\" content=\"{export_date}\">\n\
	<style type=\"text/css\">\n\
{export_styles}\n\
	</style>\n\
</head>\n\
\n\
<body>\n\
<p>\n\
{export_content}\n\
</p>\n\
</body>\n\
</html>\n"

#define TEMPLATE_LATEX "\
% {export_filename} (LaTeX code generated by Geany " VERSION " on {export_date})\n\
\\documentclass[a4paper]{article}\n\
\\usepackage[a4paper,margin=2cm]{geometry}\n\
\\usepackage[utf8x]{inputenc}\n\
\\usepackage[T1]{fontenc}\n\
\\usepackage{color}\n\
\\setlength{\\parindent}{0em}\n\
\\setlength{\\parskip}{2ex plus1ex minus0.5ex}\n\
{export_styles}\n\
\\begin{document}\
\n\
\\ttfamily\n\
\\setlength{\\fboxrule}{0pt}\n\
\\setlength{\\fboxsep}{0pt}\n\
{export_content}\
\\end{document}\n"


enum
{
	FORE = 0,
	BACK,
	BOLD,
	ITALIC,
	USED,
	MAX_TYPES
};

enum
{
	DATE_TYPE_DEFAULT,
	DATE_TYPE_HTML
};

typedef void (*ExportFunc) (GeanyDocument *doc, const gchar *filename, gboolean use_zoom);
typedef struct
{
	GeanyDocument *doc;
	gboolean have_zoom_level_checkbox;
	ExportFunc export_func;
} ExportInfo;

static void on_file_save_dialog_response(GtkDialog *dialog, gint response, gpointer user_data);
static void write_html_file(GeanyDocument *doc, const gchar *filename, gboolean use_zoom);
static void write_latex_file(GeanyDocument *doc, const gchar *filename, gboolean use_zoom);


/* converts a RGB colour into a LaTeX compatible representation, taken from SciTE */
static gchar* get_tex_rgb(gint rgb_colour)
{
	/* texcolor[rgb]{0,0.5,0}{....} */
	gdouble rf = (rgb_colour % 256) / 256.0;
	gdouble gf = ((rgb_colour & - 16711936) / 256) / 256.0;
	gdouble bf = ((rgb_colour & 0xff0000) / 65536) / 256.0;
	gint r = (gint) (rf * 10 + 0.5);
	gint g = (gint) (gf * 10 + 0.5);
	gint b = (gint) (bf * 10 + 0.5);

	return g_strdup_printf("%d.%d, %d.%d, %d.%d", r / 10, r % 10, g / 10, g % 10, b / 10, b % 10);
}


/* convert a style number (0..127) into a string representation (aa, ab, .., ba, bb, .., zy, zz) */
static gchar *get_tex_style(gint style)
{
	static gchar buf[4];
	int i = 0;

	do
	{
		buf[i] = (style % 26) + 'a';
		style /= 26;
		i++;
	} while (style > 0);
	buf[i] = '\0';

	return buf;
}


static void create_file_save_as_dialog(const gchar *extension, ExportFunc func,
									   gboolean show_zoom_level_checkbox)
{
	GtkWidget *dialog;
	GtkTooltips *tooltips;
	GeanyDocument *doc;
	ExportInfo *exi;

	if (extension == NULL)
		return;

	doc = p_document->get_current();
	tooltips = GTK_TOOLTIPS(p_support->lookup_widget(geany->main_widgets->window, "tooltips"));

	exi = g_new(ExportInfo, 1);
	exi->doc = doc;
	exi->export_func = func;
	exi->have_zoom_level_checkbox = FALSE;

	dialog = gtk_file_chooser_dialog_new(_("Export File"), GTK_WINDOW(geany->main_widgets->window),
				GTK_FILE_CHOOSER_ACTION_SAVE, NULL, NULL);
	gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
	gtk_window_set_destroy_with_parent(GTK_WINDOW(dialog), TRUE);
	gtk_window_set_skip_taskbar_hint(GTK_WINDOW(dialog), TRUE);
	gtk_window_set_type_hint(GTK_WINDOW(dialog), GDK_WINDOW_TYPE_HINT_DIALOG);
	gtk_widget_set_name(dialog, "GeanyExportDialog");

	gtk_dialog_add_buttons(GTK_DIALOG(dialog),
		GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL, GTK_STOCK_SAVE, GTK_RESPONSE_ACCEPT, NULL);
	gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);

	if (show_zoom_level_checkbox)
	{
		GtkWidget *vbox, *check_zoom_level;

		vbox = gtk_vbox_new(FALSE, 0);
		check_zoom_level = gtk_check_button_new_with_mnemonic(_("_Use current zoom level"));
		gtk_tooltips_set_tip(tooltips, check_zoom_level,
			_("Renders the font size of the document together with the current zoom level."), NULL);
		gtk_box_pack_start(GTK_BOX(vbox), check_zoom_level, FALSE, FALSE, 0);
		gtk_widget_show_all(vbox);
		gtk_file_chooser_set_extra_widget(GTK_FILE_CHOOSER(dialog), vbox);

		g_object_set_data_full(G_OBJECT(dialog), "check_zoom_level",
					gtk_widget_ref(check_zoom_level), (GDestroyNotify) gtk_widget_unref);

		exi->have_zoom_level_checkbox = TRUE;
	}

	g_signal_connect((gpointer) dialog, "delete_event",
		G_CALLBACK(gtk_widget_hide_on_delete), NULL);
	g_signal_connect((gpointer) dialog, "response",
		G_CALLBACK(on_file_save_dialog_response), exi);

	gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(geany->main_widgets->window));

	/* if the current document has a filename we use it as the default. */
	gtk_file_chooser_unselect_all(GTK_FILE_CHOOSER(dialog));
	if (doc->file_name != NULL)
	{
		gchar *base_name = g_path_get_basename(doc->file_name);
		gchar *short_name = p_utils->remove_ext_from_filename(base_name);
		gchar *file_name;
		gchar *locale_filename;
		gchar *locale_dirname;
		gchar *suffix = "";

		if (g_str_has_suffix(doc->file_name, extension))
			suffix = "_export";

		file_name = g_strconcat(short_name, suffix, extension, NULL);
		locale_filename = p_utils->get_locale_from_utf8(doc->file_name);
		locale_dirname = g_path_get_dirname(locale_filename);
		/* set the current name to base_name.html which probably doesn't exist yet so
		 * gtk_file_chooser_set_filename() can't be used and we need
		 * gtk_file_chooser_set_current_folder() additionally */
		gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dialog), locale_dirname);
		gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dialog), file_name);
		g_free(locale_filename);
		g_free(short_name);
		g_free(file_name);
		g_free(base_name);
	}
	else
	{
		const gchar *default_open_path = geany->prefs->default_open_path;
		gchar *fname = g_strconcat(GEANY_STRING_UNTITLED, extension, NULL);

		gtk_file_chooser_unselect_all(GTK_FILE_CHOOSER(dialog));
		gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dialog), fname);

		/* use default startup directory(if set) if no files are open */
		if (NZV(default_open_path) && g_path_is_absolute(default_open_path))
		{
			gchar *locale_path = p_utils->get_locale_from_utf8(default_open_path);
			gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dialog), locale_path);
			g_free(locale_path);
		}
		g_free(fname);
	}
	gtk_dialog_run(GTK_DIALOG(dialog));
}


static void on_menu_create_latex_activate(GtkMenuItem *menuitem, gpointer user_data)
{
	create_file_save_as_dialog(".tex", write_latex_file, FALSE);
}


static void on_menu_create_html_activate(GtkMenuItem *menuitem, gpointer user_data)
{
	create_file_save_as_dialog(".html", write_html_file, TRUE);
}


static void write_data(const gchar *filename, const gchar *data)
{
	gint error_nr = p_utils->write_file(filename, data);
	gchar *utf8_filename = p_utils->get_utf8_from_locale(filename);

	if (error_nr == 0)
		p_ui->set_statusbar(TRUE, _("Document successfully exported as '%s'."), utf8_filename);
	else
		p_ui->set_statusbar(TRUE, _("File '%s' could not be written (%s)."),
			utf8_filename, g_strerror(error_nr));

	g_free(utf8_filename);
}


static const gchar *get_date(gint type)
{
	static gchar str[128];
	gchar *format;
	time_t t;
	struct tm *tmp;

	t = time(NULL);
	tmp = localtime(&t);
	if (tmp == NULL)
		return "";

	if (type == DATE_TYPE_HTML)
/** needs testing */
#ifdef _GNU_SOURCE
		format = "%Y-%m-%dT%H:%M:%S%z";
#else
		format = "%Y-%m-%dT%H:%M:%S";
#endif
	else
		format = "%c";

	strftime(str, sizeof str, format, tmp);

	return str;
}


static void on_file_save_dialog_response(GtkDialog *dialog, gint response, gpointer user_data)
{
	ExportInfo *exi = user_data;

	if (response == GTK_RESPONSE_ACCEPT && exi != NULL)
	{
		gchar *new_filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
		gchar *utf8_filename;
		gboolean use_zoom_level = FALSE;

		if (exi->have_zoom_level_checkbox)
		{
			use_zoom_level = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(
				p_support->lookup_widget(GTK_WIDGET(dialog), "check_zoom_level")));
		}

		utf8_filename = p_utils->get_utf8_from_locale(new_filename);

		/* check if file exists and ask whether to overwrite or not */
		if (g_file_test(new_filename, G_FILE_TEST_EXISTS))
		{
			if (p_dialogs->show_question(
				_("The file '%s' already exists. Do you want to overwrite it?"),
				utf8_filename) == FALSE)
				return;
		}

		exi->export_func(exi->doc, new_filename, use_zoom_level);

		g_free(utf8_filename);
		g_free(new_filename);
	}
	g_free(exi);
	gtk_widget_destroy(GTK_WIDGET(dialog));
}


static void write_latex_file(GeanyDocument *doc, const gchar *filename, gboolean use_zoom)
{
	gint i, style = -1, old_style = 0, column = 0;
	gchar c, c_next, *tmp;
	/* 0 - fore, 1 - back, 2 - bold, 3 - italic, 4 - font size, 5 - used(0/1) */
	gint styles[STYLE_MAX + 1][MAX_TYPES];
	gboolean block_open = FALSE;
	GString *body;
	GString *cmds;
	GString *latex;
	gint style_max = pow(2, p_sci->send_message(doc->sci, SCI_GETSTYLEBITS, 0, 0));

	/* first read all styles from Scintilla */
	for (i = 0; i < style_max; i++)
	{
		styles[i][FORE] = p_sci->send_message(doc->sci, SCI_STYLEGETFORE, i, 0);
		styles[i][BACK] = p_sci->send_message(doc->sci, SCI_STYLEGETBACK, i, 0);
		styles[i][BOLD] = p_sci->send_message(doc->sci, SCI_STYLEGETBOLD, i, 0);
		styles[i][ITALIC] = p_sci->send_message(doc->sci, SCI_STYLEGETITALIC, i, 0);
		styles[i][USED] = 0;
	}

	/* read the document and write the LaTeX code */
	body = g_string_new("");
	for (i = 0; i < p_sci->get_length(doc->sci); i++)
	{
		style = p_sci->get_style_at(doc->sci, i);
		c = p_sci->get_char_at(doc->sci, i);
		c_next = p_sci->get_char_at(doc->sci, i + 1);

		if (style != old_style || ! block_open)
		{
			old_style = style;
			styles[style][USED] = 1;
			if (block_open)
			{
				g_string_append(body, "}\n");
				block_open = FALSE;
			}
			g_string_append_printf(body, "\\style%s{", get_tex_style(style));

			block_open = TRUE;
		}
		/* escape the current character if necessary else just add it */
		switch (c)
		{
			case '\r':
			case '\n':
			{
				if (c == '\r' && c_next == '\n')
					continue; /* when using CR/LF skip CR and add the line break with LF */

				if (block_open)
				{
					g_string_append(body, "}");
					block_open = FALSE;
				}
				g_string_append(body, " \\\\\n");
				column = -1;
				break;
			}
			case '\t':
			{
				gint tab_stop = geany->editor_prefs->tab_width -
					(column % geany->editor_prefs->tab_width);

				column += tab_stop - 1; /* -1 because we add 1 at the end of the loop */
				g_string_append_printf(body, "\\hspace*{%dem}", tab_stop);
				break;
			}
			case ' ':
			{
				if (c_next == ' ')
				{
					g_string_append(body, "{\\hspace*{1em}}");
					i++; /* skip the next character */
				}
				else
					g_string_append_c(body, ' ');
				break;
			}
			case '{':
			case '}':
			case '_':
			case '&':
			case '$':
			case '#':
			case '%':
			{
				g_string_append_printf(body, "\\%c", c);
				break;
			}
			case '\\':
			{
				g_string_append(body, "\\symbol{92}");
				break;
			}
			case '~':
			{
				g_string_append(body, "\\symbol{126}");
				break;
			}
			case '^':
			{
				g_string_append(body, "\\symbol{94}");
				break;
			}
			/** TODO still don't work for "---" or "----" */
			case '-':  /* mask "--" */
			{
				if (c_next == '-')
				{
					g_string_append(body, "-\\/-");
					i++; /* skip the next character */
				}
				else
					g_string_append_c(body, '-');

				break;
			}
			case '<':  /* mask "<<" */
			{
				if (c_next == '<')
				{
					g_string_append(body, "<\\/<");
					i++; /* skip the next character */
				}
				else
					g_string_append_c(body, '<');

				break;
			}
			case '>':  /* mask ">>" */
			{
				if (c_next == '>')
				{
					g_string_append(body, ">\\/>");
					i++; /* skip the next character */
				}
				else
					g_string_append_c(body, '>');

				break;
			}
			default: g_string_append_c(body, c);
		}
		column++;
	}
	if (block_open)
	{
		g_string_append(body, "}\n");
		block_open = FALSE;
	}

	/* force writing of style 0 (used at least for line breaks) */
	styles[0][USED] = 1;

	/* write used styles in the header */
	cmds = g_string_new("");
	for (i = 0; i <= STYLE_MAX; i++)
	{
		if (styles[i][USED])
		{
			g_string_append_printf(cmds,
				"\\newcommand{\\style%s}[1]{\\noindent{", get_tex_style(i));
			if (styles[i][BOLD])
				g_string_append(cmds, "\\textbf{");
			if (styles[i][ITALIC])
				g_string_append(cmds, "\\textit{");

			tmp = get_tex_rgb(styles[i][FORE]);
			g_string_append_printf(cmds, "\\textcolor[rgb]{%s}{", tmp);
			g_free(tmp);
			tmp = get_tex_rgb(styles[i][BACK]);
			g_string_append_printf(cmds, "\\fcolorbox[rgb]{0, 0, 0}{%s}{", tmp);
			g_string_append(cmds, "#1}}");
			g_free(tmp);

			if (styles[i][BOLD])
				g_string_append_c(cmds, '}');
			if (styles[i][ITALIC])
				g_string_append_c(cmds, '}');
			g_string_append(cmds, "}}\n");
		}
	}

	/* write all */
	latex = g_string_new(TEMPLATE_LATEX);
	p_utils->string_replace_all(latex, "{export_content}", body->str);
	p_utils->string_replace_all(latex, "{export_styles}", cmds->str);
	p_utils->string_replace_all(latex, "{export_date}", get_date(DATE_TYPE_DEFAULT));
	if (doc->file_name == NULL)
		p_utils->string_replace_all(latex, "{export_filename}", GEANY_STRING_UNTITLED);
	else
		p_utils->string_replace_all(latex, "{export_filename}", doc->file_name);

	write_data(filename, latex->str);

	g_string_free(body, TRUE);
	g_string_free(cmds, TRUE);
	g_string_free(latex, TRUE);
}


static void write_html_file(GeanyDocument *doc, const gchar *filename, gboolean use_zoom)
{
	gint i, style = -1, old_style = 0, column = 0;
	gchar c, c_next;
	/* 0 - fore, 1 - back, 2 - bold, 3 - italic, 4 - font size, 5 - used(0/1) */
	gint styles[STYLE_MAX + 1][MAX_TYPES];
	gboolean span_open = FALSE;
	const gchar *font_name;
	gint font_size;
	PangoFontDescription *font_desc;
	GString *body;
	GString *css;
	GString *html;
	gint style_max = pow(2, p_sci->send_message(doc->sci, SCI_GETSTYLEBITS, 0, 0));

	/* first read all styles from Scintilla */
	for (i = 0; i < style_max; i++)
	{
		styles[i][FORE] = ROTATE_RGB(p_sci->send_message(doc->sci, SCI_STYLEGETFORE, i, 0));
		styles[i][BACK] = ROTATE_RGB(p_sci->send_message(doc->sci, SCI_STYLEGETBACK, i, 0));
		styles[i][BOLD] = p_sci->send_message(doc->sci, SCI_STYLEGETBOLD, i, 0);
		styles[i][ITALIC] = p_sci->send_message(doc->sci, SCI_STYLEGETITALIC, i, 0);
		styles[i][USED] = 0;
	}

	/* read Geany's font and font size */
	font_desc = pango_font_description_from_string(geany->interface_prefs->editor_font);
	font_name = pango_font_description_get_family(font_desc);
	/*font_size = pango_font_description_get_size(font_desc) / PANGO_SCALE;*/
	/* take the zoom level also into account */
	font_size = p_sci->send_message(doc->sci, SCI_STYLEGETSIZE, 0, 0);
	if (use_zoom)
		font_size += p_sci->send_message(doc->sci, SCI_GETZOOM, 0, 0);

	/* read the document and write the HTML body */
	body = g_string_new("");
	for (i = 0; i < p_sci->get_length(doc->sci); i++)
	{
		style = p_sci->get_style_at(doc->sci, i);
		c = p_sci->get_char_at(doc->sci, i);
		/* p_sci->get_char_at() takes care of index boundaries and return 0 if i is too high */
		c_next = p_sci->get_char_at(doc->sci, i + 1);

		if ((style != old_style || ! span_open) && ! isspace(c))
		{
			old_style = style;
			styles[style][USED] = 1;
			if (span_open)
			{
				g_string_append(body, "</span>");
			}
			g_string_append_printf(body, "<span class=\"style_%d\">", style);

			span_open = TRUE;
		}
		/* escape the current character if necessary else just add it */
		switch (c)
		{
			case '\r':
			case '\n':
			{
				if (c == '\r' && c_next == '\n')
					continue; /* when using CR/LF skip CR and add the line break with LF */

				if (span_open)
				{
					g_string_append(body, "</span>");
					span_open = FALSE;
				}
				g_string_append(body, "<br />\n");
				column = -1;
				break;
			}
			case '\t':
			{
				gint j;
				gint tab_stop = geany->editor_prefs->tab_width -
					(column % geany->editor_prefs->tab_width);

				column += tab_stop - 1; /* -1 because we add 1 at the end of the loop */
				for (j = 0; j < tab_stop; j++)
				{
					g_string_append(body, "&nbsp;");
				}
				break;
			}
			case ' ':
			{
				g_string_append(body, "&nbsp;");
				break;
			}
			case '<':
			{
				g_string_append(body, "&lt;");
				break;
			}
			case '>':
			{
				g_string_append(body, "&gt;");
				break;
			}
			case '&':
			{
				g_string_append(body, "&amp;");
				break;
			}
			default: g_string_append_c(body, c);
		}
		column++;
	}
	if (span_open)
	{
		g_string_append(body, "</span>");
		span_open = FALSE;
	}

	/* write used styles in the header */
	css = g_string_new("");
	g_string_append_printf(css,
	"\tbody\n\t{\n\t\tfont-family: %s, monospace;\n\t\tfont-size: %dpt;\n\t}\n",
				font_name, font_size);

	for (i = 0; i <= STYLE_MAX; i++)
	{
		if (styles[i][USED])
		{
			g_string_append_printf(css,
	"\t.style_%d\n\t{\n\t\tcolor: #%06x;\n\t\tbackground-color: #%06x;\n%s%s\t}\n",
				i, styles[i][FORE], styles[i][BACK],
				(styles[i][BOLD]) ? "\t\tfont-weight: bold;\n" : "",
				(styles[i][ITALIC]) ? "\t\tfont-style: italic;\n" : "");
		}
	}

	/* write all */
	html = g_string_new(TEMPLATE_HTML);
	p_utils->string_replace_all(html, "{export_date}", get_date(DATE_TYPE_HTML));
	p_utils->string_replace_all(html, "{export_content}", body->str);
	p_utils->string_replace_all(html, "{export_styles}", css->str);
	if (doc->file_name == NULL)
		p_utils->string_replace_all(html, "{export_filename}", GEANY_STRING_UNTITLED);
	else
		p_utils->string_replace_all(html, "{export_filename}", doc->file_name);

	write_data(filename, html->str);

	pango_font_description_free(font_desc);
	g_string_free(body, TRUE);
	g_string_free(css, TRUE);
	g_string_free(html, TRUE);
}


void plugin_init(GeanyData *data)
{
	GtkWidget *menu_export;
	GtkWidget *menu_export_menu;
	GtkWidget *menu_create_html;
	GtkWidget *menu_create_latex;

	menu_export = gtk_image_menu_item_new_with_mnemonic(_("_Export"));
	gtk_container_add(GTK_CONTAINER(geany->main_widgets->tools_menu), menu_export);

	menu_export_menu = gtk_menu_new ();
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(menu_export), menu_export_menu);

	/* HTML */
	menu_create_html = gtk_menu_item_new_with_mnemonic(_("As _HTML"));
	gtk_container_add(GTK_CONTAINER (menu_export_menu), menu_create_html);

	g_signal_connect((gpointer) menu_create_html, "activate",
		G_CALLBACK(on_menu_create_html_activate), NULL);

	/* LaTeX */
	menu_create_latex = gtk_menu_item_new_with_mnemonic(_("As _LaTeX"));
	gtk_container_add(GTK_CONTAINER (menu_export_menu), menu_create_latex);

	g_signal_connect((gpointer) menu_create_latex, "activate",
		G_CALLBACK(on_menu_create_latex_activate), NULL);

	/* disable menu_item when there are no documents open */
	plugin_fields->menu_item = menu_export;
	plugin_fields->flags = PLUGIN_IS_DOCUMENT_SENSITIVE;

	gtk_widget_show_all(menu_export);
}


void plugin_cleanup(void)
{
	gtk_widget_destroy(plugin_fields->menu_item);
}
