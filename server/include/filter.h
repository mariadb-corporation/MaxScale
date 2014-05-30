#ifndef _FILTER_H
#define _FILTER_H
/*
 * This file is distributed as part of the SkySQL Gateway.  It is free
 * software: you can redistribute it and/or modify it under the terms of the
 * GNU General Public License as published by the Free Software Foundation,
 * version 2.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Copyright SkySQL Ab 2014
 */

/**
 * @file filter.h -  The filter interface mechanisms
 *
 * Revision History
 *
 * Date		Who			Description
 * 27/05/2014	Mark Riddoch		Initial implementation
 *
 */
#include <dcb.h>
#include <session.h>
#include <buffer.h>
#include <stdint.h>

/**
 * The FILTER handle points to module specific data, so the best we can do
 * is to make it a void * externally.
 */
typedef void *FILTER;
/**
 * @verbatim
 * The "module object" structure for a query router module
 *
 * The entry points are:
 * 	createInstance		Called by the service to create a new
 * 				instance of the filter 
 * 	newSession		Called to create a new user session
 * 				within the filter
 * 	closeSession		Called when a session is closed
 * 	freeSession		Called when a session is freed
 *	setDownstream		Sets the downstream component of the
 *				filter pipline
 * 	routeQuery		Called on each query that requires
 * 				routing
 * 	diagnostics		Called to force the filter to print
 * 				diagnostic output
 *
 * @endverbatim
 *
 * @see load_module
 */
typedef struct filter_object {
	FILTER	*(*createInstance)(char **options);
	void	*(*newSession)(FILTER *instance, SESSION *session);
	void 	(*closeSession)(FILTER *instance, void *fsession);
        void 	(*freeSession)(FILTER *instance, void *fsession);
	void	(*setDownstream)(FILTER *instance, void *fsession, DOWNSTREAM *downstream);
	int	(*routeQuery)(FILTER *instance, void *fsession, GWBUF *queue);
	void	(*diagnostics)(FILTER *instance, void *fsession, DCB *dcb);
} FILTER_OBJECT;

/**
 * The definition of a filter form the configuration file.
 * This is basically the link between a plugin to load and the
 * optons to pass to that plugin.
 */
typedef struct filter_def {
	char		*name;		/*< The Filter name */
	char		*module;	/*< The module to load */
	char		**options;	/*< The options set for this filter */
	FILTER		filter;
	FILTER_OBJECT	*obj;
	SPINLOCK	spin;
	struct	filter_def
			*next;		/*< Next filter in the chain of all filters */
} FILTER_DEF;

FILTER_DEF	*filter_alloc(char *, char *);
void		filter_free(FILTER_DEF *);
FILTER_DEF	*filter_find(char *);
void		filterAddOption(FILTER_DEF *, char *);
DOWNSTREAM	*filterApply(FILTER_DEF *, SESSION *, DOWNSTREAM *);
void		dprintAllFilters(DCB *);
void		dprintFilter(DCB *, FILTER_DEF *);
void		dListFilters(DCB *);
#endif
