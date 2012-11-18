/*
 * Calcurse - text-based organizer
 *
 * Copyright (c) 2004-2012 calcurse Development Team <misc@calcurse.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the
 *        following disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the
 *        following disclaimer in the documentation and/or other
 *        materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Send your feedback or comments to : misc@calcurse.org
 * Calcurse home page : http://calcurse.org
 *
 */

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "calcurse.h"

llist_t todolist;
static int hilt = 0;
static int todos = 0;
static int first = 1;
static char *msgsav;

/* Returns a structure containing the selected item. */
struct todo *todo_get_item(int item_number)
{
  return LLIST_GET_DATA(LLIST_NTH(&todolist, item_number - 1));
}

/* Sets which todo is highlighted. */
void todo_hilt_set(int highlighted)
{
  hilt = highlighted;
}

void todo_hilt_decrease(int n)
{
  hilt -= n;
}

void todo_hilt_increase(int n)
{
  hilt += n;
}

/* Return which todo is highlighted. */
int todo_hilt(void)
{
  return hilt;
}

/* Return the number of todos. */
int todo_nb(void)
{
  return todos;
}

/* Set the number of todos. */
void todo_set_nb(int nb)
{
  todos = nb;
}

/* Set which one is the first todo to be displayed. */
void todo_set_first(int nb)
{
  first = nb;
}

void todo_first_increase(int n)
{
  first += n;
}

void todo_first_decrease(int n)
{
  first -= n;
}

/*
 * Return the position of the hilghlighted item, relative to the first one
 * displayed.
 */
int todo_hilt_pos(void)
{
  return hilt - first;
}

/* Return the last visited todo. */
char *todo_saved_mesg(void)
{
  return msgsav;
}

static int todo_cmp_id(struct todo *a, struct todo *b)
{
  /*
   * As of version 2.6, todo items can have a negative id, which means they
   * were completed. To keep them sorted, we need to consider the absolute id
   * value.
   */
  int abs_a = abs(a->id);
  int abs_b = abs(b->id);

  return abs_a < abs_b ? -1 : (abs_a == abs_b ? 0 : 1);
}

/*
 * Add an item in the todo linked list.
 */
struct todo *todo_add(char *mesg, int id, char *note)
{
  struct todo *todo;

  todo = mem_malloc(sizeof(struct todo));
  todo->mesg = mem_strdup(mesg);
  todo->id = id;
  todo->note = (note != NULL && note[0] != '\0') ? mem_strdup(note) : NULL;

  LLIST_ADD_SORTED(&todolist, todo, todo_cmp_id);

  return todo;
}

void todo_write(struct todo *todo, FILE * f)
{
  if (todo->note)
    fprintf(f, "[%d]>%s %s\n", todo->id, todo->note, todo->mesg);
  else
    fprintf(f, "[%d] %s\n", todo->id, todo->mesg);
}

/* Delete a note previously attached to a todo item. */
void todo_delete_note(struct todo *todo)
{
  if (!todo->note)
    EXIT(_("no note attached"));
  erase_note(&todo->note);
}

/* Delete an item from the todo linked list. */
void todo_delete(struct todo *todo)
{
  llist_item_t *i = LLIST_FIND_FIRST(&todolist, todo, NULL);

  if (!i)
    EXIT(_("no such todo"));

  LLIST_REMOVE(&todolist, i);
  mem_free(todo->mesg);
  erase_note(&todo->note);
  mem_free(todo);
}

/*
 * Flag a todo item (for now on, only the 'completed' state is available).
 * Internally, a completed item keeps its priority, but it becomes negative.
 * This way, it is easy to retrive its original priority if the user decides
 * that in fact it was not completed.
 */
void todo_flag(struct todo *t)
{
  t->id = -t->id;
}

/*
 * Returns the position into the linked list corresponding to the
 * given todo item.
 */
static int todo_get_position(struct todo *needle)
{
  llist_item_t *i;
  int n = 0;

  LLIST_FOREACH(&todolist, i) {
    n++;
    if (LLIST_TS_GET_DATA(i) == needle)
      return n;
  }

  EXIT(_("todo not found"));
  return -1;                    /* avoid compiler warnings */
}

