/*
 * Samba Unix/Linux SMB client library
 * Registry Editor
 * Copyright (C) Christopher Davis 2012
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "includes.h"
#include "regedit_dialog.h"
#include "regedit_valuelist.h"
#include "regedit_hexedit.h"
#include "util_reg.h"
#include "lib/registry/registry.h"
#include <stdarg.h>
#include <form.h>

static char *string_trim_n(TALLOC_CTX *ctx, const char *buf, size_t n)
{
	char *str;

	str = talloc_strndup(ctx, buf, n);

	if (str) {
		trim_string(str, " ", " ");
	}

	return str;
}

static char *string_trim(TALLOC_CTX *ctx, const char *buf)
{
	char *str;

	str = talloc_strdup(ctx, buf);

	if (str) {
		trim_string(str, " ", " ");
	}

	return str;
}

static int dialog_free(struct dialog *dia)
{
	if (dia->window) {
		delwin(dia->window);
	}
	if (dia->sub_window) {
		delwin(dia->sub_window);
	}
	if (dia->panel) {
		del_panel(dia->panel);
	}
	if (dia->choices) {
		unpost_menu(dia->choices);
		free_menu(dia->choices);
	}
	if (dia->choice_items) {
		ITEM **it;
		for (it = dia->choice_items; *it != NULL; ++it) {
			free_item(*it);
		}
	}

	return 0;
}

struct dialog *dialog_new(TALLOC_CTX *ctx, const char *title, int nlines,
			  int ncols, int y, int x)
{
	struct dialog *dia;

	dia = talloc_zero(ctx, struct dialog);
	if (dia == NULL) {
		return NULL;
	}

	talloc_set_destructor(dia, dialog_free);

	dia->window = newwin(nlines, ncols, y, x);
	if (dia->window == NULL) {
		goto fail;
	}

	box(dia->window, 0, 0);
	mvwaddstr(dia->window, 0, 1, title);

	/* body of the dialog within the box outline */
	dia->sub_window = derwin(dia->window, nlines - 2, ncols - 2, 1, 1);
	if (dia->sub_window == NULL) {
		goto fail;
	}

	dia->panel = new_panel(dia->window);
	if (dia->panel == NULL) {
		goto fail;
	}

	return dia;

fail:
	talloc_free(dia);

	return NULL;

}

static void center_dialog_above_window(WINDOW *below, int *nlines, int *ncols,
				       int *y, int *x)
{
	int maxy, maxx;
	int centery, centerx;

	getmaxyx(below, maxy, maxx);

	centery = maxy / 2;
	centerx = maxx / 2;
	*y = 0;
	*x = 0;

	if (*nlines > maxy) {
		*nlines = maxy;
	}
	if (*ncols > maxx) {
		*ncols = maxx;
	}

	if (*nlines < centery) {
		*y = centery - *nlines;
	}
	if (*ncols < centerx) {
		*x = centerx - *ncols;
	}
}

struct dialog *dialog_center_new(TALLOC_CTX *ctx, const char *title, int nlines,
				 int ncols, WINDOW *below)
{
	int y, x;

	center_dialog_above_window(below, &nlines, &ncols, &y, &x);

	return dialog_new(ctx, title, nlines, ncols, y, x);
}

struct dialog *dialog_choice_new(TALLOC_CTX *ctx, const char *title,
				 const char **choices, int nlines,
				 int ncols, int y, int x)
{
	size_t nchoices, i;
	struct dialog *dia;

	dia = dialog_new(ctx, title, nlines, ncols, y, x);
	if (dia == NULL) {
		return NULL;
	}

	dia->menu_window = derwin(dia->sub_window, 1, ncols - 3,
				  nlines - 3, 0);
	if (dia->menu_window == NULL) {
		goto fail;
	}

	for (nchoices = 0; choices[nchoices] != NULL; ++nchoices)
		;
	dia->choice_items = talloc_zero_array(dia, ITEM *, nchoices + 1);
	if (dia->choice_items == NULL) {
		goto fail;
	}
	for (i = 0; i < nchoices; ++i) {
		char *desc = talloc_strdup(dia, choices[i]);
		if (desc == NULL) {
			goto fail;
		}
		dia->choice_items[i] = new_item(desc, desc);
		if (dia->choice_items[i] == NULL) {
			goto fail;
		}
		/* store choice index */
		set_item_userptr(dia->choice_items[i], (void*)(uintptr_t)i);
	}

	dia->choices = new_menu(dia->choice_items);
	if (dia->choices == NULL) {
		goto fail;
	}

	set_menu_format(dia->choices, 1, ncols);
	set_menu_win(dia->choices, dia->sub_window);
	set_menu_sub(dia->choices, dia->menu_window);
	menu_opts_off(dia->choices, O_SHOWDESC);
	set_menu_mark(dia->choices, "* ");
	post_menu(dia->choices);
	wmove(dia->sub_window, 0, 0);

	return dia;

fail:
	talloc_free(dia);

	return NULL;
}

