/* $OpenBSD$ */

/*
 * Copyright (c) 2009 Nicholas Marriott <nicholas.marriott@gmail.com>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF MIND, USE, DATA OR PROFITS, WHETHER
 * IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING
 * OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/types.h>

#include <fnmatch.h>
#include <stdlib.h>
#include <string.h>

#include "tmux.h"

/*
 * Find window containing text.
 */

static enum cmd_retval	cmd_find_window_exec(struct cmd *, struct cmdq_item *);

/* Flags for determining matching behavior. */
#define CMD_FIND_WINDOW_BY_TITLE   0x1
#define CMD_FIND_WINDOW_BY_CONTENT 0x2
#define CMD_FIND_WINDOW_BY_NAME    0x4

#define CMD_FIND_WINDOW_ALL		\
	(CMD_FIND_WINDOW_BY_TITLE |	\
	 CMD_FIND_WINDOW_BY_CONTENT |	\
	 CMD_FIND_WINDOW_BY_NAME)

const struct cmd_entry cmd_find_window_entry = {
	.name = "find-window",
	.alias = "findw",

	.args = { "CNt:T", 1, 4 },
	.usage = "[-CNT] " CMD_TARGET_PANE_USAGE " match-string",

	.target = { 't', CMD_FIND_PANE, 0 },

	.flags = 0,
	.exec = cmd_find_window_exec
};

#if XXX
struct cmd_find_window_data {
	struct winlink	*wl;
	char		*list_ctx;
	u_int		 pane_id;
	TAILQ_ENTRY(cmd_find_window_data) entry;
};
TAILQ_HEAD(cmd_find_window_list, cmd_find_window_data);

static u_int	cmd_find_window_match_flags(struct args *);
static void	cmd_find_window_match(struct cmd_find_window_list *, int,
		    struct winlink *, const char *, const char *);

static u_int
cmd_find_window_match_flags(struct args *args)
{
	u_int	match_flags = 0;

	/* Turn on flags based on the options. */
	if (args_has(args, 'T'))
		match_flags |= CMD_FIND_WINDOW_BY_TITLE;
	if (args_has(args, 'C'))
		match_flags |= CMD_FIND_WINDOW_BY_CONTENT;
	if (args_has(args, 'N'))
		match_flags |= CMD_FIND_WINDOW_BY_NAME;

	/* If none of the flags were set, default to matching anything. */
	if (match_flags == 0)
		match_flags = CMD_FIND_WINDOW_ALL;

	return (match_flags);
}

static void
cmd_find_window_match(struct cmd_find_window_list *find_list,
    int match_flags, struct winlink *wl, const char *str,
    const char *searchstr)
{
	struct cmd_find_window_data	*find_data;
	struct window_pane		*wp;
	u_int				 i, line;
	char				*sres;

	find_data = xcalloc(1, sizeof *find_data);

	i = 0;
	TAILQ_FOREACH(wp, &wl->window->panes, entry) {
		i++;

		if ((match_flags & CMD_FIND_WINDOW_BY_NAME) &&
		    fnmatch(searchstr, wl->window->name, 0) == 0) {
			find_data->list_ctx = xstrdup("");
			break;
		}

		if ((match_flags & CMD_FIND_WINDOW_BY_TITLE) &&
		    fnmatch(searchstr, wp->base.title, 0) == 0) {
			xasprintf(&find_data->list_ctx,
			    "pane %u title: \"%s\"", i - 1, wp->base.title);
			break;
		}

		if (match_flags & CMD_FIND_WINDOW_BY_CONTENT &&
		    (sres = window_pane_search(wp, str, &line)) != NULL) {
			xasprintf(&find_data->list_ctx,
			    "pane %u line %u: \"%s\"", i - 1, line + 1, sres);
			free(sres);
			break;
		}
	}

	if (find_data->list_ctx != NULL) {
		find_data->wl = wl;
		find_data->pane_id = i - 1;
		TAILQ_INSERT_TAIL(find_list, find_data, entry);
	} else
		free(find_data);
}
#endif

static enum cmd_retval
cmd_find_window_exec(struct cmd *self, struct cmdq_item *item)
{
#if XXX
	struct args			*args = self->args;
	struct cmd_find_state		*current = &item->shared->current;
	struct client			*c = cmd_find_client(item, NULL, 1);
	struct window_choose_data	*cdata;
	struct session			*s = item->target.s;
	struct winlink			*wl = item->target.wl, *wm;
	struct cmd_find_window_list	 find_list;
	struct cmd_find_window_data	*find_data;
	struct cmd_find_window_data	*find_data1;
	char				*str, *searchstr;
	const char			*template;
	u_int				 i, match_flags;

	if (c == NULL) {
		cmdq_error(item, "no client available");
		return (CMD_RETURN_ERROR);
	}

	if ((template = args_get(args, 'F')) == NULL)
		template = FIND_WINDOW_TEMPLATE;

	match_flags = cmd_find_window_match_flags(args);
	str = args->argv[0];

	TAILQ_INIT(&find_list);

	xasprintf(&searchstr, "*%s*", str);
	RB_FOREACH(wm, winlinks, &s->windows)
	    cmd_find_window_match(&find_list, match_flags, wm, str, searchstr);
	free(searchstr);

	if (TAILQ_EMPTY(&find_list)) {
		cmdq_error(item, "no windows matching: %s", str);
		return (CMD_RETURN_ERROR);
	}

	if (TAILQ_NEXT(TAILQ_FIRST(&find_list), entry) == NULL) {
		if (session_select(s, TAILQ_FIRST(&find_list)->wl->idx) == 0) {
			cmd_find_from_session(current, s);
			server_redraw_session(s);
		}
		recalculate_sizes();
		goto out;
	}

	if (window_pane_set_mode(wl->window->active, &window_choose_mode, NULL) != 0)
		goto out;

	i = 0;
	TAILQ_FOREACH(find_data, &find_list, entry) {
		cdata = window_choose_data_create(TREE_OTHER, c, c->session);
		cdata->idx = find_data->wl->idx;
		cdata->wl = find_data->wl;

		cdata->ft_template = xstrdup(template);
		cdata->pane_id = find_data->pane_id;

		format_add(cdata->ft, "line", "%u", i);
		format_add(cdata->ft, "window_find_matches", "%s",
		    find_data->list_ctx);
		format_defaults(cdata->ft, NULL, s, find_data->wl, NULL);

		window_choose_add(wl->window->active, cdata);

		i++;
	}

	window_choose_ready(wl->window->active, 0, cmd_find_window_callback);

out:
	TAILQ_FOREACH_SAFE(find_data, &find_list, entry, find_data1) {
		free(find_data->list_ctx);
		TAILQ_REMOVE(&find_list, find_data, entry);
		free(find_data);
	}
#endif
	return (CMD_RETURN_NORMAL);
}