/* Change an item priority by pressing '+' or '-' inside TODO panel. */
void todo_chg_priority(struct todo *backup, int diff)
{
  char backup_mesg[BUFSIZ];
  int backup_id;
  char backup_note[MAX_NOTESIZ + 1];

  strncpy(backup_mesg, backup->mesg, strlen(backup->mesg) + 1);
  backup_id = backup->id;
  if (backup->note)
    strncpy(backup_note, backup->note, MAX_NOTESIZ + 1);
  else
    backup_note[0] = '\0';

  backup_id += diff;
  if (backup_id < 1)
    backup_id = 1;
  else if (backup_id > 9)
    backup_id = 9;

  todo_delete(todo_get_item(hilt));
  backup = todo_add(backup_mesg, backup_id, backup_note);
  hilt = todo_get_position(backup);
}

/* Display todo items in the corresponding panel. */
static void
display_todo_item(int incolor, char *msg, int prio, int note, int width, int y,
                  int x)
{
  WINDOW *w;
  int ch_note;
  char buf[width * UTF8_MAXLEN], priostr[2];
  int i;

  w = win[TOD].p;
  ch_note = (note) ? '>' : '.';
  if (prio > 0)
    snprintf(priostr, sizeof priostr, "%d", prio);
  else
    strncpy(priostr, "X", sizeof priostr);

  if (incolor == 0)
    custom_apply_attr(w, ATTR_HIGHEST);
  if (utf8_strwidth(msg) < width)
    mvwprintw(w, y, x, "%s%c %s", priostr, ch_note, msg);
  else {
    for (i = 0; msg[i] && width > 0; i++) {
      if (!UTF8_ISCONT(msg[i]))
        width -= utf8_width(&msg[i]);
      buf[i] = msg[i];
    }
    if (i)
      buf[i - 1] = 0;
    else
      buf[0] = 0;
    mvwprintw(w, y, x, "%s%c %s...", priostr, ch_note, buf);
  }
  if (incolor == 0)
    custom_remove_attr(w, ATTR_HIGHEST);
}

/* Updates the ToDo panel. */
void todo_update_panel(int which_pan)
{
  llist_item_t *i;
  int len = win[TOD].w - 8;
  int num_todo = 0;
  int y_offset = 3, x_offset = 1;
  int t_realpos = -1;
  int title_lines = 3;
  int todo_lines = 1;
  int max_items = win[TOD].h - 4;
  int incolor = -1;

  /* Print todo item in the panel. */
  erase_window_part(win[TOD].p, 1, title_lines, win[TOD].w - 2, win[TOD].h - 2);
  LLIST_FOREACH(&todolist, i) {
    struct todo *todo = LLIST_TS_GET_DATA(i);
    num_todo++;
    t_realpos = num_todo - first;
    incolor = (which_pan == TOD) ? num_todo - hilt : num_todo;
    if (incolor == 0)
      msgsav = todo->mesg;
    if (t_realpos >= 0 && t_realpos < max_items) {
      display_todo_item(incolor, todo->mesg, todo->id,
                        (todo->note != NULL) ? 1 : 0, len, y_offset, x_offset);
      y_offset = y_offset + todo_lines;
    }
  }

  /* Draw the scrollbar if necessary. */
  if (todos > max_items) {
    int sbar_length = max_items * (max_items + 1) / todos;
    int highend = max_items * first / todos;
    unsigned hilt_bar = (which_pan == TOD) ? 1 : 0;
    int sbar_top = highend + title_lines;

    if ((sbar_top + sbar_length) > win[TOD].h - 1)
      sbar_length = win[TOD].h - 1 - sbar_top;
    draw_scrollbar(win[TOD].p, sbar_top, win[TOD].w - 2,
                   sbar_length, title_lines, win[TOD].h - 1, hilt_bar);
  }

  wnoutrefresh(win[TOD].p);
}

/* Attach a note to a todo */
void todo_edit_note(struct todo *i, const char *editor)
{
  edit_note(&i->note, editor);
}

/* View a note previously attached to a todo */
void todo_view_note(struct todo *i, const char *pager)
{
  view_note(i->note, pager);
}

void todo_free(struct todo *todo)
{
  mem_free(todo->mesg);
  erase_note(&todo->note);
  mem_free(todo);
}

void todo_init_list(void)
{
  LLIST_INIT(&todolist);
}

void todo_free_list(void)
{
  LLIST_FREE_INNER(&todolist, todo_free);
  LLIST_FREE(&todolist);
}
