#ifndef RANDOM_JKISS_H
#define	RANDOM_JKISS_H
/*
 * This file is distributed as part of the MariaDB Corporation MaxScale.  It is free
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
 * Copyright MariaDB Corporation Ab 2013-2014
 */

/*
 * File:   random_jkiss.h
 * Author: mbrampton
 *
 * Created on 26 August 2015, 15:34
 */

#ifdef	__cplusplus
extern "C" {
#endif

extern unsigned int random_jkiss(void);

#ifdef	__cplusplus
}
#endif

#endif	/* RANDOM_H */