struct dialog *dialog_choice_center_new(TALLOC_CTX *ctx, const char *title,
					const char **choices, int nlines,
					int ncols, WINDOW *below)
{
	int y, x;

	center_dialog_above_window(below, &nlines, &ncols, &y, &x);

	return dialog_choice_new(ctx, title, choices, nlines, ncols, y, x);
}

static int handle_menu_input(MENU *menu, int c)
{
	ITEM *item;

	switch (c) {
	case KEY_LEFT:
		menu_driver(menu, REQ_LEFT_ITEM);
		break;
	case KEY_RIGHT:
		menu_driver(menu, REQ_RIGHT_ITEM);
		break;
	case KEY_ENTER:
	case '\n':
		item = current_item(menu);
		return (int)(uintptr_t)item_userptr(item);
	}

	return -1;
}

static void handle_form_input(FORM *frm, int c)
{
	switch (c) {
	case '\n':
		form_driver(frm, REQ_NEW_LINE);
		break;
	case KEY_UP:
		form_driver(frm, REQ_UP_CHAR);
		break;
	case KEY_DOWN:
		form_driver(frm, REQ_DOWN_CHAR);
		break;
	case '\b':
	case KEY_BACKSPACE:
		form_driver(frm, REQ_DEL_PREV);
		break;
	case KEY_LEFT:
		form_driver(frm, REQ_LEFT_CHAR);
		break;
	case KEY_RIGHT:
		form_driver(frm, REQ_RIGHT_CHAR);
		break;
	default:
		form_driver(frm, c);
		break;
	}
}

static int modal_loop(struct dialog *dia)
{
	int c;
	int selection = -1;

	keypad(dia->window, true);
	update_panels();
	doupdate();

	while (selection == -1) {
		c = wgetch(dia->window);
		selection = handle_menu_input(dia->choices, c);
		update_panels();
		doupdate();
	}

	talloc_free(dia);

	return selection;
}

static struct dialog *dialog_msg_new(TALLOC_CTX *ctx, const char *title,
				     const char **choices, WINDOW *below,
				     int nlines, const char *msg, va_list ap)
{
	struct dialog *dia;
	char *str;
	int width;
#define MIN_WIDTH 20

	str = talloc_vasprintf(ctx, msg, ap);
	if (str == NULL) {
		return NULL;
	}

	width = strlen(str) + 2;
	if (width < MIN_WIDTH) {
		width = MIN_WIDTH;
	}
	dia = dialog_choice_center_new(ctx, title, choices, nlines, width, below);
	if (dia == NULL) {
		return NULL;
	}

	waddstr(dia->sub_window, str);
	talloc_free(str);

	return dia;
}

int dialog_input(TALLOC_CTX *ctx, char **output, const char *title,
		 WINDOW *below, const char *msg, ...)
{
	va_list ap;
	struct dialog *dia;
	const char *choices[] = {
		"Ok",
		"Cancel",
		NULL
	};
	FIELD *field[2] = {0};
	FORM *input;
	WINDOW *input_win;
	int y, x;
	int rv = -1;
	bool input_section = true;

	va_start(ap, msg);
	dia = dialog_msg_new(ctx, title, choices, below, 7, msg, ap);
	va_end(ap);
	if (dia == NULL) {
		return -1;
	}

	getmaxyx(dia->sub_window, y, x);
	input_win = derwin(dia->sub_window, 1, x - 2, 2, 1);
	if (input_win == NULL) {
		goto finish;
	}
	field[0] = new_field(1, x - 2, 0, 0, 0, 0);
	if (field[0] == NULL) {
		goto finish;
	}

	field_opts_off(field[0], O_BLANK | O_AUTOSKIP | O_STATIC);
	set_field_back(field[0], A_REVERSE);

	input = new_form(field);
	form_opts_off(input, O_NL_OVERLOAD | O_BS_OVERLOAD);
	set_form_win(input, dia->sub_window);
	set_form_sub(input, input_win);
	set_current_field(input, field[0]);
	post_form(input);
	*output = NULL;

	keypad(dia->window, true);
	update_panels();
	doupdate();

	while (rv == -1) {
		int c = wgetch(dia->window);

		if (c == '\t' || c == KEY_BTAB) {
			if (input_section) {
				if (form_driver(input,REQ_VALIDATION) == E_OK) {
					input_section = false;
					menu_driver(dia->choices, REQ_FIRST_ITEM);
				}
			} else {
				input_section = true;
				set_current_field(input, field[0]);
			}
			continue;
		}

		if (input_section) {
			handle_form_input(input, c);
		} else {
			rv = handle_menu_input(dia->choices, c);
			if (rv == DIALOG_OK) {
				const char *buf = field_buffer(field[0], 0);
				*output = string_trim(ctx, buf);
			}
		}
	}

finish:
	if (input) {
		unpost_form(input);
		free_form(input);
	}
	if (field[0]) {
		free_field(field[0]);
	}
	if (input_win) {
		delwin(input_win);
	}
	talloc_free(dia);

	return rv;
}

int dialog_notice(TALLOC_CTX *ctx, enum dialog_type type,
		  const char *title, WINDOW *below,
		  const char *msg, ...)
{
	va_list ap;
	struct dialog *dia;
	const char *choices[] = {
		"Ok",
		"Cancel",
		NULL
	};

	if (type == DIA_ALERT) {
		choices[1] = NULL;
	}

	va_start(ap, msg);
	dia = dialog_msg_new(ctx, title, choices, below, 5, msg, ap);
	va_end(ap);
	if (dia == NULL) {
		return -1;
	}

	return modal_loop(dia);
}

#define MAX_FIELDS 8

enum input_section {
	IN_NAME,
	IN_DATA,
	IN_MENU
};

struct edit_dialog {
	struct dialog *dia;
	WINDOW *input_win;
	FORM *input;
	FIELD *field[MAX_FIELDS];
	struct hexedit *buf;
	enum input_section section;
};

static int edit_dialog_free(struct edit_dialog *edit)
{
	if (edit->input) {
		unpost_form(edit->input);
	}
	if (edit->field[0]) {
		free_field(edit->field[0]);
	}
	if (edit->field[1]) {
		free_field(edit->field[1]);
	}
	delwin(edit->input_win);

	return 0;
}

static WERROR fill_value_buffer(struct edit_dialog *edit,
			        const struct value_item *vitem)
{
	char *tmp;

	switch (vitem->type) {
	case REG_DWORD: {
		uint32_t v = 0;
		if (vitem->data.length >= 4) {
			v = IVAL(vitem->data.data, 0);
		}
		tmp = talloc_asprintf(edit, "0x%x", v);
		if (tmp == NULL) {
			return WERR_NOMEM;
		}
		set_field_buffer(edit->field[1], 0, tmp);
		talloc_free(tmp);
		break;
	}
	case REG_SZ:
	case REG_EXPAND_SZ: {
		const char *s;

		if (!pull_reg_sz(edit, &vitem->data, &s)) {
			return WERR_NOMEM;
		}
		set_field_buffer(edit->field[1], 0, s);
		break;
	}
	case REG_MULTI_SZ: {
		const char **p, **a;
		char *buf = NULL;

		if (!pull_reg_multi_sz(edit, &vitem->data, &a)) {
			return WERR_NOMEM;
		}
		for (p = a; *p != NULL; ++p) {
			if (buf == NULL) {
				buf = talloc_asprintf(edit, "%s\n", *p);
			} else {
				buf = talloc_asprintf_append(buf, "%s\n", *p);
			}
			if (buf == NULL) {
				return WERR_NOMEM;
			}
		}
		set_field_buffer(edit->field[1], 0, buf);
		talloc_free(buf);
	}
	case REG_BINARY:
		/* initialized upon dialog creation */
		break;
	}

	return WERR_OK;
}

static bool value_exists(TALLOC_CTX *ctx, const struct registry_key *key,
			 const char *name)
{
	uint32_t type;
	DATA_BLOB blob;
	WERROR rv;

	rv = reg_key_get_value_by_name(ctx, key, name, &type, &blob);

	return W_ERROR_IS_OK(rv);
}

static WERROR set_value(struct edit_dialog *edit, struct registry_key *key,
			uint32_t type, bool new_value)
{
	WERROR rv;
	DATA_BLOB blob;
	char *name = string_trim(edit, field_buffer(edit->field[0], 0));

	if (!new_value && !edit->buf && !field_status(edit->field[1])) {
		return WERR_OK;
	}
	if (new_value && value_exists(edit, key, name)) {
		return WERR_FILE_EXISTS;
	}

	switch (type) {
	case REG_DWORD: {
		uint32_t val;
		int base = 10;
		const char *buf = field_buffer(edit->field[1], 0);

		if (buf[0] == '0' && tolower(buf[1]) == 'x') {
			base = 16;
		}

		val = strtoul(buf, NULL, base);
		blob = data_blob_talloc(edit, NULL, sizeof(val));
		SIVAL(blob.data, 0, val);
		rv = WERR_OK;
		break;
	}
	case REG_SZ:
	case REG_EXPAND_SZ: {
		const char *buf = field_buffer(edit->field[1], 0);
		char *str = string_trim(edit, buf);

		if (!str || !push_reg_sz(edit, &blob, str)) {
			rv = WERR_NOMEM;
		}
		break;
	}
	case REG_MULTI_SZ: {
		int rows, cols, max;
		const char **arr;
		size_t i;
		const char *buf = field_buffer(edit->field[1], 0);

		dynamic_field_info(edit->field[1], &rows, &cols, &max);

		arr = talloc_zero_array(edit, const char *, rows + 1);
		if (arr == NULL) {
			return WERR_NOMEM;
		}
		for (i = 0; *buf; ++i, buf += cols) {
			SMB_ASSERT(i < rows);
			arr[i] = string_trim_n(edit, buf, cols);
		}
		if (!push_reg_multi_sz(edit, &blob, arr)) {
			rv = WERR_NOMEM;
		}
		break;
	}
	case REG_BINARY:
		blob = data_blob_talloc(edit, NULL, edit->buf->len);
		memcpy(blob.data, edit->buf->data, edit->buf->len);
		break;
	}

	rv = reg_val_set(key, name, type, blob);

	return rv;
}

static void section_down(struct edit_dialog *edit)
{
	switch (edit->section) {
	case IN_NAME:
		if (form_driver(edit->input, REQ_VALIDATION) == E_OK) {
			edit->section = IN_DATA;
			if (edit->buf) {
				hexedit_set_cursor(edit->buf);
			} else {
				set_current_field(edit->input, edit->field[1]);
			}
		}
		break;
	case IN_DATA:
		if (edit->buf ||
		    form_driver(edit->input, REQ_VALIDATION) == E_OK) {
			edit->section = IN_MENU;
			menu_driver(edit->dia->choices, REQ_FIRST_ITEM);
		}
		break;
	case IN_MENU:
		edit->section = IN_NAME;
		set_current_field(edit->input, edit->field[0]);
		break;
	}
}

static void section_up(struct edit_dialog *edit)
{
	switch (edit->section) {
	case IN_NAME:
		if (form_driver(edit->input, REQ_VALIDATION) == E_OK) {
			edit->section = IN_MENU;
			menu_driver(edit->dia->choices, REQ_FIRST_ITEM);
		}
		break;
	case IN_DATA:
		if (edit->buf ||
		    form_driver(edit->input, REQ_VALIDATION) == E_OK) {
			edit->section = IN_NAME;
			set_current_field(edit->input, edit->field[0]);
		}
		break;
	case IN_MENU:
		edit->section = IN_DATA;
		if (edit->buf) {
			hexedit_set_cursor(edit->buf);
		} else {
			set_current_field(edit->input, edit->field[1]);
		}
		break;
	}
}

static void handle_hexedit_input(struct hexedit *buf, int c)
{
	switch (c) {
	case KEY_UP:
		hexedit_driver(buf, HE_CURSOR_UP);
		break;
	case KEY_DOWN:
		hexedit_driver(buf, HE_CURSOR_DOWN);
		break;
	case KEY_LEFT:
		hexedit_driver(buf, HE_CURSOR_LEFT);
		break;
	case KEY_RIGHT:
		hexedit_driver(buf, HE_CURSOR_RIGHT);
		break;
	default:
		hexedit_driver(buf, c);
		break;
	}

	hexedit_set_cursor(buf);
}

WERROR dialog_edit_value(TALLOC_CTX *ctx, struct registry_key *key, uint32_t type,
		      const struct value_item *vitem, WINDOW *below)
{
	struct edit_dialog *edit;
	const char *choices[] = {
		"Ok",
		"Cancel",
		"Resize",
		NULL
	};
#define DIALOG_RESIZE 2
	char *title;
	int nlines, ncols, val_rows;
	WERROR rv = WERR_NOMEM;
	int selection;

	edit = talloc_zero(ctx, struct edit_dialog);
	if (edit == NULL) {
		return rv;
	}
	talloc_set_destructor(edit, edit_dialog_free);

	title = talloc_asprintf(edit, "Edit %s value", str_regtype(type));
	if (title == NULL) {
		goto finish;
	}

	nlines = 9;
	if (type == REG_MULTI_SZ) {
		nlines += 4;
	} else if (type == REG_BINARY) {
		nlines += 10;
	}
	/* don't include a resize button */
	if (type != REG_BINARY) {
		choices[2] = NULL;
	}
	ncols = 50;
	edit->dia = dialog_choice_center_new(edit, title, choices, nlines,
					     ncols, below);
	talloc_free(title);
	if (edit->dia == NULL) {
		goto finish;
	}

	/* name */
	edit->field[0] = new_field(1, ncols - 4, 1, 1, 0, 0);
	if (edit->field[0] == NULL) {
		goto finish;
	}

	/* data */
	if (type == REG_BINARY) {
		size_t len = 8;
		const void *buf = NULL;

		if (vitem) {
			len = vitem->data.length;
			buf = vitem->data.data;
		}
		edit->buf = hexedit_new(edit, edit->dia->sub_window, 10,
					5, 0, buf, len);
		if (edit->buf == NULL) {
			goto finish;
		}
		hexedit_refresh(edit->buf);
		hexedit_set_cursor(edit->buf);
		edit->input_win = derwin(edit->dia->sub_window, 2,
					 ncols - 3, 0, 0);

	} else {
		val_rows = 1;
		if (type == REG_MULTI_SZ) {
			val_rows += 4;
		}
		edit->field[1] = new_field(val_rows, ncols - 4, 4, 1, 0, 0);
		if (edit->field[1] == NULL) {
			goto finish;
		}
		edit->input_win = derwin(edit->dia->sub_window, nlines - 3,
					 ncols - 3, 0, 0);
	}
	if (edit->input_win == NULL) {
		goto finish;
	}

	set_field_back(edit->field[0], A_REVERSE);
	field_opts_off(edit->field[0], O_BLANK | O_AUTOSKIP | O_STATIC);
	if (edit->field[1]) {
		set_field_back(edit->field[1], A_REVERSE);
		field_opts_off(edit->field[1], O_BLANK | O_AUTOSKIP | O_STATIC | O_WRAP);
		if (type == REG_DWORD) {
			set_field_type(edit->field[1], TYPE_REGEXP,
				       "^ *([0-9]+|0[xX][0-9a-fA-F]+) *$");
		}
	}

	if (vitem) {
		set_field_buffer(edit->field[0], 0, vitem->value_name);
		field_opts_off(edit->field[0], O_EDIT);
		fill_value_buffer(edit, vitem);
	}

	edit->input = new_form(edit->field);
	if (edit->input == NULL) {
		goto finish;
	}
	form_opts_off(edit->input, O_NL_OVERLOAD | O_BS_OVERLOAD);


	set_form_win(edit->input, edit->dia->sub_window);
	set_form_sub(edit->input, edit->input_win);
	post_form(edit->input);
	mvwprintw(edit->dia->sub_window, 0, 0, "Name");
	mvwprintw(edit->dia->sub_window, 3, 0, "Data");

	keypad(edit->dia->window, true);
	update_panels();
	doupdate();

	edit->section = IN_NAME;

	while (1) {
		int c = wgetch(edit->dia->window);
		if (c == '\t') {
			section_down(edit);
			continue;
		} else if (c == KEY_BTAB) {
			section_up(edit);
			continue;
		}

		if (edit->section == IN_NAME) {
			handle_form_input(edit->input, c);
		} else if (edit->section == IN_DATA) {
			if (edit->buf) {
				handle_hexedit_input(edit->buf, c);
			} else {
				handle_form_input(edit->input, c);
			}
		} else {
			selection = handle_menu_input(edit->dia->choices, c);
			if (selection == DIALOG_OK) {
				rv = set_value(edit, key, type, vitem == NULL);
				if (W_ERROR_EQUAL(rv, WERR_FILE_EXISTS)) {
					dialog_notice(edit, DIA_ALERT,
						      "Value exists", below,
						      "Value name already exists.");
					selection = -1;
				} else {
					goto finish;
				}
			} else if (selection == DIALOG_RESIZE) {
				char *n;
				size_t newlen = 0;

				dialog_input(edit, &n, "Resize buffer", below,
					     "Enter new size");
				if (n) {
					newlen = strtoul(n, NULL, 10);
					hexedit_resize_buffer(edit->buf, newlen);
					hexedit_refresh(edit->buf);
					talloc_free(n);
				}
			} else if (selection == DIALOG_CANCEL) {
				rv = WERR_OK;
				goto finish;
			}
		}

		update_panels();
		doupdate();
	}

finish:
	talloc_free(edit);

	return rv;
}

int dialog_select_type(TALLOC_CTX *ctx, int *type, WINDOW *below)
{
	struct dialog *dia;
	const char *choices[] = {
		"OK",
		"Cancel",
		NULL
	};
	const char *reg_types[] = {
		"REG_DWORD",
		"REG_SZ",
		"REG_EXPAND_SZ",
		"REG_MULTI_SZ",
		"REG_BINARY",
	};
#define NTYPES (sizeof(reg_types) / sizeof(const char*))
	ITEM **item;
	MENU *list;
	WINDOW *type_win;
	int sel = -1;
	size_t i;

	dia = dialog_choice_center_new(ctx, "New Value", choices, 10, 20,
				       below);
	if (dia == NULL) {
		return -1;
	}

	mvwprintw(dia->sub_window, 0, 0, "Choose type:");
	type_win = derwin(dia->sub_window, 6, 18, 1, 0);
	if (type_win == NULL) {
		goto finish;
	}

	item = talloc_zero_array(dia, ITEM *, NTYPES + 1);
	if (item == NULL) {
		goto finish;
	}

	for (i = 0; i < NTYPES; ++i) {
		int t = regtype_by_string(reg_types[i]);

		item[i] = new_item(reg_types[i], reg_types[i]);
		if (item[i] == NULL) {
			goto finish;
		}
		set_item_userptr(item[i], (void*)(uintptr_t)t);
	}

	list = new_menu(item);
	if (list == NULL) {
		goto finish;
	}

	set_menu_format(list, 7, 1);
	set_menu_win(list, dia->sub_window);
	set_menu_sub(list, type_win);
	menu_opts_off(list, O_SHOWDESC);
	set_menu_mark(list, "* ");
	post_menu(list);

	keypad(dia->window, true);
	update_panels();
	doupdate();

	while (sel == -1) {
		ITEM *it;
		int c = wgetch(dia->window);

		switch (c) {
		case KEY_UP:
			menu_driver(list, REQ_UP_ITEM);
			break;
		case KEY_DOWN:
			menu_driver(list, REQ_DOWN_ITEM);
			break;
		case KEY_LEFT:
			menu_driver(dia->choices, REQ_LEFT_ITEM);
			break;
		case KEY_RIGHT:
			menu_driver(dia->choices, REQ_RIGHT_ITEM);
			break;
		case '\n':
		case KEY_ENTER:
			it = current_item(list);
			*type = (int)(uintptr_t)item_userptr(it);
			it = current_item(dia->choices);
			sel = (int)(uintptr_t)item_userptr(it);
			break;
		}
	}

finish:
	if (list) {
		unpost_menu(list);
		free_menu(list);
	}
	if (item) {
		ITEM **it;
		for (it = item; *it; ++it) {
			free_item(*it);
		}
	}
	if (type_win) {
		delwin(type_win);
	}
	talloc_free(dia);

	return sel;
}